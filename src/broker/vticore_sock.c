// vticore_sock.c - client backend of vticore.h for the Windows wrapper running under Wine on Linux
// (findings/design-linux.md, L2): talks to the NATIVE Linux vtibroker over its Unix domain socket
// (/tmp/vtibroker-<uid>.sock; Wine's Winsock supports AF_UNIX and maps the Windows path to the Unix one) with the
// same messages as vticore_pipe.c. Broker audio (protocol 3): the broker's POSIX shared memory
// /dev/shm/tirevive-audio-<uid> is exposed to vtiaudio.c under the usual name VTI_AUDIO_SHM_NAME (a Windows file
// mapping of that file is a MAP_SHARED mapping in Wine), and every VTIMSG_AUDIO_TICK sets the client's event, so
// vtiaudio.c follows the ring exactly as on Windows.
// The broker is started by systemd socket activation (tirevive-broker.socket), not from here.
#include "vticore.h"
#include "vtiaudioshm.h"
#include "vtiproto.h"
#include <winsock2.h>
#include <afunix.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vti {
    SOCKET s;
    vti_rx_cb cb;
    void *user;
    HANDLE rd;
    CRITICAL_SECTION wr;
    volatile LONG stop, connected;
    CRITICAL_SECTION ctl_cs; // one outstanding control / audio request at a time
    HANDLE ctl_done;
    uint32_t ctl_seq;
    int32_t ctl_result;
    uint8_t *ctl_buf;
    uint16_t ctl_len;
    HANDLE audio_done;
    volatile int32_t audio_result;
    HANDLE audio_ev;           // the audio client's event, set on every VTIMSG_AUDIO_TICK
    HANDLE shm_file, shm_map;  // keep the named mapping alive for vtiaudio.c's OpenFileMapping
};

// The Unix uid of this session: Wine passes the Unix environment through; XDG_RUNTIME_DIR is /run/user/<uid>.
static unsigned unix_uid(void)
{
    char buf[256];
    if (GetEnvironmentVariableA("VTI_UID", buf, sizeof buf))
        return (unsigned)strtoul(buf, NULL, 10);
    if (GetEnvironmentVariableA("XDG_RUNTIME_DIR", buf, sizeof buf)) {
        const char *p = strstr(buf, "/run/user/");
        if (p)
            return (unsigned)strtoul(p + 10, NULL, 10);
    }
    return 1000;
}

static int sock_write(SOCKET s, const void *p, int n)
{
    for (const char *b = p; n > 0;) {
        int w = send(s, b, n, 0);
        if (w <= 0)
            return -1;
        b += w;
        n -= w;
    }
    return 0;
}

static int sock_read(SOCKET s, void *p, int n)
{
    for (char *b = p; n > 0;) {
        int r = recv(s, b, n, 0);
        if (r <= 0)
            return -1;
        b += r;
        n -= r;
    }
    return 0;
}

static int send_msg(vti *v, uint16_t type, const void *a, int alen, const void *b, int blen)
{
    uint8_t buf[sizeof(vtimsg) + VTIMSG_MAX];
    if (alen + blen > VTIMSG_MAX)
        return -1;
    vtimsg *h = (vtimsg *)buf;
    h->type = type;
    h->len = (uint16_t)(alen + blen);
    memcpy(buf + sizeof *h, a, alen);
    if (blen)
        memcpy(buf + sizeof *h + alen, b, blen);
    EnterCriticalSection(&v->wr);
    int r = sock_write(v->s, buf, (int)(sizeof *h + alen + blen));
    LeaveCriticalSection(&v->wr);
    return r;
}

static DWORD WINAPI reader(LPVOID arg)
{
    vti *v = arg;
    uint8_t buf[VTIMSG_MAX];
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL); // audio ticks pass through here
    while (!v->stop) {
        vtimsg h;
        if (sock_read(v->s, &h, sizeof h) || h.len > VTIMSG_MAX || sock_read(v->s, buf, h.len)) {
            if (!v->stop)
                vti_log("vti: broker connection lost\n");
            v->connected = 0;
            SetEvent(v->ctl_done);
            SetEvent(v->audio_done);
            break;
        }
        switch (h.type) {
        case VTIMSG_RX:
            if (h.len >= 1)
                v->cb(v->user, buf[0], buf + 1, h.len - 1);
            break;
        case VTIMSG_CONTROL_REPLY:
            if (h.len >= 8 && *(uint32_t *)buf == v->ctl_seq) {
                v->ctl_result = *(int32_t *)(buf + 4);
                int n = h.len - 8 < v->ctl_len ? h.len - 8 : v->ctl_len;
                if (n > 0 && v->ctl_buf)
                    memcpy(v->ctl_buf, buf + 8, n);
                SetEvent(v->ctl_done);
            }
            break;
        case VTIMSG_AUDIO_REPLY:
            v->audio_result = h.len >= 4 ? *(int32_t *)buf : -1;
            SetEvent(v->audio_done);
            break;
        case VTIMSG_AUDIO_TICK: {
            HANDLE ev = v->audio_ev;
            if (ev)
                SetEvent(ev);
            break;
        }
        case VTIMSG_STATE:
            v->connected = h.len >= 4 && *(uint32_t *)buf;
            vti_log("vti: broker reports device %s\n", v->connected ? "connected" : "disconnected");
            break;
        }
    }
    return 0;
}

static SOCKET connect_broker(void)
{
    static int wsa;
    if (!wsa) {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d))
            return INVALID_SOCKET;
        wsa = 1;
    }
    // Wine converts the Windows path to the Unix one (Z: = /); try that and the plain Unix path
    unsigned uid = unix_uid();
    static const char *fmt[2] = {"Z:\\tmp\\vtibroker-%u.sock", "/tmp/vtibroker-%u.sock"};
    for (int i = 0; i < 2; i++) {
        SOCKET s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) {
            vti_log("vti: AF_UNIX sockets not available (error %d)\n", WSAGetLastError());
            return INVALID_SOCKET;
        }
        SOCKADDR_UN sa = {.sun_family = AF_UNIX};
        snprintf(sa.sun_path, sizeof sa.sun_path, fmt[i], uid);
        if (!connect(s, (struct sockaddr *)&sa, sizeof sa))
            return s;
        vti_log("vti: connect %s: error %d\n", sa.sun_path, WSAGetLastError());
        closesocket(s);
    }
    return INVALID_SOCKET;
}

vti *vti_open(vti_rx_cb cb, void *user)
{
    SOCKET s = INVALID_SOCKET;
    for (int i = 0; i < 20 && s == INVALID_SOCKET; i++) { // systemd starts the broker on the first connect
        s = connect_broker();
        if (s == INVALID_SOCKET)
            Sleep(100);
    }
    if (s == INVALID_SOCKET) {
        vti_log("vti: vtibroker not reachable (TI Revive broker not installed / socket unit not enabled?)\n");
        return NULL;
    }
    vti *v = calloc(1, sizeof *v);
    v->s = s;
    v->cb = cb;
    v->user = user;
    InitializeCriticalSection(&v->wr);
    InitializeCriticalSection(&v->ctl_cs);
    v->ctl_done = CreateEventA(NULL, FALSE, FALSE, NULL);
    v->audio_done = CreateEventA(NULL, FALSE, FALSE, NULL);
    uint32_t ver = VTI_PROTO_VERSION;
    send_msg(v, VTIMSG_HELLO, &ver, 4, NULL, 0);
    v->rd = CreateThread(NULL, 0, reader, v, 0, NULL);
    for (int i = 0; i < 500 && !v->connected; i++) // a freshly started broker opens the device first
        Sleep(10);
    if (!v->connected) {
        vti_log("vti: broker has no device\n");
        vti_close(v);
        return NULL;
    }
    return v;
}

static void audio_release(vti *v)
{
    HANDLE ev = v->audio_ev;
    v->audio_ev = NULL;
    if (ev)
        CloseHandle(ev);
    if (v->shm_map)
        CloseHandle(v->shm_map);
    if (v->shm_file && v->shm_file != INVALID_HANDLE_VALUE)
        CloseHandle(v->shm_file);
    v->shm_map = v->shm_file = NULL;
}

void vti_close(vti *v)
{
    if (!v)
        return;
    v->stop = 1;
    shutdown(v->s, SD_BOTH);
    closesocket(v->s);
    if (v->rd) {
        WaitForSingleObject(v->rd, 2000);
        CloseHandle(v->rd);
    }
    audio_release(v);
    CloseHandle(v->ctl_done);
    CloseHandle(v->audio_done);
    DeleteCriticalSection(&v->wr);
    DeleteCriticalSection(&v->ctl_cs);
    free(v);
}

int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len)
{
    for (int off = 0; off < len;) {
        int n = len - off > 1024 ? 1024 : len - off;
        if (send_msg(v, VTIMSG_WRITE, &ep, 1, data + off, n))
            return -1;
        off += n;
    }
    return 0;
}

int vti_control(vti *v, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data,
                uint16_t len)
{
    EnterCriticalSection(&v->ctl_cs);
    uint8_t hdr[12];
    uint32_t seq = ++v->ctl_seq;
    memcpy(hdr, &seq, 4);
    hdr[4] = bmRequestType;
    hdr[5] = bRequest;
    hdr[6] = (uint8_t)wValue;
    hdr[7] = (uint8_t)(wValue >> 8);
    hdr[8] = (uint8_t)wIndex;
    hdr[9] = (uint8_t)(wIndex >> 8);
    hdr[10] = (uint8_t)len;
    hdr[11] = (uint8_t)(len >> 8);
    v->ctl_buf = (bmRequestType & 0x80) ? data : NULL;
    v->ctl_len = len;
    v->ctl_result = -1;
    ResetEvent(v->ctl_done);
    int r = -1;
    if (!send_msg(v, VTIMSG_CONTROL, hdr, 12, (bmRequestType & 0x80) ? NULL : data, (bmRequestType & 0x80) ? 0 : len) &&
        WaitForSingleObject(v->ctl_done, 3000) == WAIT_OBJECT_0)
        r = v->ctl_result;
    v->ctl_buf = NULL;
    LeaveCriticalSection(&v->ctl_cs);
    return r;
}

// Expose the broker's /dev/shm ring under VTI_AUDIO_SHM_NAME for vtiaudio.c (OpenFileMapping by name).
static int audio_map(vti *v)
{
    if (v->shm_map)
        return 0;
    wchar_t path[64];
    _snwprintf(path, 64, L"Z:\\dev\\shm\\tirevive-audio-%u", unix_uid());
    v->shm_file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, 0, NULL);
    if (v->shm_file == INVALID_HANDLE_VALUE) {
        vti_log("vti: audio ring %ls: error %lu\n", path, GetLastError());
        v->shm_file = NULL;
        return -1;
    }
    v->shm_map = CreateFileMappingA(v->shm_file, NULL, PAGE_READWRITE, 0, sizeof(vti_audio_shm), VTI_AUDIO_SHM_NAME);
    if (!v->shm_map) {
        vti_log("vti: audio ring mapping: error %lu\n", GetLastError());
        audio_release(v);
        return -1;
    }
    return 0;
}

int vti_audio(vti *v, int on, const char *event_name)
{
    uint32_t o = on ? 1 : 0;
    EnterCriticalSection(&v->ctl_cs);
    v->audio_result = -1;
    ResetEvent(v->audio_done);
    int r = -1;
    if (!send_msg(v, VTIMSG_AUDIO, &o, 4, NULL, 0) && WaitForSingleObject(v->audio_done, 4000) == WAIT_OBJECT_0)
        r = v->audio_result;
    if (on && r == 1) {
        HANDLE ev = event_name ? OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name) : NULL;
        HANDLE old = v->audio_ev;
        v->audio_ev = ev;
        if (old)
            CloseHandle(old);
        if (!ev || audio_map(v))
            r = -1;
    } else if (!on) {
        audio_release(v);
    }
    LeaveCriticalSection(&v->ctl_cs);
    return r;
}

int vti_is_dead(vti *v)
{
    return !v->connected;
}
