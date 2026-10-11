// vtiaudiotest.c - test client of the broker audio (protocol 2, vtiaudioshm.h): registers like the plug-in,
// follows the shared input ring and prints frames/s against the PC clock, the largest gap between two events and
// the input peak. Several instances at once check the fan-out to more than one client.
// usage: vtiaudiotest [seconds]
#include "vticore.h"
#include "vtiaudioshm.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
    (void)ep;
    (void)data;
    (void)len;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 10;
    vti *v = vti_open(on_rx, NULL);
    if (!v) {
        printf("no broker connection\n");
        return 1;
    }
    char name[64];
    snprintf(name, sizeof name, "Local\\vtiaudiotest_%lu", GetCurrentProcessId());
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, name);
    LARGE_INTEGER f, t0, t, last;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    int r = vti_audio(v, 1, name);
    QueryPerformanceCounter(&t);
    printf("vti_audio -> %d after %.0f ms\n", r, (double)(t.QuadPart - t0.QuadPart) * 1000 / f.QuadPart);
    if (r != 1)
        return 1;
    HANDLE map = OpenFileMappingA(FILE_MAP_READ, FALSE, VTI_AUDIO_SHM_NAME);
    const vti_audio_shm *shm = map ? MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof *shm) : NULL;
    if (!shm || shm->magic != VTI_AUDIO_MAGIC) {
        printf("shared memory missing\n");
        return 1;
    }
    int64_t pos = shm->in_frames, start = pos;
    QueryPerformanceCounter(&t0);
    last = t0;
    double gap_max = 0;
    int peak = 0, events = 0;
    for (int s = 1; s <= secs;) {
        DWORD w = WaitForSingleObject(ev, 500);
        QueryPerformanceCounter(&t);
        if (w == WAIT_OBJECT_0) {
            double gap = (double)(t.QuadPart - last.QuadPart) * 1000 / f.QuadPart;
            if (gap > gap_max)
                gap_max = gap;
            last = t;
            events++;
        }
        int64_t wr = shm->in_frames;
        for (; pos < wr; pos++)
            for (int c = 0; c < VTI_AUDIO_IN_CH; c++) {
                int x = abs(shm->in[(pos & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH + c]);
                if (x > peak)
                    peak = x;
            }
        double el = (double)(t.QuadPart - t0.QuadPart) / f.QuadPart;
        if (el >= s) {
            printf("%3d s: %lld frames, %.1f frames/s, %d events, max gap %.1f ms, peak %d\n", s,
                   (long long)(pos - start), (pos - start) / el, events, gap_max, peak);
            fflush(stdout);
            gap_max = 0;
            peak = 0;
            events = 0;
            s++;
        }
    }
    printf("vti_audio off -> %d\n", vti_audio(v, 0, NULL));
    vti_close(v);
    return 0;
}
