// panelfollow.h - what the plug-in does when the Virus reports a change made on its own panel (E152), like the
// original Virus Control: a preset chosen on the Virus (it reports the part's Bank / Patch and waits for the host
// to send the patch into the edit buffer) and a soft knob turned on the Virus (it reports only the knob's value).
// The decisions live here, without the plug-in, so the tests can check them; the plug-in does the sending.
#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace vti {

class PanelFollow {
public:
    using Clock = std::chrono::steady_clock;
    // the original loads a part's patch 150 ms after its last Bank / Patch report (timer 0x18007ec30): turning the
    // Virus' value knob through the presets loads only where it stops
    static constexpr std::chrono::milliseconds PRESET_QUIET{150};

    static bool is_preset_path(const std::string &path); // Multi Part Parameter/Bank or /Patch
    // a reported Bank / Patch change of `part` (0..15): marks it; false if it is no such report or out of range
    bool preset_report(const std::string &path, int part, Clock::time_point now);
    // the parts (bit per part) whose report is PRESET_QUIET old, and forgets them; 0 if none is due
    unsigned presets_due(Clock::time_point now);
    // a newer report of `part` waits (marked, not yet handed out): a reply read for an older choice is stale
    bool pending(int part);

    // "Global Soft Knob Configuration/Value Softknob0..2" -> soft knob 1..3, else 0
    static int softknob_report(const std::string &path);

    // the controllers the original sends again after a patch went into a part's edit buffer (0x180083ae0), so the
    // new patch starts with the current mod wheel, breath, ... positions
    static const std::vector<std::string> &restored_controllers();

    // the request for a part's edit buffer (30 00 <part>, read-only) and the bytes 6..8 of its reply (10 00 <part>):
    // the original asks for it when the chosen patch is not in its library
    static std::vector<uint8_t> edit_buffer_request(int part);
    static std::vector<uint8_t> edit_buffer_reply(int part);

private:
    std::mutex mu_;
    unsigned parts_ = 0;
    Clock::time_point due_{};
};

} // namespace vti
