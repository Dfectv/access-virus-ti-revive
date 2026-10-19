// sendguard.h - what TI Revive never sends to the Virus (rule 2 of AGENTS.md), checked on every message: send() and
// send_rt() check each one, send_state() a project's records with drop_unsendable().
//
// Why: issue #7 reports that the multi parameter "Original Patch" (`72 00 03`) made a TI2 load Init into all 16
// edit buffers at the next power-up (2/2 there, not reproduced by us; the DSP ignores page 0x72 parameters 0-6,
// the host CPU stores 0-3 in its multi edit buffer - E255/E258). The plug-in never builds such a message, but
// until round 0.8.27 that was an assumption about the code paths; now DeviceOut drops it with a log line. The
// same for status 0xF4, the DSP's private command parser (E236/E248). Page 0x72 parameters 4-13 (the multi
// name) and everything else pass.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

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

// One complete message the Virus can parse: a SysEx F0 .. F7 or a channel message of its exact length, every data
// byte below 0x80. State records are always one of these (real projects: SysEx and controllers).
inline bool one_message(const uint8_t *m, size_t n)
{
    if (n == 0)
        return false;
    if (m[0] == 0xF0)
        return n >= 2 && m[n - 1] == 0xF7 && std::all_of(m + 1, m + n - 1, [](uint8_t b) { return b < 0x80; });
    if (m[0] < 0x80 || m[0] >= 0xF0)
        return false;
    const size_t len = (m[0] & 0xF0) == 0xC0 || (m[0] & 0xF0) == 0xD0 ? 2 : 3;
    return n == len && std::all_of(m + 1, m + n, [](uint8_t b) { return b < 0x80; });
}

// A project's state records before they go to the Virus (VirusPlugin::send_state): they come from the host's project
// file, so a damaged or crafted file must not get past rule 2 or put a broken message on the wire. Removes what
// blocked_to_virus() forbids and what is not one_message(), keeps the order; returns how many were removed.
inline size_t drop_unsendable(std::vector<std::vector<uint8_t>> &recs)
{
    const size_t before = recs.size();
    recs.erase(std::remove_if(recs.begin(), recs.end(),
                              [](const std::vector<uint8_t> &r) {
                                  return blocked_to_virus(r.data(), r.size()) || !one_message(r.data(), r.size());
                              }),
               recs.end());
    return before - recs.size();
}

} // namespace vti
