// vtisend.c - send SysEx messages to the Virus TI and print everything received.
// usage: vtisend [-c cable] [-w ms] "F0 00 20 33 01 10 30 01 00 F7" ["..." ...]
//   each message is sent as USB-MIDI on EP 0x06 (default cable 2, like the plugin), then received traffic
//   is printed for -w ms (default 1500) before the next message. Active Sensing keeps running meanwhile.
#include "vticore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double t0;
static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9 - t0;
}

static vti_unpacker up;

static void on_sysex(void *user, uint8_t cable, const uint8_t *m, int len)
{
    printf("%9.4f  rx %s c%u SysEx len %d:", now(), (const char *)user, cable, len);
    for (int i = 0; i < len && i < 24; i++)
        printf(" %02X", m[i]);
    printf(len > 24 ? " ...\n" : "\n");
}

static void on_event(void *user, uint8_t cable, const uint8_t *e, int len)
{
    printf("%9.4f  rx %s c%u event:", now(), (const char *)user, cable);
    for (int i = 0; i < len; i++)
        printf(" %02X", e[i]);
    printf("\n");
}

static void on_rx(void *user, uint8_t ep, const uint8_t *d, int len)
{
    (void)user;
    vti_unpack(&up, d, len, on_sysex, on_event, ep == VTI_EP_MI03_IN ? "ep85" : "ep86");
}

int main(int argc, char **argv)
{
    int cable = 2, wait_ms = 1500;
    t0 = now();
    setvbuf(stdout, NULL, _IONBF, 0); // unbuffered: output survives a hang or kill
    vti *v = vti_open(on_rx, NULL);
    if (!v)
        return 1;
    static const uint8_t fe[4] = {0x2f, 0xfe, 0, 0};
    for (int a = 1; a < argc; a++) {
        if (!strcmp(argv[a], "-c") && a + 1 < argc) {
            cable = atoi(argv[++a]);
            continue;
        }
        if (!strcmp(argv[a], "-w") && a + 1 < argc) {
            wait_ms = atoi(argv[++a]);
            continue;
        }
        uint8_t msg[512], pk[1024];
        int n = 0;
        for (char *p = argv[a]; *p && n < (int)sizeof msg;) {
            char *end;
            long b = strtol(p, &end, 16);
            if (end == p)
                break;
            msg[n++] = (uint8_t)b;
            p = end;
        }
        int k = vti_pack_sysex((uint8_t)cable, msg, n, pk, sizeof pk);
        printf("%9.4f  tx c%d:", now(), cable);
        for (int i = 0; i < n; i++)
            printf(" %02X", msg[i]);
        printf("\n");
        vti_write(v, VTI_EP_MI04_OUT, pk, k);
        for (int t = 0; t < wait_ms; t += 300) {
            usleep(300000);
            vti_write(v, VTI_EP_MI04_OUT, fe, 4);
        }
    }
    vti_close(v);
    return 0;
}
