// vticore_pipe.c - client backend of vticore.h that talks to vtibroker.exe over \\.\pipe\vtibroker
// (vtiproto.h) instead of opening WinUSB itself. Used by the wrapper VST3 (vtiemu) so several plugin
// instances and Control Center can share the device (findings/design.md, M3).
// The broker is not started at logon: the first client that finds no pipe starts it, and the broker exits
// by itself when no client has been connected for a while (vtibroker.c, IDLE_EXIT_MS).
#include "vticore.h"
#include "vtiproto.h"
#include "vtipipe_io.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

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
    HANDLE audio_done; // VTIMSG_AUDIO_REPLY
    volatile int32_t audio_result;
};

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
    int r = vtipipe_write(v->pipe, buf, sizeof *h + alen + blen);
    LeaveCriticalSection(&v->wr);
    return r;
}

static DWORD WINAPI reader(LPVOID arg)
{
    vti *v = arg;
    uint8_t buf[VTIMSG_MAX];
    while (!v->stop) {
        vtimsg h;
        if (vtipipe_read(v->pipe, &h, sizeof h) || h.len > VTIMSG_MAX || vtipipe_read(v->pipe, buf, h.len)) {
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
        case VTIMSG_STATE:
            v->connected = h.len >= 4 && *(uint32_t *)buf;
            vti_log("vti: broker reports device %s\n", v->connected ? "connected" : "disconnected");
            break;
        }
    }
    return 0;
}

// vtibroker.exe: $VTI_BROKER, else InstallDir from HKLM\SOFTWARE\TI Revive (written by the MSI), else next to
// this module (vtiemu-pipe.dll / vtilaunch.exe live in the install folder), else %ProgramW6432%\TI Revive.
static int broker_path(wchar_t *path, DWORD cap)
{
    DWORD n = GetEnvironmentVariableW(L"VTI_BROKER", path, cap);
    if (n && n < cap && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return 1;
    wchar_t dir[MAX_PATH];
    DWORD sz = sizeof dir;
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\TI Revive", L"InstallDir", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                      NULL, dir, &sz)) {
        _snwprintf(path, cap, L"%ls%lsvtibroker.exe", dir, dir[0] && dir[wcslen(dir) - 1] == L'\\' ? L"" : L"\\");
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
            return 1;
    }
    HMODULE self = NULL;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void *)broker_path, &self) &&
        GetModuleFileNameW(self, path, cap)) {
        wchar_t *slash = wcsrchr(path, L'\\');
        if (slash && (size_t)(slash + 1 - path) + 14 < cap) {
            wcscpy(slash + 1, L"vtibroker.exe");
            if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
                return 1;
        }
    }
    n = GetEnvironmentVariableW(L"ProgramW6432", dir, MAX_PATH);
    if (n && n < MAX_PATH) {
        _snwprintf(path, cap, L"%ls\\TI Revive\\vtibroker.exe", dir);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
            return 1;
    }
    return 0;
}

// Start the broker detached from the caller (a DAW may run plug-ins in a job object that is killed with it).
static int start_broker(void)
{
    wchar_t path[MAX_PATH + 32];
    if (!broker_path(path, MAX_PATH + 32)) {
        vti_log("vti: vtibroker.exe not found (TI Revive not installed?)\n");
        return 0;
    }
    wchar_t cmd[MAX_PATH + 40];
    _snwprintf(cmd, MAX_PATH + 40, L"\"%ls\"", path);
    STARTUPINFOW si = {.cb = sizeof si};
    PROCESS_INFORMATION pi;
    DWORD flags = DETACHED_PROCESS | CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
    if (!CreateProcessW(path, cmd, NULL, NULL, FALSE, flags | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi) &&
        !CreateProcessW(path, cmd, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)) {
        vti_log("vti: starting vtibroker failed (error %lu)\n", GetLastError());
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    vti_log("vti: started vtibroker on demand\n");
    return 1;
}

vti *vti_open(vti_rx_cb cb, void *user)
{
    HANDLE p = INVALID_HANDLE_VALUE;
    int started = 0;
    for (int i = 0; i < 40 && p == INVALID_HANDLE_VALUE; i++) { // the broker may be busy accepting another client
        p = CreateFileA(VTI_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                        NULL);
        if (p == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e == ERROR_PIPE_BUSY) {
                WaitNamedPipeA(VTI_PIPE_NAME, 100);
            } else if (e == ERROR_FILE_NOT_FOUND && !started) {
                if (!(started = start_broker()))
                    break;
                Sleep(100);
            } else if (e == ERROR_FILE_NOT_FOUND) {
                Sleep(100); // broker starting up: up to 4 s
            } else {
                break;
            }
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
    v->audio_done = CreateEventA(NULL, FALSE, FALSE, NULL);
    uint32_t ver = VTI_PROTO_VERSION;
    send_msg(v, VTIMSG_HELLO, &ver, 4, NULL, 0);
    v->rd = CreateThread(NULL, 0, reader, v, 0, NULL);
    // wait for STATE "connected" (sent right after connect; a freshly started broker opens the device first)
    for (int i = 0; i < (started ? 500 : 50) && !v->connected; i++)
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
    CloseHandle(v->audio_done);
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

int vti_audio(vti *v, int on, const char *event_name)
{
    uint32_t o = on ? 1 : 0;
    int n = on && event_name ? (int)strlen(event_name) + 1 : 0;
    EnterCriticalSection(&v->ctl_cs);
    v->audio_result = -1;
    ResetEvent(v->audio_done);
    int r = -1;
    // starting the stream takes up to ~1 s (alternate settings, sample rate); an old broker never answers
    if (!send_msg(v, VTIMSG_AUDIO, &o, 4, event_name, n) && WaitForSingleObject(v->audio_done, 4000) == WAIT_OBJECT_0)
        r = v->audio_result;
    LeaveCriticalSection(&v->ctl_cs);
    return r;
}

int vti_is_dead(vti *v)
{
    return !v->connected;
}
