// vtihook.c - trace the Virus TI driver interface used by the Access software.
//
// Injected into a host process (DAW or Virus Control Center). Patches the import tables of the
// Access modules so SetupDiGetClassDevsA / CreateFileA/W / DeviceIoControl / CloseHandle calls are
// logged with their buffers to %TEMP%\vtihook-<pid>.log, then forwarded unchanged to the real driver.
// The same IAT mechanism will later redirect these calls to the user-mode broker (findings/design.md).
#include <windows.h>
#include <setupapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#define MAX_DUMP 512 // bytes per buffer dump; larger buffers are truncated (the full size is logged)

static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_cs;
static LARGE_INTEGER g_freq, g_t0;
static HANDLE g_dev[64];
static volatile LONG g_stop;

static HANDLE(WINAPI *real_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE(WINAPI *real_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL(WINAPI *real_DeviceIoControl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL(WINAPI *real_CloseHandle)(HANDLE);
static HMODULE(WINAPI *real_LoadLibraryA)(LPCSTR);
static HMODULE(WINAPI *real_LoadLibraryW)(LPCWSTR);
static HMODULE(WINAPI *real_LoadLibraryExA)(LPCSTR, HANDLE, DWORD);
static HMODULE(WINAPI *real_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);
static HDEVINFO(WINAPI *real_SetupDiGetClassDevsA)(const GUID *, PCSTR, HWND, DWORD);

static void vlog(const char *fmt, ...)
{
    char buf[2048];
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
    EnterCriticalSection(&g_cs);
    DWORD w;
    WriteFile(g_log, buf, n, &w, NULL);
    LeaveCriticalSection(&g_cs);
}

static void dump(const char *tag, const void *p, DWORD n)
{
    if (!p || !n)
        return;
    char line[3 * 32 + 1];
    const unsigned char *b = p;
    DWORD m = n > MAX_DUMP ? MAX_DUMP : n;
    for (DWORD i = 0; i < m; i += 32) {
        int k = 0;
        for (DWORD j = i; j < i + 32 && j < m; j++)
            k += sprintf(line + k, "%02x ", b[j]);
        vlog("  %s+%04lx: %s%s", tag, i, line, (i + 32 >= m && m < n) ? "..." : "");
    }
}

static int is_dev(HANDLE h)
{
    for (int i = 0; i < 64; i++)
        if (g_dev[i] && g_dev[i] == h)
            return 1;
    return 0;
}

static void track(HANDLE h)
{
    for (int i = 0; i < 64; i++)
        if (!g_dev[i]) {
            g_dev[i] = h;
            return;
        }
}

static HDEVINFO WINAPI hook_SetupDiGetClassDevsA(const GUID *g, PCSTR en, HWND w, DWORD fl)
{
    if (!real_SetupDiGetClassDevsA) // resolved lazily: the caller imports setupapi, so it is loaded by now
        real_SetupDiGetClassDevsA = (void *)GetProcAddress(GetModuleHandleA("setupapi.dll"), "SetupDiGetClassDevsA");
    HDEVINFO r = real_SetupDiGetClassDevsA(g, en, w, fl);
    if (g)
        vlog("SetupDiGetClassDevsA {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} flags=%lx -> %p", g->Data1,
             g->Data2, g->Data3, g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5],
             g->Data4[6], g->Data4[7], fl, r);
    return r;
}

static HANDLE WINAPI hook_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                     DWORD fl, HANDLE tmpl)
{
    HANDLE h = real_CreateFileA(name, acc, share, sa, disp, fl, tmpl);
    if (name && (StrStrIA(name, "8002dbff") || StrStrIA(name, "\\\\?\\usb") || StrStrIA(name, "\\\\.\\"))) {
        DWORD e = GetLastError();
        vlog("CreateFileA \"%s\" acc=%lx share=%lx flags=%lx -> %p err=%lu", name, acc, share, fl, h, e);
        if (h != INVALID_HANDLE_VALUE)
            track(h);
        SetLastError(e);
    }
    return h;
}

static HANDLE WINAPI hook_CreateFileW(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                     DWORD fl, HANDLE tmpl)
{
    HANDLE h = real_CreateFileW(name, acc, share, sa, disp, fl, tmpl);
    if (name && (StrStrIW(name, L"8002dbff") || StrStrIW(name, L"\\\\?\\usb") || StrStrIW(name, L"\\\\.\\"))) {
        DWORD e = GetLastError();
        vlog("CreateFileW \"%ls\" acc=%lx share=%lx flags=%lx -> %p err=%lu", name, acc, share, fl, h, e);
        if (h != INVALID_HANDLE_VALUE)
            track(h);
        SetLastError(e);
    }
    return h;
}

// AudioSM monitor (M4): after 0x220030 starts streaming into a user buffer of 0x810D0 bytes, sample its
// header and the samples around the write position every 100 ms for 60 s (layout: findings E051).
static volatile uint8_t *g_asm;

static DWORD WINAPI asm_monitor(LPVOID p)
{
    volatile uint8_t *b = p;
    for (int i = 0; i < 600 && g_asm == p; i++) {
        volatile uint32_t *u = (volatile uint32_t *)b;
        uint32_t nch = u[0x80000 / 4], w8 = u[0x80008 / 4], wc = u[0x8000c / 4], w10 = u[0x80010 / 4];
        vlog("ASM nch=%u maxf=%u 08=%u 0c=%u 10=%u 1028=%u 102c=%u 1030=%u 10b0=%u 10b4=%u 10b8=%u 10bc=%u 10c8=%llx",
             nch, u[0x80004 / 4], w8, wc, w10, u[0x81028 / 4], u[0x8102c / 4], u[0x81030 / 4], u[0x810b0 / 4],
             u[0x810b4 / 4], u[0x810b8 / 4], u[0x810bc / 4], *(volatile unsigned long long *)(b + 0x810c8));
        if (i % 10 == 0) { // every second: the three sub-structs and 4 frames of samples behind each counter
            for (int s = 0; s < 3; s++) {
                char line[200];
                int k = 0;
                for (int j = 0; j < 10; j++)
                    k += sprintf(line + k, "%08x ", u[(0x81038 + s * 0x28) / 4 + j]);
                vlog("ASM sub%d: %s", s, line);
            }
            uint32_t pos[3] = {w8, wc, w10};
            for (int c = 0; c < 3; c++) {
                if (!nch || nch > 32)
                    break;
                char line[400];
                int k = 0;
                uint32_t f = pos[c] % (0x20000 / nch);
                for (int j = 0; j < 4 * (int)nch && j < 16; j++)
                    k += sprintf(line + k, "%08x ", u[((f + 0x20000 / nch - 4) % (0x20000 / nch)) * nch + j]);
                vlog("ASM samples@%s(%u): %s", c == 0 ? "08" : c == 1 ? "0c" : "10", pos[c], line);
            }
        }
        Sleep(100);
    }
    return 0;
}

// Buffer-switch interposer (M4, E053): the plugin registers {routine 0, thread, event} via 0x2200E4 and its
// priority-31 thread consumes each block right after the event, so outside readers never see the data. We
// hand the driver our own event; on each switch our thread snapshots the new frames of the AudioSM ring and
// then signals the plugin's event.
static HANDLE g_bs_plugin_event, g_bs_our_event;

static DWORD WINAPI bs_thread(LPVOID unused)
{
    (void)unused;
    uint32_t last = 0xffffffff, switches = 0, nz_blocks = 0;
    int32_t mn = 0, mx = 0;
    for (;;) {
        if (WaitForSingleObject(g_bs_our_event, 2000) != WAIT_OBJECT_0)
            continue;
        volatile uint8_t *b = g_asm;
        if (b) {
            volatile uint32_t *u = (volatile uint32_t *)b;
            uint32_t nch = u[0x80000 / 4], maxf = u[0x80004 / 4], wr = u[0x81028 / 4];
            if (nch && nch <= 32 && maxf && last != 0xffffffff && wr - last < maxf) {
                int nz = 0;
                for (uint32_t f = last; f != wr; f++)
                    for (uint32_t c = 0; c < nch; c++) {
                        int32_t v = (int32_t)u[(f % maxf) * nch + c];
                        if (v) {
                            nz++;
                            if (v < mn) mn = v;
                            if (v > mx) mx = v;
                        }
                    }
                if (nz)
                    nz_blocks++;
                if (switches % 200 == 0 || (nz && nz_blocks <= 5)) {
                    char line[300];
                    int k = 0;
                    uint32_t f0 = last % maxf;
                    for (uint32_t j = 0; j < 8 * nch && j < 16; j++)
                        k += sprintf(line + k, "%08x ", u[(f0 * nch + j) % (maxf * nch)]);
                    vlog("BS #%u frames %u..%u (%u) nonzero=%d blocks_with_signal=%u range[%08x..%08x] first: %s",
                         switches, last, wr, wr - last, nz, nz_blocks, (uint32_t)mn, (uint32_t)mx, line);
                }
            }
            last = wr;
        }
        switches++;
        if (g_bs_plugin_event)
            SetEvent(g_bs_plugin_event);
    }
    return 0;
}

static BOOL WINAPI hook_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD insz, LPVOID out, DWORD outsz,
                                        LPDWORD ret, LPOVERLAPPED ov)
{
    if (!is_dev(h) && (code >> 16) != 0x22)
        return real_DeviceIoControl(h, code, in, insz, out, outsz, ret, ov);
    DWORD got = 0;
    if (!ret)
        ret = &got;
    vlog("IOCTL %08lx h=%p in=%p/%lu out=%p/%lu ov=%p", code, h, in, insz, out, outsz, ov);
    dump("in ", in, insz);
    // These codes pass their input in the output buffer (the driver reads Irp->UserBuffer), so dump it before the call
    // too. 0x220084 = raw pipe write; 0x220088 is skipped (32 KB ring, only its header matters).
    switch (code) {
    case 0x220084:
        dump("tx ", out, outsz);
        break;
    case 0x220024: case 0x220030: case 0x220064: case 0x2200b0: case 0x2200e4: case 0x2200e8:
        dump("ub ", out, outsz);
        break;
    }
    if (code == 0x2200e4 && out && outsz >= 24) { // {routine, thread, event}: interpose the threaded variant
        uint64_t *r = out;
        if (r[0] == 0 && r[1] && r[2]) {
            if (!g_bs_our_event) {
                g_bs_our_event = CreateEventA(NULL, FALSE, FALSE, NULL);
                CloseHandle(CreateThread(NULL, 0, bs_thread, NULL, 0, NULL));
            }
            g_bs_plugin_event = (HANDLE)(uintptr_t)r[2];
            vlog("  buffer switch interposed: plugin event %p -> ours %p", g_bs_plugin_event, g_bs_our_event);
            r[2] = (uint64_t)(uintptr_t)g_bs_our_event;
        }
    }
    BOOL ok = real_DeviceIoControl(h, code, in, insz, out, outsz, ret, ov);
    DWORD e = GetLastError();
    if (ok && code == 0x220030 && out && outsz >= 16) { // stream start/stop: {u8 start, pad, void *buffer}
        uint8_t start = *(uint8_t *)out;
        void *buf = *(void **)((uint8_t *)out + 8);
        vlog("  stream %s buffer=%p", start ? "START" : "STOP", buf);
        if (start && buf) {
            g_asm = buf;
            CloseHandle(CreateThread(NULL, 0, asm_monitor, buf, 0, NULL));
        } else {
            g_asm = NULL;
        }
    }
    vlog("  -> ok=%d ret=%lu err=%lu", ok, *ret, ok ? 0 : e);
    if (ok && code == 0x220088 && out && outsz >= 0x8020) // raw pipe open: log the ring header only
        dump("hdr", (char *)out + 0x8000, 0x20);
    else if (ok && code != 0x220084)
        dump("out", out, *ret ? *ret : outsz);
    SetLastError(e);
    return ok;
}

static BOOL WINAPI hook_CloseHandle(HANDLE h)
{
    if (h && is_dev(h)) {
        vlog("CloseHandle %p", h);
        for (int i = 0; i < 64; i++)
            if (g_dev[i] == h)
                g_dev[i] = NULL;
    }
    return real_CloseHandle(h);
}

static void patch_module(HMODULE m);

static void after_load(HMODULE m, const char *how)
{
    if (!m)
        return;
    char path[MAX_PATH];
    GetModuleFileNameA(m, path, sizeof path);
    if (StrStrIA(path, "Virus Control") || StrStrIA(path, "Virus TI")) {
        vlog("%s loaded %s", how, path);
        patch_module(m);
    }
}

static HMODULE WINAPI hook_LoadLibraryA(LPCSTR n)
{
    HMODULE m = real_LoadLibraryA(n);
    after_load(m, "LoadLibraryA");
    return m;
}
static HMODULE WINAPI hook_LoadLibraryW(LPCWSTR n)
{
    HMODULE m = real_LoadLibraryW(n);
    after_load(m, "LoadLibraryW");
    return m;
}
static HMODULE WINAPI hook_LoadLibraryExA(LPCSTR n, HANDLE f, DWORD fl)
{
    HMODULE m = real_LoadLibraryExA(n, f, fl);
    after_load(m, "LoadLibraryExA");
    return m;
}
static HMODULE WINAPI hook_LoadLibraryExW(LPCWSTR n, HANDLE f, DWORD fl)
{
    HMODULE m = real_LoadLibraryExW(n, f, fl);
    after_load(m, "LoadLibraryExW");
    return m;
}

static const struct {
    const char *dll, *fn;
    void *hook;
} HOOKS[] = {
    {"setupapi.dll", "SetupDiGetClassDevsA", hook_SetupDiGetClassDevsA},
    {"kernel32.dll", "CreateFileA", hook_CreateFileA},
    {"kernel32.dll", "CreateFileW", hook_CreateFileW},
    {"kernel32.dll", "DeviceIoControl", hook_DeviceIoControl},
    {"kernel32.dll", "CloseHandle", hook_CloseHandle},
    {"kernel32.dll", "LoadLibraryA", hook_LoadLibraryA},
    {"kernel32.dll", "LoadLibraryW", hook_LoadLibraryW},
    {"kernel32.dll", "LoadLibraryExA", hook_LoadLibraryExA},
    {"kernel32.dll", "LoadLibraryExW", hook_LoadLibraryExW},
};

// Replace the IAT slots of module m that import one of HOOKS by name. Idempotent.
static void patch_module(HMODULE m)
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
            continue; // bound-only imports: names unavailable
        IMAGE_THUNK_DATA *names = (void *)(base + d->OriginalFirstThunk), *slots = (void *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (size_t i = 0; i < sizeof HOOKS / sizeof *HOOKS; i++) {
                if (_stricmp(dll, HOOKS[i].dll) || strcmp(fn, HOOKS[i].fn) || slots->u1.Function == (ULONG_PTR)HOOKS[i].hook)
                    continue;
                DWORD old;
                VirtualProtect(&slots->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &old);
                slots->u1.Function = (ULONG_PTR)HOOKS[i].hook;
                VirtualProtect(&slots->u1.Function, sizeof(ULONG_PTR), old, &old);
                patched++;
            }
        }
    }
    if (patched) {
        char path[MAX_PATH];
        GetModuleFileNameA(m, path, sizeof path);
        vlog("patched %d imports in %s", patched, path);
    }
}

// Patch Access modules that are already loaded, and keep watching for ones the host loads later
// (the DAW loads Virus TI.vst3 itself; its LoadLibrary hook then catches Virus Control.dll synchronously).
static DWORD WINAPI watcher(LPVOID unused)
{
    (void)unused;
    while (!g_stop) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me = {.dwSize = sizeof me};
            for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
                if (StrStrIW(me.szModule, L"Virus TI") || StrStrIW(me.szModule, L"Virus Control"))
                    patch_module(me.hModule);
            CloseHandle(snap);
        }
        Sleep(50);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID unused)
{
    (void)unused;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_cs);
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_t0);
        char path[MAX_PATH + 32], tmp[MAX_PATH];
        GetTempPathA(sizeof tmp, tmp);
        snprintf(path, sizeof path, "%svtihook-%lu.log", tmp, GetCurrentProcessId());
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
        vlog("vtihook attached to pid %lu %s", GetCurrentProcessId(), exe);
        CloseHandle(CreateThread(NULL, 0, watcher, NULL, 0, NULL));
    } else if (reason == DLL_PROCESS_DETACH) {
        g_stop = 1;
    }
    return TRUE;
}
