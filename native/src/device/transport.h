// transport.h - Play and loop restarts, and what the original sends at them.
//
// Why: at every Play and every loop restart the original writes the song position F2, the global
// `F0 00 20 33 01 00 73 00 23 01 F7` and `6F <part> 7F 00` for all 16 parts (captures of the original, E275). In the
// Virus' DSP the F2 handler sets its MIDI-clock position (0x540ad/0x540b4: position x 6 - 1) and the handler of global
// 0x23 (0x540ca) adds its value to that position, so the pair puts the clock exactly on the song position: the
// arpeggiator and clock-synced LFOs restart with the bar. 6F <part> 7F (handler 0x565a8, one DSP, the part's voice
// slot via X:$54136) sets a per-part value the original always sets to 0 - what it controls is open (LOW).
// TI Revive sent F2 only when it anchored its timeline (a start, a large jump), not at Play or a loop.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace vti {

class TransportWatch {
public:
    // per block: playing, the block's start in quarter notes, its length in quarter notes; true = (re)start here
    bool update(bool playing, double ppq, double len_qn)
    {
        bool restart = playing && (!was_ || std::fabs(ppq - next_) > 0.01); // 0.01 quarter: ~5 ms at 120 BPM
        was_ = playing;
        next_ = ppq + len_qn;
        return restart;
    }

private:
    bool was_ = false;
    double next_ = 0;
};

// the messages of a (re)start at `spp` sixteenths, in the original's order
inline std::vector<std::vector<uint8_t>> transport_messages(int spp)
{
    std::vector<std::vector<uint8_t>> m;
    m.push_back({0xF2, (uint8_t)(spp & 0x7F), (uint8_t)((spp >> 7) & 0x7F)});
    m.push_back({0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x73, 0x00, 0x23, 0x01, 0xF7}); // device id 00, as the original
    for (uint8_t p = 0; p < 16; p++)
        m.push_back({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6F, p, 0x7F, 0x00, 0xF7});
    return m;
}

} // namespace vti
