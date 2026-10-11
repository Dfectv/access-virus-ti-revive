// vtibroker.c - per-user background process that owns the Virus TI over WinUSB and serves clients
// (wrapper VST3 instances, later Control Center and MIDI ports) over \\.\pipe\vtibroker (vtiproto.h).
//
// Why a broker (findings/design.md, E042): WinUSB allows one owner per interface, and the device stops
// accepting OUT data when nobody drains EP 0x85/0x86. The broker keeps the device open and drained
// permanently, sends the handshake once per device arrival, reconnects after unplug/errors, and
// fans received data out to every connected client.
//
// usage: vtibroker.exe [-v]      (-v: log every message; log file %TEMP%\vtibroker.log)
#include "vticore.h"
#include "vtiproto.h"
#include "vtipipe_io.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_CLIENTS 16
#define IDLE_EXIT_MS 30000 // exit when no client has been connected for this long (started on demand, vticore_pipe.c)

static HANDLE g_log = INVALID_HANDLE_VALUE;
static int g_verbose;
static CRITICAL_SECTION g_cs_log, g_cs_dev, g_cs_clients;
static vti *g_dev;
static volatile LONG g_dev_failed; // set by the RX path on fatal device errors

#define QUEUE_BYTES (256 * 1024)

// Messages to a client go through a byte queue drained by the client's own sender thread, so a slow or
// stuck client can never block the device readers (they must keep EP 0x85/0x86 drained, E042).
typedef struct {
    HANDLE pipe;
    CRITICAL_SECTION q; // protects queue
    HANDLE q_event;     // set when data is queued or the client dies
    uint8_t *queue;
    uint32_t q_head, q_len;
    volatile LONG alive;
    HANDLE sender;
} client;
static client *g_clients[MAX_CLIENTS];

static void blog(const char *fmt, ...)
{
    char buf[1024];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2)
        n = sizeof buf - 2;
    while (n > 0 && buf[n - 1] == '\n')
        n--;
    buf[n++] = '\n';
    DWORD w;
    EnterCriticalSection(&g_cs_log);
    WriteFile(g_log, buf, n, &w, NULL);
    fwrite(buf, 1, n, stdout);
    LeaveCriticalSection(&g_cs_log);
}

// Queue one message (header + a + b). On overflow the client is dropped instead of blocking the caller.
static int send_msg(client *c, uint16_t type, const void *a, int alen, const void *b, int blen)
{
    if (!c->alive || alen + blen > VTIMSG_MAX)
        return -1;
    vtimsg h = {type, (uint16_t)(alen + blen)};
    uint32_t total = sizeof h + alen + blen;
    EnterCriticalSection(&c->q);
    int ok = c->q_len + total <= QUEUE_BYTES;
    if (ok) {
        const uint8_t *parts[3] = {(const uint8_t *)&h, a, b};
        uint32_t lens[3] = {sizeof h, (uint32_t)alen, (uint32_t)blen};
        for (int p = 0; p < 3; p++)
            for (uint32_t i = 0; i < lens[p]; i++)
                c->queue[(c->q_head + c->q_len++) % QUEUE_BYTES] = parts[p][i];
    } else {
        c->alive = 0;
    }
    LeaveCriticalSection(&c->q);
    SetEvent(c->q_event);
    if (!ok)
        blog("client queue overflow: dropping client");
    return ok ? 0 : -1;
}

static DWORD WINAPI sender_thread(LPVOID arg)
{
    client *c = arg;
    uint8_t chunk[8192];
    while (c->alive) {
        WaitForSingleObject(c->q_event, 500);
        for (;;) {
            EnterCriticalSection(&c->q);
            uint32_t n = c->q_len < sizeof chunk ? c->q_len : sizeof chunk;
            for (uint32_t i = 0; i < n; i++)
                chunk[i] = c->queue[(c->q_head + i) % QUEUE_BYTES];
            c->q_head = (c->q_head + n) % QUEUE_BYTES;
            c->q_len -= n;
            LeaveCriticalSection(&c->q);
            if (!n)
                break;
            if (vtipipe_write(c->pipe, chunk, n)) {
                c->alive = 0;
                break;
            }
        }
    }
    CancelIoEx(c->pipe, NULL); // wakes the client thread's pending read
    return 0;
}

static void broadcast(uint16_t type, const void *a, int alen, const void *b, int blen)
{
    EnterCriticalSection(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i])
            send_msg(g_clients[i], type, a, alen, b, blen);
    LeaveCriticalSection(&g_cs_clients);
}

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
    if (g_verbose)
        blog("rx ep%02x %d bytes", ep, len);
    broadcast(VTIMSG_RX, &ep, 1, data, len);
}

static void on_vti_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    blog("vticore: %s", buf);
}

// Device thread: open the device when present, reopen after failures (unplug, wedged pipes).
static int g_stay; // -stay: never exit when idle (debugging, or started by hand)

static int client_count_locked(void)
{
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += g_clients[i] != NULL;
    return n;
}

static int client_count(void)
{
    int n = 0;
    EnterCriticalSection(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += g_clients[i] != NULL;
    LeaveCriticalSection(&g_cs_clients);
    return n;
}

static DWORD WINAPI device_thread(LPVOID unused)
{
    (void)unused;
    DWORD last_busy = GetTickCount();
    for (;;) {
        if (client_count())
            last_busy = GetTickCount();
        else if (!g_stay && GetTickCount() - last_busy > IDLE_EXIT_MS) {
            EnterCriticalSection(&g_cs_clients); // no client may connect in between
            if (!client_count_locked()) {
                EnterCriticalSection(&g_cs_dev);
                if (g_dev)
                    vti_close(g_dev);
                blog("no clients for %d s: exiting", IDLE_EXIT_MS / 1000);
                ExitProcess(0);
            }
            LeaveCriticalSection(&g_cs_clients);
        }
        EnterCriticalSection(&g_cs_dev);
        if (g_dev && (g_dev_failed || vti_is_dead(g_dev))) {
            blog("device failed: closing");
            vti_close(g_dev);
            g_dev = NULL;
            uint32_t st = 0;
            broadcast(VTIMSG_STATE, &st, 4, NULL, 0);
        }
        if (!g_dev) {
            g_dev_failed = 0;
            g_dev = vti_open(on_rx, NULL); // starts the IN readers, then sends the handshake
            if (g_dev) {
                blog("device open, handshake sent");
                uint32_t st = 1;
                broadcast(VTIMSG_STATE, &st, 4, NULL, 0);
            }
        }
        LeaveCriticalSection(&g_cs_dev);
        Sleep(g_dev ? 500 : 2000);
    }
    return 0;
}

static DWORD WINAPI client_thread(LPVOID arg)
{
    client *c = arg;
    uint8_t buf[VTIMSG_MAX];
    blog("client connected");
    {
        EnterCriticalSection(&g_cs_dev);
        uint32_t st = g_dev != NULL;
        LeaveCriticalSection(&g_cs_dev);
        send_msg(c, VTIMSG_STATE, &st, 4, NULL, 0);
    }
    for (;;) {
        vtimsg h;
        if (vtipipe_read(c->pipe, &h, sizeof h) || h.len > VTIMSG_MAX || vtipipe_read(c->pipe, buf, h.len))
            break;
        switch (h.type) {
        case VTIMSG_HELLO:
            blog("client hello, protocol %u", h.len >= 4 ? *(uint32_t *)buf : 0);
            break;
        case VTIMSG_WRITE:
            if (h.len >= 1) {
                EnterCriticalSection(&g_cs_dev);
                int r = g_dev ? vti_write(g_dev, buf[0], buf + 1, h.len - 1) : -1;
                LeaveCriticalSection(&g_cs_dev);
                if (g_verbose || r)
                    blog("write ep%02x %d bytes -> %d", buf[0], h.len - 1, r);
            }
            break;
        case VTIMSG_CONTROL:
            if (h.len >= 12) {
                uint32_t seq = *(uint32_t *)buf;
                uint8_t *s = buf + 4, data[VTIMSG_MAX];
                uint16_t wl = s[6] | s[7] << 8;
                if (wl > VTIMSG_MAX - 16)
                    wl = VTIMSG_MAX - 16;
                if (!(s[0] & 0x80))
                    memcpy(data, buf + 12, h.len - 12 < wl ? h.len - 12 : wl);
                EnterCriticalSection(&g_cs_dev);
                int32_t r = g_dev ? vti_control(g_dev, s[0], s[1], s[2] | s[3] << 8, s[4] | s[5] << 8, data, wl) : -1;
                LeaveCriticalSection(&g_cs_dev);
                uint8_t hdr[8];
                memcpy(hdr, &seq, 4);
                memcpy(hdr + 4, &r, 4);
                send_msg(c, VTIMSG_CONTROL_REPLY, hdr, 8, data, (s[0] & 0x80) && r > 0 ? r : 0);
            }
            break;
        default:
            blog("client: unknown message %u", h.type);
        }
    }
    blog("client disconnected");
    EnterCriticalSection(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] == c)
            g_clients[i] = NULL;
    LeaveCriticalSection(&g_cs_clients);
    c->alive = 0;
    SetEvent(c->q_event);
    WaitForSingleObject(c->sender, INFINITE); // removed from g_clients above: no new messages can arrive
    CloseHandle(c->sender);
    DisconnectNamedPipe(c->pipe);
    CloseHandle(c->pipe);
    CloseHandle(c->q_event);
    DeleteCriticalSection(&c->q);
    free(c->queue);
    free(c);
    return 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        g_verbose |= !strcmp(argv[i], "-v");
        g_stay |= !strcmp(argv[i], "-stay");
    }
    InitializeCriticalSection(&g_cs_log);
    InitializeCriticalSection(&g_cs_dev);
    InitializeCriticalSection(&g_cs_clients);
    char path[MAX_PATH + 32], tmp[MAX_PATH];
    GetTempPathA(sizeof tmp, tmp);
    snprintf(path, sizeof path, "%svtibroker.log", tmp);
    g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    setvbuf(stdout, NULL, _IONBF, 0);
    // Single instance per session.
    HANDLE mtx = CreateMutexA(NULL, TRUE, "Local\\vtibroker");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        blog("another vtibroker is running, exiting");
        return 0;
    }
    (void)mtx;
    vti_log = on_vti_log;
    blog("vtibroker starting (protocol %d)", VTI_PROTO_VERSION);
    CloseHandle(CreateThread(NULL, 0, device_thread, NULL, 0, NULL));
    for (;;) {
        HANDLE p = CreateNamedPipeA(VTI_PIPE_NAME, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 65536,
                                    65536, 0, NULL);
        if (p == INVALID_HANDLE_VALUE) {
            blog("CreateNamedPipe failed: %lu", GetLastError());
            Sleep(1000);
            continue;
        }
        OVERLAPPED ov = {0};
        ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        BOOL connected = ConnectNamedPipe(p, &ov);
        DWORD e = GetLastError(), dummy;
        if (!connected && e == ERROR_IO_PENDING)
            connected = GetOverlappedResult(p, &ov, &dummy, TRUE);
        else if (!connected && e == ERROR_PIPE_CONNECTED)
            connected = TRUE;
        CloseHandle(ov.hEvent);
        if (!connected) {
            CloseHandle(p);
            continue;
        }
        client *c = calloc(1, sizeof *c);
        c->pipe = p;
        c->alive = 1;
        c->queue = malloc(QUEUE_BYTES);
        c->q_event = CreateEventA(NULL, FALSE, FALSE, NULL);
        InitializeCriticalSection(&c->q);
        int slot = -1;
        EnterCriticalSection(&g_cs_clients);
        for (int i = 0; i < MAX_CLIENTS && slot < 0; i++)
            if (!g_clients[i]) {
                g_clients[i] = c;
                slot = i;
            }
        LeaveCriticalSection(&g_cs_clients);
        if (slot < 0) {
            blog("too many clients");
            DisconnectNamedPipe(p);
            CloseHandle(p);
            CloseHandle(c->q_event);
            DeleteCriticalSection(&c->q);
            free(c->queue);
            free(c);
            continue;
        }
        c->sender = CreateThread(NULL, 0, sender_thread, c, 0, NULL);
        CloseHandle(CreateThread(NULL, 0, client_thread, c, 0, NULL));
    }
}
