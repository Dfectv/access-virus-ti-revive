// offlinepace.h - offline render (export, freeze, offline bounce) at the Virus' pace. The host calls process() as fast
// as it can, but the Virus makes its sound in real time: without a wait the plug-in's timeline ran ahead of the audio
// ring, re-anchored again and again and the render got repeated or missing audio. The original refuses offline
// rendering (Virus Control 5.1.7: canDo "offline"/"noRealTime" = no, 0x18002d8c0; a warning when the host's process
// level is offline, 0x18002fd40 -> 0x180080ba0). Here each offline block waits until the Virus has produced as many
// frames as the blocks so far: the render is right, at real-time speed. Header only: the plug-in and the tests use it.
#pragma once
#include <cmath>
#include <cstdint>

namespace vti {

class OfflinePacer {
public:
    // a slow render never lags more than ~93 ms behind the Virus: the plug-in reads at the paced position, and the
    // audio ring keeps only 16384 frames (VTI_AUDIO_RING)
    static constexpr int64_t MAX_BEHIND = 4096;

    // the ring frame `newest` (the Virus' newest audio, -1 without audio) must reach before this block runs;
    // `frames` = the block in Virus frames (fractional at other host rates). < 0: nothing to pace by.
    int64_t wait_until(int64_t newest, double frames)
    {
        if (newest < 0) {
            reset();
            return -1;
        }
        if (pos_ < 0 || newest < seen_) { // the first offline block, or a new stream (the Virus came back)
            pos_ = (double)newest;
            seen_ = newest;
            return newest;
        }
        seen_ = newest;
        pos_ += frames;
        if (pos_ < (double)(newest - MAX_BEHIND))
            pos_ = (double)(newest - MAX_BEHIND);
        return (int64_t)std::floor(pos_ + 1e-9);
    }

    // back to real time, or the wait gave up (the Virus stopped streaming): the next offline block starts afresh
    void reset()
    {
        pos_ = -1.0;
        seen_ = -1;
    }

private:
    double pos_ = -1.0;
    int64_t seen_ = -1;
};

} // namespace vti
