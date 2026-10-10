// vticore_pipe.c - client backend of vticore.h that talks to vtibroker.exe over \\.\pipe\vtibroker
// (vtiproto.h) instead of opening WinUSB itself. Used by the wrapper VST3 (vtiemu) so several plugin
// instances and Control Center can share the device (findings/design.md, M3).
#include "vticore.h"
#include "vtiproto.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vti {
    HANDLE pipe;
    vti_rx_cb cb;
    void *user;
    HANDLE rd;
    CRITICAL_SECTION wr;
    volatile LONG stop, connected;
    // one outstanding control request at a time (callers are serialized by ctl_cs)
    CRITICAL_SECTION ctl_cs;
    HANDLE ctl_done;
    uint32_t ctl_seq;
    int32_t ctl_result;
    uint8_t *ctl_buf;
    uint16_t ctl_len;
};

static int read_full(HANDLE p, void *buf, DWORD n)
{
    DWORD got = 0;
    while (got < n) {
        DWORD r = 0;
        if (!ReadFile(p, (uint8_t *)buf + got, n - got, &r, NULL) || !r)
            return -1;
        got += r;
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
    DWORD w = 0, total = sizeof *h + alen + blen;
    EnterCriticalSection(&v->wr);
    BOOL ok = WriteFile(v->pipe, buf, total, &w, NULL);
    LeaveCriticalSection(&v->wr);
    return ok && w == total ? 0 : -1;
}

static DWORD WINAPI reader(LPVOID arg)
{
    vti *v = arg;
    uint8_t buf[VTIMSG_MAX];
    while (!v->stop) {
        vtimsg h;
        if (read_full(v->pipe, &h, sizeof h) || h.len > VTIMSG_MAX || read_full(v->pipe, buf, h.len)) {
            if (!v->stop)
                vti_log("vti: broker connection lost\n");
            v->connected = 0;
            SetEvent(v->ctl_done);
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
        case VTIMSG_STATE:
            v->connected = h.len >= 4 && *(uint32_t *)buf;
            vti_log("vti: broker reports device %s\n", v->connected ? "connected" : "disconnected");
            break;
        }
    }
    return 0;
}

vti *vti_open(vti_rx_cb cb, void *user)
{
    HANDLE p = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 20 && p == INVALID_HANDLE_VALUE; i++) { // the broker may be busy accepting another client
        p = CreateFileA(VTI_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (p == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_PIPE_BUSY)
                break;
            WaitNamedPipeA(VTI_PIPE_NAME, 100);
        }
    }
    if (p == INVALID_HANDLE_VALUE) {
        vti_log("vti: vtibroker not running (pipe %s: error %lu)\n", VTI_PIPE_NAME, GetLastError());
        return NULL;
    }
    vti *v = calloc(1, sizeof *v);
    v->pipe = p;
    v->cb = cb;
    v->user = user;
    InitializeCriticalSection(&v->wr);
    InitializeCriticalSection(&v->ctl_cs);
    v->ctl_done = CreateEventA(NULL, FALSE, FALSE, NULL);
    uint32_t ver = VTI_PROTO_VERSION;
    send_msg(v, VTIMSG_HELLO, &ver, 4, NULL, 0);
    v->rd = CreateThread(NULL, 0, reader, v, 0, NULL);
    for (int i = 0; i < 50 && !v->connected; i++) // wait for the STATE message (sent right after connect)
        Sleep(10);
    if (!v->connected) {
        vti_log("vti: broker has no device\n");
        vti_close(v);
        return NULL;
    }
    return v;
}

void vti_close(vti *v)
{
    if (!v)
        return;
    v->stop = 1;
    CancelIoEx(v->pipe, NULL);
    CloseHandle(v->pipe);
    if (v->rd) {
        WaitForSingleObject(v->rd, 2000);
        CloseHandle(v->rd);
    }
    CloseHandle(v->ctl_done);
    DeleteCriticalSection(&v->wr);
    DeleteCriticalSection(&v->ctl_cs);
    free(v);
}

int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len)
{
    for (int off = 0; off < len;) { // keep each message below VTIMSG_MAX
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
