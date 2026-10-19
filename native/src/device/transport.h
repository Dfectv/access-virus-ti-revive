// transport.h - Play and loop restarts, and what the original sends at them.
//
// Why: at every Play and every loop restart the original writes the song position F2, the global
// `F0 00 20 33 01 00 73 00 23 01 F7` and `6F <part> 7F 00` for all 16 parts (captures of the original, E275); TI
// Revive sends the first two. In the
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

// the pieces of a (re)start without allocation (the audio thread builds them): F2 and the +1 tick. The original
// also sends 6F <part> 7F 00 for all 16 parts (E276: an Atomizer value); left out - on the device each set delayed
// the next note by 29-37 ms (E280)
inline void transport_f2(int spp, uint8_t m[3])
{
    m[0] = 0xF2, m[1] = (uint8_t)(spp & 0x7F), m[2] = (uint8_t)((spp >> 7) & 0x7F);
}
constexpr uint8_t TRANSPORT_TICK[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x73, 0x00, 0x23, 0x01, 0xF7}; // device 00

// the messages of a (re)start at `spp` sixteenths (for tests and tools: it allocates)
inline std::vector<std::vector<uint8_t>> transport_messages(int spp)
{
    uint8_t f2[3];
    transport_f2(spp, f2);
    return {std::vector<uint8_t>(f2, f2 + 3), std::vector<uint8_t>(TRANSPORT_TICK, TRANSPORT_TICK + 11)};
}

} // namespace vti
