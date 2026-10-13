// audioin.h - the Virus' audio (USB 1 / USB 2) from the TI Revive broker's ring (vtiaudioshm.h). The broker streams
// once its owner asked for it (Broker::audio); the synth is the clock master, so the reader keeps a fixed distance
// behind the newest frame and corrects drift by dropping/repeating single frames (TODO R5: resampling / F5 sync).
#pragma once
#include <cstdint>

namespace vti {

class AudioIn {
public:
    ~AudioIn();
    bool open();  // Linux: map /dev/shm/tirevive-audio-<uid>; Windows: the broker's mapping; macOS: Core Audio input
    void close();
    bool streaming() const;
    // n frames into usb1 (L, R) and usb2 (L, R); missing data -> silence. `latency` = target distance in frames.
    void read(float *usb1l, float *usb1r, float *usb2l, float *usb2r, int n, int latency);
    int64_t drops() const { return drops_; }
    // n frames from the absolute ring frame `pos` (silence where not available)
    void read_at(float *usb1l, float *usb1r, float *usb2l, float *usb2r, int n, int64_t pos);
    int64_t newest_frame() const; // synth frames produced so far (-1 if not streaming)

private:
    void *map_ = nullptr; // vti_audio_shm (vtiaudioshm.h)
    void *mac_ = nullptr; // macOS: the Core Audio reader that fills map_
    int64_t pos_ = -1, drops_ = 0;
};

} // namespace vti
