// cablerule.h - which USB-MIDI cable a queued message to the Virus goes out on.
//
// Why: the Virus' host CPU hands cable 3 to its own parser only, cable 5 to the DSPs only and every other cable to
// both (E252). The original sends every single dump (`10 <bank> <prog>`, 524 bytes) on cable 3 and everything else
// on cable 2 (its start-up trace, protocol.md; a patch change while another part plays, E268). On cable 2 the two
// DSPs also parse the 524 bytes, in the same byte stream as the notes and behind their last time stamp: with TI
// Revive a patch change on one part made the others stop for a moment and raised 6-12 Sync Information Events per
// Play (E266/E267); the original plays through. The multi dump (`11`) stays on cable 2, as in the original.
#pragma once
#include <cstddef>
#include <cstdint>

namespace vti {

// m = a complete message queued for cable `cable` (2 unless the sender chose another one)
inline uint8_t queued_cable(const uint8_t *m, size_t n, uint8_t cable)
{
    if (cable == 2 && n > 8 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x33 && m[4] == 0x01 &&
        m[6] == 0x10)
        return 3;
    return cable;
}

} // namespace vti
