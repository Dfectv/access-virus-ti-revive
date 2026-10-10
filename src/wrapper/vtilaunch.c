// vtilaunch.c - start an Access program (e.g. Virus Control Center) with the IOCTL emulation loaded before
// its first instruction runs, so its driver calls go to vtibroker over WinUSB like the wrapper VST3's.
// usage: vtilaunch.exe [-d emu.dll] "C:\...\Virus Control Center.exe" [args]
//   -d: emulation DLL (default: vtiemu-pipe.dll next to vtilaunch.exe)
// Method: CreateProcess suspended, queue an APC with LoadLibraryW(emu.dll) on the main thread, resume.
// The APC runs at the end of process initialisation, after the static imports are bound and before the
// entry point.
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

int wmain(int argc, wchar_t **argv)
{
    wchar_t dll[MAX_PATH];
    int a = 1;
    if (argc > 2 && !wcscmp(argv[1], L"-d")) {
        GetFullPathNameW(argv[2], MAX_PATH, dll, NULL);
        a = 3;
    } else {
        GetModuleFileNameW(NULL, dll, MAX_PATH);
        wchar_t *slash = wcsrchr(dll, L'\\');
        wcscpy(slash ? slash + 1 : dll, L"vtiemu-pipe.dll");
    }
    if (a >= argc) {
        fwprintf(stderr, L"usage: vtilaunch [-d emu.dll] <program.exe> [args]\n");
        return 2;
    }
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"emulation DLL not found: %ls\n", dll);
        return 1;
    }
    // command line: quoted program + remaining args
    wchar_t cmd[4096];
    int n = swprintf(cmd, 4096, L"\"%ls\"", argv[a]);
    for (int i = a + 1; i < argc && n < 4000; i++)
        n += swprintf(cmd + n, 4096 - n, L" \"%ls\"", argv[i]);
    wchar_t dir[MAX_PATH];
    wcsncpy(dir, argv[a], MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = 0;
    STARTUPINFOW si = {.cb = sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(argv[a], cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, slash ? dir : NULL, &si, &pi)) {
        fwprintf(stderr, L"CreateProcess %ls failed: %lu\n", argv[a], GetLastError());
        return 1;
    }
    SIZE_T sz = (wcslen(dll) + 1) * sizeof(wchar_t);
    void *mem = VirtualAllocEx(pi.hProcess, NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    void *ll = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!mem || !WriteProcessMemory(pi.hProcess, mem, dll, sz, NULL) ||
        !QueueUserAPC((PAPCFUNC)ll, pi.hThread, (ULONG_PTR)mem)) {
        fwprintf(stderr, L"injection failed: %lu\n", GetLastError());
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }
    ResumeThread(pi.hThread);
    wprintf(L"started %ls (pid %lu) with %ls\n", argv[a], pi.dwProcessId, dll);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
