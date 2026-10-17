// flashinfo.h - the broker's only flash access: the Virus' identification blocks (model, OS version), read exactly
// like the original Access driver does at plug-in start (E026) and like vtiflash already did on the user's TI2 through
// WinUSB (E040): per 16-byte block an address request (OUT 0x40, bRequest 1, wIndex 0x12, 3-byte address), status
// polls (IN 0xC0, 0x81, wIndex 1) until 0x10, then the data (IN 0xC0, 0x81, wIndex 0x30, 16 bytes). READ ONLY and a
// fixed list of addresses: nothing else is ever sent, the first error stops it. Clients' own vendor requests stay
// refused (ctrlguard.h, AGENTS.md rule 2). Tests: native/tests/test_flashinfo.cpp.
#pragma once
#include <stdint.h>

#define VTI_FLASH_BLOCKS 12
#define VTI_FLASH_MAX_POLLS 100 // x 10 ms; the Access driver needs 100-250 ms per block (E040)

typedef int (*vti_flash_ctl)(void *u, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint8_t *d, uint16_t len);
typedef void (*vti_flash_nap)(int ms);

// the original driver's reads at plug-in start (E026), in its order
static const uint32_t vti_flash_addr[VTI_FLASH_BLOCKS] = {0xFF7FA0, 0xFF7FB0, 0xFF7FC0, 0xFF7FD0, 0xFF7FE0, 0xFF7FF0,
                                                          0x800000, 0x800010, 0x800020, 0x800030, 0x800040, 0x800050};

// fills out[VTI_FLASH_BLOCKS * 16]; returns the byte count or -1 (nothing more is sent after a failed request)
static inline int vti_flash_info(vti_flash_ctl ctl, vti_flash_nap nap, void *u, uint8_t *out)
{
    for (int b = 0; b < VTI_FLASH_BLOCKS; b++) {
        uint32_t a = vti_flash_addr[b];
        uint8_t ad[3] = {(uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t)a}, st = 0;
        if (ctl(u, 0x40, 1, 0, 0x12, ad, 3) < 0)
            return -1;
        for (int polls = 0;; polls++) {
            if (polls == VTI_FLASH_MAX_POLLS || ctl(u, 0xC0, 0x81, 0, 1, &st, 1) != 1)
                return -1;
            if (st >= 0x10)
                break;
            nap(10);
        }
        if (ctl(u, 0xC0, 0x81, 0, 0x30, out + 16 * b, 16) != 16)
            return -1;
    }
    return VTI_FLASH_BLOCKS * 16;
}
