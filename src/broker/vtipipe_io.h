// vtipipe_io.h - blocking read/write helpers on an OVERLAPPED pipe handle, shared by vtibroker.c and
// vticore_pipe.c. Both sides read on one thread and write from others: on a handle opened without
// FILE_FLAG_OVERLAPPED Windows serializes the I/O, so a pending ReadFile blocks every WriteFile on the same
// handle (first broker test deadlocked, E047). With overlapped handles they run concurrently.
#pragma once
#include <windows.h>

// Read exactly n bytes. Returns 0, or -1 on error/disconnect.
static int vtipipe_read(HANDLE h, void *buf, DWORD n)
{
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD got = 0;
    int rc = 0;
    while (got < n) {
        DWORD r = 0;
        ResetEvent(ov.hEvent);
        if (!ReadFile(h, (BYTE *)buf + got, n - got, NULL, &ov) && GetLastError() != ERROR_IO_PENDING) {
            rc = -1;
            break;
        }
        if (!GetOverlappedResult(h, &ov, &r, TRUE) || !r) {
            rc = -1;
            break;
        }
        got += r;
    }
    CloseHandle(ov.hEvent);
    return rc;
}

// Write all n bytes. Returns 0, or -1 on error/disconnect.
static int vtipipe_write(HANDLE h, const void *buf, DWORD n)
{
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD w = 0;
    int rc = 0;
    if (!WriteFile(h, buf, n, NULL, &ov) && GetLastError() != ERROR_IO_PENDING)
        rc = -1;
    else if (!GetOverlappedResult(h, &ov, &w, TRUE) || w != n)
        rc = -1;
    CloseHandle(ov.hEvent);
    return rc;
}
