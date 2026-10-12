// usbmidi.h - MIDI bytes <-> USB-MIDI event packets (4 bytes: cable << 4 | code index, 3 data bytes), as the Virus
// TI's plug-in channel (EP 0x06 / 0x86) carries them: cable 2 = MIDI/SysEx of the plug-in, cable 5 = F5 time stamps.
#pragma once
#include <cstdint>
#include <vector>

namespace vti {

// One complete MIDI message (channel message, system common or a whole SysEx F0..F7) as packets for `cable`.
inline std::vector<uint8_t> usbmidi_pack(const std::vector<uint8_t> &msg, uint8_t cable)
{
    std::vector<uint8_t> out;
    if (msg.empty())
        return out;
    auto put = [&](uint8_t cin, uint8_t a, uint8_t b, uint8_t c) {
        out.insert(out.end(), {(uint8_t)(cable << 4 | cin), a, b, c});
    };
    uint8_t s = msg[0];
    if (s == 0xF0) {
        for (size_t i = 0; i < msg.size(); i += 3) {
            size_t rest = msg.size() - i;
            uint8_t cin = rest > 3 ? 0x4 : rest == 3 ? 0x7 : rest == 2 ? 0x6 : 0x5;
            put(cin, msg[i], rest > 1 ? msg[i + 1] : 0, rest > 2 ? msg[i + 2] : 0);
        }
    } else if (s >= 0x80 && s < 0xF0) {
        uint8_t cin = s >> 4;
        put(cin, s, msg.size() > 1 ? msg[1] : 0, msg.size() > 2 ? msg[2] : 0);
    } else { // system common / real time: 1..3 bytes
        uint8_t cin = msg.size() == 1 ? 0xF : msg.size() == 2 ? 0x2 : 0x3;
        put(cin, s, msg.size() > 1 ? msg[1] : 0, msg.size() > 2 ? msg[2] : 0);
    }
    return out;
}

} // namespace vti
