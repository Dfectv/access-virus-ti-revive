// vtipeek.c - read the AudioSM shared buffer (spec §11.3) of a running plugin from outside:
// vtipeek.exe <pid> <buffer-address-hex> [count]
// Prints the write position (+0x81028), read position (+0x810BC) and the 8 frames behind the write position,
// as hex and as signed 32-bit / float, `count` times every 250 ms. Read-only (ReadProcessMemory).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: vtipeek <pid> <buffer-address-hex> [count]\n");
        return 2;
    }
    DWORD pid = strtoul(argv[1], NULL, 10);
    uintptr_t base = (uintptr_t)strtoull(argv[2], NULL, 16);
    int count = argc > 3 ? atoi(argv[3]) : 40;
    HANDLE p = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!p) {
        fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError());
        return 1;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    if (count < 0) { // scan mode: whole ring, -count times, report non-zero samples per channel
        static int32_t ring[0x20000];
        for (int i = 0; i < -count; i++) {
            uint32_t hdr[2] = {0}, wr = 0;
            SIZE_T got;
            ReadProcessMemory(p, (void *)(base + 0x80000), hdr, sizeof hdr, &got);
            ReadProcessMemory(p, (void *)(base + 0x81028), &wr, 4, &got);
            ReadProcessMemory(p, (void *)base, ring, sizeof ring, &got);
            uint32_t nch = hdr[0] ? hdr[0] : 2, nz = 0, first = 0xffffffff, last = 0;
            int32_t mn[32] = {0}, mx[32] = {0};
            uint32_t lowbyte_nz = 0;
            for (uint32_t k = 0; k < 0x20000; k++)
                if (ring[k]) {
                    nz++;
                    if (first == 0xffffffff)
                        first = k;
                    last = k;
                    uint32_t c = k % nch;
                    if (ring[k] < mn[c]) mn[c] = ring[k];
                    if (ring[k] > mx[c]) mx[c] = ring[k];
                    if (ring[k] & 0xff)
                        lowbyte_nz++;
                }
            printf("scan wr=%u (frame %u): nonzero=%u first=%u last=%u lowbyte_nonzero=%u", wr, wr % (0x20000 / nch), nz,
                   first, last, lowbyte_nz);
            for (uint32_t c = 0; c < nch && c < 4; c++)
                printf(" ch%u[%08x..%08x]", c, (uint32_t)mn[c], (uint32_t)mx[c]);
            printf("\n");
            if (nz) {
                printf("   at first:");
                for (uint32_t k = first; k < first + 8 && k < 0x20000; k++)
                    printf(" %08x", (uint32_t)ring[k]);
                printf("\n");
            }
            Sleep(1000);
        }
        return 0;
    }
    for (int i = 0; i < count; i++) {
        uint32_t hdr[4] = {0}, wr = 0, rd = 0;
        SIZE_T got;
        ReadProcessMemory(p, (void *)(base + 0x80000), hdr, sizeof hdr, &got);
        ReadProcessMemory(p, (void *)(base + 0x81028), &wr, 4, &got);
        ReadProcessMemory(p, (void *)(base + 0x810bc), &rd, 4, &got);
        uint32_t nch = hdr[0], maxf = hdr[1];
        if (!nch || nch > 32 || !maxf) {
            printf("bad header nch=%u maxf=%u\n", nch, maxf);
            return 1;
        }
        int32_t s[8 * 32];
        uint32_t f = (wr + maxf - 8) % maxf;
        ReadProcessMemory(p, (void *)(base + (uintptr_t)f * nch * 4), s, 8 * nch * 4, &got);
        printf("wr=%u rd=%u frame %u:", wr, rd, f);
        for (uint32_t k = 0; k < 8 * nch; k++)
            printf(" %08x", (uint32_t)s[k]);
        printf("\n   as int32 >>8:");
        for (uint32_t k = 0; k < 8 * nch; k++)
            printf(" %d", s[k] >> 8);
        printf("\n   as float:");
        for (uint32_t k = 0; k < 8 * nch; k++) {
            float fl;
            memcpy(&fl, &s[k], 4);
            printf(" %.4f", fl);
        }
        printf("\n");
        Sleep(250);
    }
    return 0;
}
