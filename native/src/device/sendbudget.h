// sendbudget.h - a byte budget for the MIDI traffic to the Virus' DSP (token bucket).
//
// Why: the Virus' DSP parses a stamped message at once when its stamp is within the threshold (up to 1023 frames
// ahead of its clock; the voice then starts at the stamp) and otherwise parks everything up to the next stamp in a
// 2046-byte queue without a fill check (firmware, E235/E261). Our stamps leave the plug-in A frames ahead of the
// newest ring frame (A = 1024 on Linux/macOS: nothing is parked; A = 3072 on Windows: ~1400 frames = 30 ms are),
// so on Windows more than ~2000 bytes within ~30 ms wrap the queue and corrupt the stream. Unstamped SysEx that
// follows a parked stamp is parked with it (part dumps, the editor's parameters, the state), so it counts too -
// as a debt capped at one burst (take_queued), because the Virus paces long dumps itself (E239) and a state send
// must not silence the notes for its whole length afterwards. Parameters from automation are limited before this
// (AutomationQueue, 250/s). Musical use never comes near the budget (4 kB/s = 660 notes/s with their stamps, a
// burst of 1000 bytes = 160 simultaneous notes); a runaway host is delayed, not dropped - stamps are absolute, so an
// event sent a few blocks later still sounds at its time. The old stress hang (1000 parameter changes/s plus
// ~2750 notes/s, Linux) is NOT explained by this queue (nothing parked at A = 1024, E261); the budget stays as the
// safe bound for what the DSP can park.
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
    // unstamped bytes just sent on the same cable (queued SysEx): they wait in the DSP's queue like stamped ones
    // when they follow a parked stamp, so they delay the next stamped round - by at most one burst (burst / rate)
    void take_queued(int bytes)
    {
        if (rate <= 0)
            return;
        tokens -= bytes;
        if (tokens < -burst)
            tokens = -burst;
    }
};

} // namespace vti
