// vtiemu.c - user-mode emulation of the VirusUSB.sys interface used by Virus Control.dll, on WinUSB.
//
// The Access plugin finds its driver via SetupDi(GUID {8002DBFF-...}) + CreateFileA + DeviceIoControl
// (findings E017, E034-E036). This DLL patches those imports in the Access modules:
//   - SetupDi calls for the Access GUID are redirected to our WinUSB MI_04 interface GUID (src/inf),
//   - CreateFile on that interface returns a virtual handle (an unnamed event object),
//   - DeviceIoControl on virtual handles is emulated (spec.md §11.1, §11.2): raw pipe (open/write/RX ring),
//     vendor control requests (recipient interface -> device, E040) and replayed static answers (E036).
// Everything else (e.g. the USB hub IOCTLs 0x220408/0x220410, E036) goes to the real Windows APIs.
// Log: %TEMP%\vtiemu-<pid>.log
#include "../broker/vticore.h"
#include "replay_table.h"
#include <windows.h>
#include <setupapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const GUID GUID_ACCESS = {0x8002DBFF, 0xC173, 0x4BFF, {0x80, 0xBE, 0x68, 0x15, 0xD7, 0xE1, 0x30, 0x71}};
static const GUID GUID_MI04 = {0x5B45FAF8, 0x316B, 0x4B94, {0xAA, 0xBF, 0xFF, 0x44, 0x2A, 0x85, 0xC5, 0xE7}};

// ---------------------------------------------------------------------------------------------- logging
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_cs;
static LARGE_INTEGER g_freq, g_t0;

static void vlog(const char *fmt, ...)
{
    char buf[1024];
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    int n = snprintf(buf, sizeof buf, "%10.6f t%05lu ", (double)(now.QuadPart - g_t0.QuadPart) / g_freq.QuadPart,
                     GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2)
        n = sizeof buf - 2;
    buf[n++] = '\n';
    DWORD w;
    EnterCriticalSection(&g_cs);
    WriteFile(g_log, buf, n, &w, NULL);
    LeaveCriticalSection(&g_cs);
}

// ---------------------------------------------------------------------------------------------- device
// RX ring shared with the plugin (spec §11.2, E041): struct ring + HANDLE event at +0x8010.
typedef struct {
    volatile uint32_t wr, rd, used, size;
    uint8_t data[];
} ring;

static vti *g_dev;
static int g_dev_refs;
static ring *volatile g_ring;
static HANDLE g_ring_event;
static CRITICAL_SECTION g_dev_cs;

// Port of FUN_f104a1a0 (ring write of n bytes at wr, no wrap inside).
static void ring_put(ring *r, const uint8_t *p, uint32_t n)
{
    memcpy(r->data + r->wr, p, n);
    r->wr += n;
    if (r->used == 0 || r->rd <= r->wr - n || r->wr <= r->rd) {
        r->used += n;
        if (r->used > r->size) { // overflow: drop the oldest data
            r->rd = r->wr;
            r->used = r->size;
        }
    } else {
        r->rd = r->wr;
        r->used = r->size;
    }
}

// Port of FUN_f104a590 + the RX completion FUN_f1009c20.
static void ring_write(ring *r, const uint8_t *p, uint32_t n)
{
    n &= ~3u;
    if (!n || r->size - r->used < n)
        return;
    uint32_t first = r->size - r->wr;
    if (n < first) {
        ring_put(r, p, n);
    } else {
        ring_put(r, p, first);
        r->wr = 0;
        if (r->rd == r->size)
            r->rd = 0;
        ring_put(r, p + first, n - first);
    }
}

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
    if (ep != VTI_EP_MI04_IN) // MI_03 (cable 1) belongs to the Windows MIDI ports, not the plugin pipe
        return;
    ring *r = g_ring;
    if (r) {
        ring_write(r, data, (uint32_t)len);
        SetEvent(g_ring_event);
    }
}

static int dev_acquire(void)
{
    EnterCriticalSection(&g_dev_cs);
    if (!g_dev) {
        g_dev = vti_open(on_rx, NULL);
        vlog("WinUSB open -> %p", (void *)g_dev);
    }
    int ok = g_dev != NULL;
    if (ok)
        g_dev_refs++;
    LeaveCriticalSection(&g_dev_cs);
    return ok;
}

static void dev_release(void)
{
    EnterCriticalSection(&g_dev_cs);
    if (g_dev && --g_dev_refs == 0) {
        g_ring = NULL;
        vti_close(g_dev);
        g_dev = NULL;
        vlog("WinUSB closed");
    }
    LeaveCriticalSection(&g_dev_cs);
}

// ---------------------------------------------------------------------------------------------- virtual handles
static HANDLE g_vh[32];

static int is_virtual(HANDLE h)
{
    for (int i = 0; i < 32; i++)
        if (h && g_vh[i] == h)
            return 1;
    return 0;
}

static HANDLE virtual_open(const char *path)
{
    if (!dev_acquire()) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE h = CreateEventA(NULL, FALSE, FALSE, NULL); // any real kernel handle the plugin can close
    for (int i = 0; i < 32; i++)
        if (!g_vh[i]) {
            g_vh[i] = h;
            break;
        }
    vlog("virtual open \"%s\" -> %p", path, h);
    return h;
}

// ---------------------------------------------------------------------------------------------- IOCTLs
static BOOL reply(void *out, DWORD outsz, LPDWORD ret, const void *data, DWORD len, DWORD info)
{
    if (out && outsz) {
        memset(out, 0, outsz);
        memcpy(out, data, len < outsz ? len : outsz);
    }
    *ret = info <= outsz ? info : outsz;
    return TRUE;
}

static BOOL replay(DWORD code, void *in, void *out, DWORD outsz, LPDWORD ret)
{
    int32_t sel = -1;
    if (code == 0x2200d0 || code == 0x2200d4) // property get/set: first DWORD selects the property
        sel = (int32_t) * (uint32_t *)(in ? in : out);
    for (size_t i = 0; i < sizeof REPLAY / sizeof *REPLAY; i++)
        if (REPLAY[i].code == code && REPLAY[i].sel == sel)
            return reply(out, outsz, ret, REPLAY[i].data, REPLAY[i].len, REPLAY[i].ret);
    if (sel >= 0) { // unknown property: echo the request with value 0, as the driver does for unset ones
        uint32_t echo[2] = {(uint32_t)sel, 0};
        vlog("  property %06lx sel 0x%x not recorded: echo", code, sel);
        return reply(out, outsz, ret, echo, sizeof echo, outsz);
    }
    vlog("  %06lx not recorded: zeros", code);
    return reply(out, outsz, ret, NULL, 0, outsz);
}

// 0x220008 PGIOCTL_STD_VENDOR_REQUEST: 8-byte setup (bmRequestType, bRequest, wValue, wIndex, wLength) + data.
static BOOL vendor_request(void *in, DWORD insz, void *out, DWORD outsz, LPDWORD ret)
{
    if (insz < 8) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint8_t *s = in, buf[4096];
    uint8_t rt = s[0], req = s[1];
    uint16_t wv = s[2] | s[3] << 8, wi = s[4] | s[5] << 8, wl = s[6] | s[7] << 8;
    uint16_t n = (uint16_t)(insz - 8) < wl ? (uint16_t)(insz - 8) : wl;
    if (n > sizeof buf)
        n = sizeof buf;
    // WinUSB rewrites wIndex of interface-recipient requests; the device accepts device recipient (E040).
    uint8_t rt_dev = (rt & ~0x1f) | 0x00;
    if (!(rt & 0x80))
        memcpy(buf, s + 8, n);
    int r = vti_control(g_dev, rt_dev, req, wv, wi, buf, n);
    if (rt & 0x80) {
        memcpy(out, s, 8 < outsz ? 8 : outsz);
        if (r > 0 && outsz > 8)
            memcpy((uint8_t *)out + 8, buf, (DWORD)r < outsz - 8 ? (DWORD)r : outsz - 8);
        *ret = 8 + (r > 0 ? r : 0); // a stalled request returns just the header, like the driver (E036)
    } else {
        *ret = 0;
    }
    return TRUE;
}

static BOOL emulate(HANDLE h, DWORD code, void *in, DWORD insz, void *out, DWORD outsz, LPDWORD ret)
{
    (void)h;
    switch (code) {
    case 0x220008:
        return vendor_request(in, insz, out, outsz, ret);
    case 0x22007c: // wMaxPacketSize of the IN / OUT pipe (EP 0x86 / 0x06)
    case 0x220080: {
        uint32_t mps = 0x20;
        return reply(out, outsz, ret, &mps, 4, 4);
    }
    case 0x220088: // raw pipe OPEN: out = 0x8020-byte RX ring + event handle at +0x8010
        if (!out || outsz != 0x8020) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        {
            ring *r = out;
            r->wr = r->rd = r->used = 0;
            r->size = 0x8000;
            memset(r->data, 0, 0x8000);
            g_ring_event = *(HANDLE *)((uint8_t *)out + 0x8010);
            *(uint32_t *)((uint8_t *)out + 0x8018) = 0;
            g_ring = r;
            vlog("  pipe open ring=%p event=%p", out, g_ring_event);
        }
        *ret = 0;
        return TRUE;
    case 0x22008c: // flush RX ring
        if (g_ring) {
            g_ring->wr = g_ring->rd = g_ring->used = 0;
        }
        *ret = 0;
        return TRUE;
    case 0x220090: // raw pipe CLOSE
        if (g_ring)
            *(uint32_t *)((uint8_t *)g_ring + 0x8018) = 1;
        g_ring = NULL;
        vlog("  pipe close");
        *ret = 0;
        return TRUE;
    case 0x220084: // raw pipe WRITE: USB-MIDI packets in the output buffer (Irp->UserBuffer)
        *ret = 0;
        if (out && outsz && vti_write(g_dev, VTI_EP_MI04_OUT, out, (int)outsz)) {
            SetLastError(ERROR_GEN_FAILURE);
            return FALSE;
        }
        return TRUE;
    case 0x2200a8: { // TX ring {used, free}: writes are synchronous, so always empty
        uint32_t st[2] = {0, 0x8000};
        return reply(out, outsz, ret, st, sizeof st, 8);
    }
    case 0x2200ac: // flush TX ring
    case 0x2200b0: // per-client ASIO parameters / APC registration: accepted, audio is not emulated yet
    case 0x2200e4:
    case 0x2200e8:
    case 0x220030:
    case 0x220024:
    case 0x220064:
    case 0x2200ec:
    case 0x2200f0:
        *ret = 0;
        return TRUE;
    default:
        return replay(code, in, out, outsz, ret);
    }
}

// ---------------------------------------------------------------------------------------------- hooks
static HDEVINFO(WINAPI *real_SetupDiGetClassDevsA)(const GUID *, PCSTR, HWND, DWORD);
static BOOL(WINAPI *real_SetupDiEnumDeviceInterfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD,
                                                      PSP_DEVICE_INTERFACE_DATA);
static HANDLE(WINAPI *real_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE(WINAPI *real_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL(WINAPI *real_DeviceIoControl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL(WINAPI *real_CloseHandle)(HANDLE);
static HMODULE(WINAPI *real_LoadLibraryA)(LPCSTR);
static HMODULE(WINAPI *real_LoadLibraryW)(LPCWSTR);
static HMODULE(WINAPI *real_LoadLibraryExA)(LPCSTR, HANDLE, DWORD);
static HMODULE(WINAPI *real_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);

static const GUID *map_guid(const GUID *g)
{
    return g && IsEqualGUID(g, &GUID_ACCESS) ? &GUID_MI04 : g;
}

static HDEVINFO WINAPI hook_SetupDiGetClassDevsA(const GUID *g, PCSTR en, HWND w, DWORD fl)
{
    if (!real_SetupDiGetClassDevsA)
        real_SetupDiGetClassDevsA = (void *)GetProcAddress(GetModuleHandleA("setupapi.dll"), "SetupDiGetClassDevsA");
    if (g && IsEqualGUID(g, &GUID_ACCESS))
        vlog("SetupDiGetClassDevsA Access GUID -> WinUSB MI_04 GUID");
    return real_SetupDiGetClassDevsA(map_guid(g), en, w, fl);
}

static BOOL WINAPI hook_SetupDiEnumDeviceInterfaces(HDEVINFO di, PSP_DEVINFO_DATA dd, const GUID *g, DWORD i,
                                                    PSP_DEVICE_INTERFACE_DATA id)
{
    if (!real_SetupDiEnumDeviceInterfaces)
        real_SetupDiEnumDeviceInterfaces =
            (void *)GetProcAddress(GetModuleHandleA("setupapi.dll"), "SetupDiEnumDeviceInterfaces");
    return real_SetupDiEnumDeviceInterfaces(di, dd, map_guid(g), i, id);
}

static HANDLE WINAPI hook_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                     DWORD fl, HANDLE tmpl)
{
    if (name && StrStrIA(name, "{5b45faf8-316b-4b94-aabf-ff442a85c5e7}"))
        return virtual_open(name);
    return real_CreateFileA(name, acc, share, sa, disp, fl, tmpl);
}

static HANDLE WINAPI hook_CreateFileW(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                     DWORD fl, HANDLE tmpl)
{
    if (name && StrStrIW(name, L"{5b45faf8-316b-4b94-aabf-ff442a85c5e7}")) {
        char a[MAX_PATH * 2];
        WideCharToMultiByte(CP_ACP, 0, name, -1, a, sizeof a, NULL, NULL);
        return virtual_open(a);
    }
    return real_CreateFileW(name, acc, share, sa, disp, fl, tmpl);
}

static BOOL WINAPI hook_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD insz, LPVOID out, DWORD outsz,
                                        LPDWORD ret, LPOVERLAPPED ov)
{
    if (!is_virtual(h))
        return real_DeviceIoControl(h, code, in, insz, out, outsz, ret, ov);
    DWORD got = 0;
    if (!ret)
        ret = &got;
    BOOL ok = emulate(h, code, in, insz, out, outsz, ret);
    if (code != 0x220084 && code != 0x2200a8) // the two high-rate codes are not logged
        vlog("IOCTL %06lx in=%lu out=%lu -> ok=%d ret=%lu", code, insz, outsz, ok, *ret);
    if (ov && ok) { // the plugin calls synchronously (E036); complete overlapped callers immediately
        ov->Internal = 0;
        ov->InternalHigh = *ret;
        if (ov->hEvent)
            SetEvent(ov->hEvent);
    }
    return ok;
}

static BOOL WINAPI hook_CloseHandle(HANDLE h)
{
    if (h && is_virtual(h)) {
        for (int i = 0; i < 32; i++)
            if (g_vh[i] == h)
                g_vh[i] = NULL;
        vlog("virtual close %p", h);
        dev_release();
    }
    return real_CloseHandle(h);
}

static void patch_module(HMODULE m, int all);

static void after_load(HMODULE m)
{
    if (!m)
        return;
    char path[MAX_PATH];
    GetModuleFileNameA(m, path, sizeof path);
    if (StrStrIA(path, "Virus Control") || StrStrIA(path, "Virus TI")) {
        vlog("loaded %s", path);
        patch_module(m, 1);
    }
}

static HMODULE WINAPI hook_LoadLibraryA(LPCSTR n)
{
    HMODULE m = real_LoadLibraryA(n);
    after_load(m);
    return m;
}
static HMODULE WINAPI hook_LoadLibraryW(LPCWSTR n)
{
    HMODULE m = real_LoadLibraryW(n);
    after_load(m);
    return m;
}
static HMODULE WINAPI hook_LoadLibraryExA(LPCSTR n, HANDLE f, DWORD fl)
{
    HMODULE m = real_LoadLibraryExA(n, f, fl);
    after_load(m);
    return m;
}
static HMODULE WINAPI hook_LoadLibraryExW(LPCWSTR n, HANDLE f, DWORD fl)
{
    HMODULE m = real_LoadLibraryExW(n, f, fl);
    after_load(m);
    return m;
}

static const struct {
    const char *dll, *fn;
    void *hook;
    int loader; // 1 = also patched in non-Access modules (to see Access modules being loaded)
} HOOKS[] = {
    {"setupapi.dll", "SetupDiGetClassDevsA", hook_SetupDiGetClassDevsA, 0},
    {"setupapi.dll", "SetupDiEnumDeviceInterfaces", hook_SetupDiEnumDeviceInterfaces, 0},
    {"kernel32.dll", "CreateFileA", hook_CreateFileA, 0},
    {"kernel32.dll", "CreateFileW", hook_CreateFileW, 0},
    {"kernel32.dll", "DeviceIoControl", hook_DeviceIoControl, 0},
    {"kernel32.dll", "CloseHandle", hook_CloseHandle, 0},
    {"kernel32.dll", "LoadLibraryA", hook_LoadLibraryA, 1},
    {"kernel32.dll", "LoadLibraryW", hook_LoadLibraryW, 1},
    {"kernel32.dll", "LoadLibraryExA", hook_LoadLibraryExA, 1},
    {"kernel32.dll", "LoadLibraryExW", hook_LoadLibraryExW, 1},
};

static int dll_matches(const char *imp, const char *want)
{
    if (!_stricmp(imp, want))
        return 1;
    // kernel32 functions are often imported through API sets (api-ms-win-core-*.dll)
    return !_stricmp(want, "kernel32.dll") && !_strnicmp(imp, "api-ms-win-core-", 16);
}

// Replace the IAT slots of module m that import one of HOOKS by name (all = 0: only the loader hooks).
static void patch_module(HMODULE m, int all)
{
    BYTE *base = (BYTE *)m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return;
    int patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dir.VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(base + d->Name);
        if (!d->OriginalFirstThunk)
            continue;
        IMAGE_THUNK_DATA *names = (void *)(base + d->OriginalFirstThunk), *slots = (void *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (size_t i = 0; i < sizeof HOOKS / sizeof *HOOKS; i++) {
                if ((!all && !HOOKS[i].loader) || !dll_matches(dll, HOOKS[i].dll) || strcmp(fn, HOOKS[i].fn) ||
                    slots->u1.Function == (ULONG_PTR)HOOKS[i].hook)
                    continue;
                DWORD old;
                VirtualProtect(&slots->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &old);
                slots->u1.Function = (ULONG_PTR)HOOKS[i].hook;
                VirtualProtect(&slots->u1.Function, sizeof(ULONG_PTR), old, &old);
                patched++;
            }
        }
    }
    if (patched && all) {
        char path[MAX_PATH];
        GetModuleFileNameA(m, path, sizeof path);
        vlog("patched %d imports in %s", patched, path);
    }
}

// Patch the loader imports of every module already in the process (so the host's load of Virus TI.vst3 is
// seen), and fully patch Access modules that are already loaded.
static void patch_process(HMODULE self)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return;
    MODULEENTRY32W me = {.dwSize = sizeof me};
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        if (me.hModule == self)
            continue;
        int access = StrStrIW(me.szModule, L"Virus TI") || StrStrIW(me.szModule, L"Virus Control");
        patch_module(me.hModule, access);
    }
    CloseHandle(snap);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID unused)
{
    (void)unused;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        InitializeCriticalSection(&g_dev_cs);
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_t0);
        char path[MAX_PATH + 32], tmp[MAX_PATH];
        GetTempPathA(sizeof tmp, tmp);
        snprintf(path, sizeof path, "%svtiemu-%lu.log", tmp, GetCurrentProcessId());
        g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        real_CreateFileA = (void *)GetProcAddress(k32, "CreateFileA");
        real_CreateFileW = (void *)GetProcAddress(k32, "CreateFileW");
        real_DeviceIoControl = (void *)GetProcAddress(k32, "DeviceIoControl");
        real_CloseHandle = (void *)GetProcAddress(k32, "CloseHandle");
        real_LoadLibraryA = (void *)GetProcAddress(k32, "LoadLibraryA");
        real_LoadLibraryW = (void *)GetProcAddress(k32, "LoadLibraryW");
        real_LoadLibraryExA = (void *)GetProcAddress(k32, "LoadLibraryExA");
        real_LoadLibraryExW = (void *)GetProcAddress(k32, "LoadLibraryExW");
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, sizeof exe);
        vlog("vtiemu attached to pid %lu %s", GetCurrentProcessId(), exe);
        patch_process(inst);
    }
    return TRUE;
}
