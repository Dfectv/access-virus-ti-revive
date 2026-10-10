// vtiprobe.c - hardware test of vticore against a real Virus TI (Linux host or Windows/WinUSB).
// 1. open + handshake, print the reply on EP 0x85
// 2. send Active Sensing keepalive (cable 2) like the plugin (E025)
// 3. request RAM bank 1 program 0 (F0 00 20 33 01 10 30 01 00 F7, cable 2 on EP 0x06) and time the
//    524-byte Single Dump reply on EP 0x86 (E031/E032: ~1 s per dump in the VM)
// Read-only for the synth: only a dump request, no parameter or dump is sent to it.
#include "vticore.h"
#include <stdio.h>
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
static volatile int dump_len, rx_packets, counting;
static double first_rx, last_rx, max_gap, dump_t;

static void on_sysex(void *user, uint8_t cable, const uint8_t *m, int len)
{
    (void)user;
    printf("%9.4f  SysEx c%u len %d:", now(), cable, len);
    for (int i = 0; i < len && i < 16; i++)
        printf(" %02X", m[i]);
    printf(len > 16 ? " ...\n" : "\n");
    if (len > 8 && m[6] == 0x10) {
        dump_len = len;
        dump_t = now();
    }
}

static void on_event(void *user, uint8_t cable, const uint8_t *e, int len)
{
    (void)user;
    printf("%9.4f  event c%u:", now(), cable);
    for (int i = 0; i < len; i++)
        printf(" %02X", e[i]);
    printf("\n");
}

static void on_rx(void *user, uint8_t ep, const uint8_t *d, int len)
{
    (void)user;
    (void)ep;
    if (counting && !dump_len) { // IN transfers (both endpoints) between request and complete dump
        double t = now();
        if (!rx_packets++)
            first_rx = t;
        else if (t - last_rx > max_gap)
            max_gap = t - last_rx;
        last_rx = t;
    }
    vti_unpack(&up, d, len, on_sysex, on_event, NULL);
}

int main(void)
{
    t0 = 0;
    t0 = now();
    setvbuf(stdout, NULL, _IONBF, 0); // unbuffered: output survives a hang or kill
    vti *v = vti_open(on_rx, NULL);
    if (!v)
        return 1;
    printf("%9.4f  opened, handshake sent\n", now());
    static const uint8_t fe[4] = {0x2f, 0xfe, 0, 0};
    for (int i = 0; i < 4; i++) { // ~1.2 s of keepalive; replies to the handshake arrive meanwhile
        vti_write(v, VTI_EP_MI04_OUT, fe, 4);
        usleep(300000);
    }
    static const uint8_t req[] = {0xf0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x01, 0x00, 0xf7};
    uint8_t pk[64];
    int n = vti_pack_sysex(2, req, sizeof req, pk, sizeof pk);
    rx_packets = 0;
    counting = 1;
    double sent = now();
    printf("%9.4f  sending Single Dump Request bank 1 prog 0 (%d bytes USB-MIDI)\n", sent, n);
    vti_write(v, VTI_EP_MI04_OUT, pk, n);
    for (int i = 0; i < 20 && !dump_len; i++) { // up to 6 s, keepalive continues
        usleep(300000);
        vti_write(v, VTI_EP_MI04_OUT, fe, 4);
    }
    if (dump_len)
        printf("RESULT dump %d bytes: first IN after %.1f ms, complete after %.1f ms, %d IN transfers, "
               "mean gap %.2f ms, max gap %.2f ms\n",
               dump_len, (first_rx - sent) * 1e3, (dump_t - sent) * 1e3, rx_packets,
               rx_packets > 1 ? (last_rx - first_rx) * 1e3 / (rx_packets - 1) : 0, max_gap * 1e3);
    else
        printf("RESULT no dump received (%d IN transfers)\n", rx_packets);
    vti_close(v);
    return dump_len == 524 ? 0 : 1;
}
