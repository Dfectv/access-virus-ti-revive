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

static HANDLE g_log = INVALID_HANDLE_VALUE;
static int g_verbose;
static CRITICAL_SECTION g_cs_log, g_cs_dev, g_cs_clients;
static vti *g_dev;
static volatile LONG g_dev_failed; // set by the RX path on fatal device errors

typedef struct {
    HANDLE pipe;
    CRITICAL_SECTION wr; // serializes writes to this client
    volatile LONG alive;
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

static int send_msg(client *c, uint16_t type, const void *a, int alen, const void *b, int blen)
{
    if (!c->alive)
        return -1;
    uint8_t buf[sizeof(vtimsg) + VTIMSG_MAX];
    if (alen + blen > VTIMSG_MAX)
        return -1;
    vtimsg *h = (vtimsg *)buf;
    h->type = type;
    h->len = (uint16_t)(alen + blen);
    if (alen)
        memcpy(buf + sizeof *h, a, alen);
    if (blen)
        memcpy(buf + sizeof *h + alen, b, blen);
    EnterCriticalSection(&c->wr);
    int r = vtipipe_write(c->pipe, buf, sizeof *h + alen + blen);
    LeaveCriticalSection(&c->wr);
    if (r)
        c->alive = 0;
    return r;
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
    if (strstr(buf, "error 22") || strstr(buf, "error 1167") || strstr(buf, "error 6\n")) // gone / bad handle
        g_dev_failed = 1;
}

// Device thread: open the device when present, reopen after failures (unplug, wedged pipes).
static DWORD WINAPI device_thread(LPVOID unused)
{
    (void)unused;
    for (;;) {
        EnterCriticalSection(&g_cs_dev);
        if (g_dev && g_dev_failed) {
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
    EnterCriticalSection(&c->wr); // no broadcast is writing to this pipe any more
    LeaveCriticalSection(&c->wr);
    DisconnectNamedPipe(c->pipe);
    CloseHandle(c->pipe);
    DeleteCriticalSection(&c->wr);
    free(c);
    return 0;
}

int main(int argc, char **argv)
{
    g_verbose = argc > 1 && !strcmp(argv[1], "-v");
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
        InitializeCriticalSection(&c->wr);
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
            DeleteCriticalSection(&c->wr);
            free(c);
            continue;
        }
        CloseHandle(CreateThread(NULL, 0, client_thread, c, 0, NULL));
    }
}
