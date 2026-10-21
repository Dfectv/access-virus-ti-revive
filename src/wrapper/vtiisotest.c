// vtiisotest.c - stand-alone test of the WinUSB audio streaming (vtiusbaudio.c, findings E075).
// vtiisotest [seconds]: streams IN + mirrored OUT, prints frames/s against the PC clock and the input level.
#include "../broker/vtiusbaudio.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static volatile LONG running = 1;
static volatile LONG64 frames;
static volatile LONG peak;

static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void on_in(const int16_t *pcm, uint32_t n, void *user)
{
    (void)user;
    for (uint32_t i = 0; i < n * 6; i++) {
        int v = abs(pcm[i]);
        if (v > peak)
            peak = v;
    }
    InterlockedAdd64(&frames, n);
}

static DWORD WINAPI worker(LPVOID p)
{
    (void)p;
    return (DWORD)vua_run(&running, on_in, NULL, logf_);
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 10;
    LARGE_INTEGER f, t0, t;
    QueryPerformanceFrequency(&f);
    HANDLE th = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (WaitForSingleObject(th, 500) == WAIT_OBJECT_0) {
        DWORD rc;
        GetExitCodeThread(th, &rc);
        printf("vua_run returned %d at once (no WinUSB audio function?)\n", (int)rc);
        return 1;
    }
    QueryPerformanceCounter(&t0);
    LONG64 f0 = frames;
    for (int s = 1; s <= secs; s++) {
        Sleep(1000);
        QueryPerformanceCounter(&t);
        double el = (double)(t.QuadPart - t0.QuadPart) / f.QuadPart;
        printf("%3d s: %lld frames, %.2f frames/s, peak %ld\n", s, (long long)(frames - f0), (frames - f0) / el,
               peak);
        peak = 0;
        fflush(stdout);
    }
    running = 0;
    WaitForSingleObject(th, 3000);
    return 0;
}
