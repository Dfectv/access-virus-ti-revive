// tirbridge_core.h - the format-independent part of TI Revive's native bridge plug-in (findings/design-macos-bridge.md):
// helper process, control connection, instance, parameters, state, audio/MIDI block exchange, editor link and the
// helper's notifications. The VST3 (tirbridge_vst3.cpp) and Audio Unit (tirbridge_au.cpp) front ends sit on top.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <pthread.h>
#include <string>
#include <vector>

#include "tirbridge.h"
#include "tirbridge_view.h"

void blog(const char *fmt, ...) __attribute__((format(printf, 1, 2))); // /tmp/tirevive-bridge.log

struct Conn { // control connection to this DAW process' helper (one per instance)
    int fd = -1;
    std::mutex wr, mx;
    std::condition_variable cv;
    uint32_t seq = 0;
    std::map<uint32_t, std::vector<uint8_t>> replies;
    std::function<void(const tirb_msg &, const std::vector<uint8_t> &)> on_notify;
    pthread_t reader{};

    bool start();
    void stop();
    bool post(uint32_t type, uint32_t inst, const void *p, uint32_t len, uint32_t seq = 0);
    // request/response; false on a dead helper or timeout
    bool call(uint32_t type, uint32_t inst, const void *p, uint32_t len, std::vector<uint8_t> *out = nullptr,
              int timeout_s = 30);

  private:
    void read_loop();
};

class BridgeCore : public EditorLink {
  public:
    Conn conn;
    uint32_t inst = 0;
    tirb_create_reply layout{};
    tirb_shm *shm = nullptr;
    std::vector<tirb_param> params;
    std::map<uint32_t, int32_t> param_index;  // id -> index in params
    std::vector<std::atomic<double>> values;  // last known normalized values (parallel to params)
    std::map<uint32_t, uint32_t> midimap;     // (channel << 16 | controller) -> param id (VST3 CtrlNumber)
    bool ok = false;
    double rate = 44100;
    int32_t max_block = 512;
    Steinberg::IPlugView *view = nullptr; // the editor (also used by the AU's Cocoa view)

    virtual ~BridgeCore() = default;

    bool init();      // start/connect the helper, create the instance, fetch parameters and MIDI mapping
    void term();
    void set_active(bool on);
    void setup(double sample_rate, int32_t max_block, int32_t process_mode);
    bool get_state(std::vector<uint8_t> &out);
    bool set_state(const void *data, size_t len);

    // one audio block: fill shm->events/num_events, shm->pchanges/num_pchanges and shm->transport, then run();
    // true = outputs in shm->out (frames per channel), false = helper late or gone (output silence)
    bool run(uint32_t frames);
    void note_output_level(const float *const *chans, int nch, uint32_t frames); // 5 s diagnostics

    int32_t index_of(uint32_t id) const;
    void set_value(uint32_t id, double v);
    double value(uint32_t id) const;
    void set_param_from_host(uint32_t id, double v); // controller side (VST3 setParamNormalized, AU SetParameter)
    bool param_string(uint32_t id, double v, std::string &out);
    void refresh_values(); // after loading a state

    // the front end keeps the object alive while asynchronous main-thread work is pending
    virtual void hold() = 0;
    virtual void unhold() = 0;
    // helper notifications, delivered on the main thread
    virtual void on_param_edit(const tirb_param_edit &e) = 0; // begin / perform / end from the original's UI
    virtual void on_restart(int32_t flags) = 0;              // VST3 restartComponent flags

    // EditorLink
    bool editor_open(double x, double y, double scale, tirb_editor_reply &r) override;
    void editor_close() override;
    void editor_move(double x, double y) override;
    void editor_input(const tirb_input &in) override;
    void editor_view_gone() override { view = nullptr; }
    void editor_menu_result(const tirb_menu_result &r) override;

  private:
    uint32_t seq_ = 0;
    unsigned long misses_ = 0;
    double diag_frames_ = 0;
    float diag_peak_ = 0;
    void notification(const tirb_msg &h, const std::vector<uint8_t> &b); // reader thread
};
