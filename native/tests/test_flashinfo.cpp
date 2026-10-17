// The broker's only flash access (AGENTS.md rule 2): the Virus' identification blocks, read exactly like the original
// Access driver at plug-in start (E026) and like vtiflash already did on the user's TI2 through WinUSB (E040) - the
// OS version for PATCH UTILITY > INFO. A fake device records every request: nothing but the fixed read sequence
// may ever be sent, and any error stops it. Hardware-free.
#include "check.h"
#include <cstring>
#include <vector>
extern "C" {
#include "flashinfo.h"
}

namespace {
struct Req {
    uint8_t rt, req;
    uint16_t val, idx, len;
    std::vector<uint8_t> out; // OUT data
};
struct FakeVirus {
    std::vector<Req> log;
    int fail_at = -1;        // request number that fails
    int busy_polls = 3;      // status 00, 02, 04 ... before 0x10
    int polls_left = 0;
    uint32_t addr = 0;
    static int ctl(void *u, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint8_t *d, uint16_t len)
    {
        auto *f = (FakeVirus *)u;
        Req r{rt, req, val, idx, len, {}};
        if (!(rt & 0x80))
            r.out.assign(d, d + len);
        f->log.push_back(r);
        if ((int)f->log.size() - 1 == f->fail_at)
            return -1;
        if (rt == 0x40 && req == 1 && idx == 0x12 && len == 3) {
            f->addr = (uint32_t)d[0] << 16 | d[1] << 8 | d[2];
            f->polls_left = f->busy_polls;
            return 3;
        }
        if (rt == 0xC0 && req == 0x81 && idx == 1 && len == 1) {
            int step = 2 * (f->busy_polls - f->polls_left);
            d[0] = f->polls_left > 0 ? (uint8_t)(step < 0x0E ? step : 0x0E) : 0x10; // progress 00, 02 .. then 0x10
            f->polls_left--;
            return 1;
        }
        if (rt == 0xC0 && req == 0x81 && idx == 0x30 && len == 16) {
            for (int i = 0; i < 16; i++)
                d[i] = (uint8_t)(f->addr + i); // block contents = its address bytes
            return 16;
        }
        return -1;
    }
    static void nap(int) {}
};
} // namespace

TEST(flashinfo_exact_read_sequence)
{
    FakeVirus f;
    uint8_t out[VTI_FLASH_BLOCKS * 16];
    CHECK(vti_flash_info(FakeVirus::ctl, FakeVirus::nap, &f, out) == VTI_FLASH_BLOCKS * 16);
    // the addresses of the original driver's reads (E026), in that order
    const uint32_t want[VTI_FLASH_BLOCKS] = {0xFF7FA0, 0xFF7FB0, 0xFF7FC0, 0xFF7FD0, 0xFF7FE0, 0xFF7FF0,
                                             0x800000, 0x800010, 0x800020, 0x800030, 0x800040, 0x800050};
    std::vector<uint32_t> addrs;
    bool only_reads = true;
    for (auto &r : f.log) {
        bool set_addr = r.rt == 0x40 && r.req == 1 && r.val == 0 && r.idx == 0x12 && r.len == 3;
        bool status = r.rt == 0xC0 && r.req == 0x81 && r.val == 0 && r.idx == 1 && r.len == 1;
        bool data = r.rt == 0xC0 && r.req == 0x81 && r.val == 0 && r.idx == 0x30 && r.len == 16;
        only_reads = only_reads && (set_addr || status || data);
        if (set_addr)
            addrs.push_back((uint32_t)r.out[0] << 16 | r.out[1] << 8 | r.out[2]);
    }
    CHECK(only_reads);
    CHECK(addrs == std::vector<uint32_t>(want, want + VTI_FLASH_BLOCKS));
    CHECK(f.log.size() == VTI_FLASH_BLOCKS * (1 + 4 + 1)); // address, 3 busy + 1 ready poll, data
    CHECK(out[0] == 0xA0 && out[16 * 6] == 0x00 && out[16 * 6 + 2] == 0x02); // blocks in order
}

TEST(flashinfo_stops_on_error_and_poll_limit)
{
    FakeVirus f;
    f.fail_at = 7; // second block's address request
    uint8_t out[VTI_FLASH_BLOCKS * 16];
    CHECK(vti_flash_info(FakeVirus::ctl, FakeVirus::nap, &f, out) < 0);
    CHECK(f.log.size() == 8); // nothing after the failed request
    FakeVirus slow;
    slow.busy_polls = 1000; // never ready: give up after the poll limit, no data read
    CHECK(vti_flash_info(FakeVirus::ctl, FakeVirus::nap, &slow, out) < 0);
    CHECK(slow.log.size() == 1 + VTI_FLASH_MAX_POLLS);
}
