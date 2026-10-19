// sendguard.h - what TI Revive never sends to the Virus (rule 2 of AGENTS.md), checked on every message.
//
// Why: issue #7 reports that the multi parameter "Original Patch" (`72 00 03`) made a TI2 load Init into all 16
// edit buffers at the next power-up (2/2 there, not reproduced by us; the DSP ignores page 0x72 parameters 0-6,
// the host CPU stores 0-3 in its multi edit buffer - E255/E258). The plug-in never builds such a message, but
// until round 0.8.27 that was an assumption about the code paths; now DeviceOut drops it with a log line. The
// same for status 0xF4, the DSP's private command parser (E236/E248). Page 0x72 parameters 4-13 (the multi
// name) and everything else pass.
#pragma once
#include <cstddef>
#include <cstdint>

namespace vti {

inline bool blocked_to_virus(const uint8_t *m, size_t n)
{
    if (n == 0)
        return false;
    if (m[0] == 0xF4)
        return true;
    // F0 00 20 33 01 <dev> 72 <part> <param> <value> F7 with param 0-3: Flags, Original Bank, Original Patch
    return n == 11 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x33 && m[4] == 0x01 && m[6] == 0x72 &&
           m[8] <= 3;
}

} // namespace vti
