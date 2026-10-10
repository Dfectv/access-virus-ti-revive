// vtiflash.c - repeat the plugin's flash read (spec §2.1, E026) with interface- and device-recipient requests,
// to see whether the device accepts them through WinUSB. Read-only: address write + status poll + 16-byte read.
#include "vticore.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

static void on_rx(void *u, uint8_t ep, const uint8_t *d, int len) { (void)u; (void)ep; (void)d; (void)len; }

static void read_block(vti *v, uint8_t out_rt, uint8_t in_rt, uint32_t addr)
{
    uint8_t a[3] = {addr >> 16, addr >> 8, addr}, st = 0, buf[16];
    int w = vti_control(v, out_rt, 1, 0, 0x12, a, 3);
    int polls = 0, s = -1;
    // Status (wIndex 1) is a progress counter 00, 02 .. 10; 0x10 = block ready. The Access driver polls every
    // ~12 ms and needs 100-250 ms per block (E040).
    for (; polls < 100; polls++) {
        s = vti_control(v, in_rt, 0x81, 0, 1, &st, 1);
        if (s != 1 || st >= 0x10)
            break;
        Sleep(10);
    }
    memset(buf, 0, sizeof buf);
    int r = vti_control(v, in_rt, 0x81, 0, 0x30, buf, 16);
    printf("rt %02x/%02x addr %06x: write=%d status=%d (0x%02x after %d polls) read=%d:", out_rt, in_rt, addr, w, s,
           st, polls, r);
    for (int i = 0; i < 16; i++)
        printf(" %02x", buf[i]);
    printf("  |");
    for (int i = 0; i < 16; i++)
        putchar(buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
    printf("|\n");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // unbuffered: output survives a hang or kill
    vti *v = vti_open(on_rx, NULL);
    if (!v)
        return 1;
    // expected (E026): ff7fc0 -> "FLSH200801010000", 800040 -> "1.7.00"
    read_block(v, 0x40, 0xc0, 0xff7fc0); // device recipient
    read_block(v, 0x40, 0xc0, 0x800040);
    read_block(v, 0x40, 0xc0, 0xff7fd0);
    vti_close(v);
    return 0;
}
