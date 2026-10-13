// broker.h - connection to the TI Revive broker (external/ti-revive/src/broker, protocol vtiproto.h), which owns the
// Virus TI's USB interfaces on every platform. The rewrite talks to the device only through it.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vti {

class Broker {
public:
    using RxHandler = std::function<void(uint8_t ep, const uint8_t *data, size_t len)>;
    using StateHandler = std::function<void(bool connected)>;

    ~Broker();
    // Connect (POSIX: Unix socket /tmp/vtibroker-<uid>.sock; Windows: named pipe - TODO), say HELLO, start the reader.
    bool open(std::string &error);
    void close();
    bool device_present() const { return present_; }

    void on_rx(RxHandler h) { rx_ = std::move(h); }
    void on_state(StateHandler h) { state_ = std::move(h); }

    bool write(uint8_t ep, const uint8_t *data, size_t len);
    // USB control request through the broker; returns bytes transferred or -1. `data` in/out.
    int control(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, uint8_t *data, uint16_t len,
                int timeout_ms = 3000);

    // Start/stop the Virus audio stream (only the owner may). Linux: 1 = streaming into the POSIX shm ring
    // (vtiaudioshm.h, VTI_AUDIO_SHM_LINUX), 0 = no audio function, -1 = error / not the owner.
    int audio(bool on, int timeout_ms = 3000);

private:
    intptr_t fd_ = -1; // POSIX socket / Windows pipe HANDLE (overlapped)
    int audio_reply_ = 0;
    bool audio_replied_ = false;
    std::thread reader_;
    std::mutex wr_, ctl_;
    std::condition_variable ctl_cv_;
    std::map<uint32_t, std::vector<uint8_t>> replies_; // seq -> i32 result + data
    uint32_t seq_ = 0;
    std::atomic<bool> present_{false}, stop_{false}, reader_done_{true};
    RxHandler rx_;
    StateHandler state_;
    bool send(uint16_t type, const void *a, size_t alen, const void *b = nullptr, size_t blen = 0);
    void read_loop();
};

} // namespace vti
