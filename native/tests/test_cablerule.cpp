// Which USB-MIDI cable a queued message goes out on (cablerule.h, round 0.8.27 L3): the original sends every
// single dump (`10 <bank> <prog>`, 524 bytes) on cable 3 and everything else on cable 2 (its start-up trace,
// protocol.md; the patch-change capture captures/orig-patchchange-2, E268). Cable 3 reaches only the Virus' host
// CPU, cable 2 also both DSPs (E252), which then parse the dump byte by byte in the notes' stream.
#include "check.h"
#include "device/cablerule.h"

#include <vector>

using namespace vti;

static std::vector<uint8_t> single_dump(uint8_t bank, uint8_t prog)
{
    std::vector<uint8_t> d(524, 0);
    d[0] = 0xF0, d[1] = 0x00, d[2] = 0x20, d[3] = 0x33, d[4] = 0x01, d[5] = 0x10, d[6] = 0x10, d[7] = bank, d[8] = prog;
    d[523] = 0xF7;
    return d;
}

TEST(cablerule_single_dumps_go_on_cable_3)
{
    auto d = single_dump(0x00, 0x01); // part 2's edit buffer
    CHECK(queued_cable(d.data(), d.size(), 2) == 3);
    auto r = single_dump(0x01, 0x05); // into a RAM bank slot
    CHECK(queued_cable(r.data(), r.size(), 2) == 3);
}

TEST(cablerule_everything_else_keeps_its_cable)
{
    std::vector<uint8_t> multi(267, 0);
    multi[0] = 0xF0, multi[1] = 0x00, multi[2] = 0x20, multi[3] = 0x33, multi[4] = 0x01, multi[5] = 0x10;
    multi[6] = 0x11, multi[266] = 0xF7;
    CHECK(queued_cable(multi.data(), multi.size(), 2) == 2); // the multi dump: cable 2, like the original
    const uint8_t param[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x40, 0xF7};
    CHECK(queued_cable(param, sizeof param, 2) == 2);
    const uint8_t request[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x01, 0x05, 0xF7};
    CHECK(queued_cable(request, sizeof request, 2) == 2);
    const uint8_t cc[] = {0xB0, 0x07, 0x64};
    CHECK(queued_cable(cc, sizeof cc, 2) == 2);
    auto d = single_dump(0x00, 0x00);
    CHECK(queued_cable(d.data(), d.size(), 3) == 3); // an explicit cable (remote template records) stays
    d[5] = 0x7F;                                     // another device id: still a single dump
    CHECK(queued_cable(d.data(), d.size(), 2) == 3);
    d[6] = 0x10, d[1] = 0x01; // not Access: untouched
    CHECK(queued_cable(d.data(), d.size(), 2) == 2);
}
