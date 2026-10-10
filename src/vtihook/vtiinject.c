// vtiinject.c - load a DLL into a running process: vtiinject.exe <pid|exe-name> <full\path\to\dll>
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static DWORD find_pid(const wchar_t *name)
{
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = {.dwSize = sizeof pe};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (!_wcsicmp(pe.szExeFile, name)) {
            pid = pe.th32ProcessID;
            break;
        }
    CloseHandle(snap);
    return pid;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3) {
        fwprintf(stderr, L"usage: vtiinject <pid|exe-name> <full path to dll>\n");
        return 2;
    }
    DWORD pid = wcstoul(argv[1], NULL, 10);
    if (!pid)
        pid = find_pid(argv[1]);
    if (!pid) {
        fwprintf(stderr, L"process %ls not found\n", argv[1]);
        return 1;
    }
    wchar_t full[MAX_PATH];
    GetFullPathNameW(argv[2], MAX_PATH, full, NULL);
    HANDLE p = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                               PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!p) {
        fwprintf(stderr, L"OpenProcess(%lu) failed: %lu\n", pid, GetLastError());
        return 1;
    }
    SIZE_T sz = (wcslen(full) + 1) * sizeof(wchar_t);
    void *mem = VirtualAllocEx(p, NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    WriteProcessMemory(p, mem, full, sz, NULL);
    void *ll = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE t = CreateRemoteThread(p, NULL, 0, (LPTHREAD_START_ROUTINE)ll, mem, 0, NULL);
    if (!t) {
        fwprintf(stderr, L"CreateRemoteThread failed: %lu\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(t, 10000);
    DWORD rc = 0;
    GetExitCodeThread(t, &rc);
    VirtualFreeEx(p, mem, 0, MEM_RELEASE);
    wprintf(L"injected %ls into pid %lu (LoadLibraryW -> %ls)\n", full, pid, rc ? L"ok" : L"FAILED");
    return rc ? 0 : 1;
}
