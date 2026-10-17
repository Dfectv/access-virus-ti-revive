// stallwatch.h - a Virus audio stream that stood still while time went on, then ran on (E207 M3: macOS sleep pauses
// Core Audio; the ring keeps its frame counter and continues after wake, without a USB error). The Virus' stamp clock
// kept running meanwhile, so the plug-in's anchor between ring frames and that clock is off by the pause: every note
// was stamped ~19 s wrong and nothing sounded. update() reports the resume once; the plug-in then starts a new
// timeline (re-anchor, sync ping), as after a reconnect. Header only: the plug-in and the tests use it.
#pragma once
#include <cstdint>

namespace vti {

class StallWatch {
public:
    static constexpr int64_t LIMIT_MS = 1500; // longer than any hiccup of a busy system, shorter than a sleep

    // `newest` = the ring's newest frame (-1: no stream), `now_ms` = a steady clock; true once when the stream moves
    // again after standing still for LIMIT_MS or longer
    bool update(int64_t newest, int64_t now_ms)
    {
        if (newest < 0) {
            last_ = -1;
            return false;
        }
        if (last_ < 0 || newest < last_) { // first frame, or a new stream (its counter starts again)
            last_ = newest;
            moved_ms_ = now_ms;
            return false;
        }
        if (newest == last_)
            return false;
        const bool resumed = now_ms - moved_ms_ >= LIMIT_MS;
        last_ = newest;
        moved_ms_ = now_ms;
        return resumed;
    }

private:
    int64_t last_ = -1, moved_ms_ = 0;
};

} // namespace vti
