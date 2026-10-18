// sendbudget.h - a byte budget for the stamped MIDI traffic to the Virus (token bucket).
//
// Why: the Virus' DSP parks every stamped byte in a 2046-byte queue without a fill check until the stamp is due
// (firmware, E235/E238). With the plug-in's lead of ~160 ms a burst of more than ~2000 bytes in that window wraps
// the queue and corrupts the stream; the old stress test that hung the Virus (1000 parameter changes/s plus ~2750
// notes/s) was 3.6 kB per window. Parameters are already limited (AutomationQueue, 250/s); this bounds everything
// that leaves process() with a stamp: notes, controllers, clock. Musical use never comes near it (4 kB/s = 660
// notes/s with their stamps, a burst of 1000 bytes = 160 simultaneous notes); a runaway host is delayed, not
// dropped - stamps are absolute, so an event sent a few blocks later still sounds at its time.
#pragma once
#include <cstdint>

namespace vti {

struct SendBudget {
    double rate = 4000; // bytes per second, sustained (0 = unlimited)
    double burst = 1000; // bytes available at once
    double tokens = 1000;
    int64_t last_ns = -1;

    // bytes that may go out now
    int available(int64_t now_ns)
    {
        if (rate <= 0)
            return 1 << 20;
        if (last_ns >= 0 && now_ns > last_ns)
            tokens += (double)(now_ns - last_ns) * 1e-9 * rate;
        last_ns = now_ns;
        if (tokens > burst)
            tokens = burst;
        return tokens < 0 ? 0 : (int)tokens;
    }
    // bytes just sent (may drive the balance below zero; the next round waits for it)
    void take(int bytes) { tokens -= bytes; }
};

} // namespace vti
