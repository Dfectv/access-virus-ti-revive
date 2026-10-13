// virusplugin.h - the native Virus TI plug-in (R3): single-component VST3 like the original, same parameter ids
// (automation.xml + 32 MIDI-mapping targets) and the same state format (docs/formats.md), talking to the Virus
// through the TI Revive broker. Audio and editor follow in later steps.
#pragma once
#include "access/accessfiles.h"
#include "device/audioin.h"
#include "editor/editormodel.h"
#include "device/broker.h"
#include "device/memsync.h"
#include "model/remotedevice.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <chrono>
#include <thread>
#include <vector>

namespace vti {

// Original class ids (formats.md). The development build uses its own id (byte 15 XOR 0xA5) so it can sit next to
// the original; VTI_NATIVE_DROPIN=ON builds with the original id (loads projects saved with the original).
extern const Steinberg::FUID kVirusTIClassId;

// Messages to the Virus, sent by a worker thread (socket writes do not belong in process()).
class DeviceOut {
public:
    DeviceOut();
    ~DeviceOut();
    void send(std::vector<uint8_t> msg); // a complete MIDI message (SysEx or channel message); not from process()
    // from process() only (single producer, lock-free): a message of up to 16 bytes; false if the ring is full
    // `stamp` = frame on the Virus timeline (F5 time stamp before the message), -1 = the latest timeline position
    bool send_rt(const uint8_t *msg, size_t n, int64_t stamp = -1);
    std::atomic<int64_t> timeline{-1};
    bool busy() const { return queued_.load() > 0; } // non-realtime messages (state upload) still waiting
    // USB control request through the broker (device descriptor / strings)
    int control(uint8_t rt, uint8_t req, uint16_t value, uint16_t index, uint8_t *data, uint16_t len)
    {
        return broker_.control(rt, req, value, index, data, len);
    } // the latest timeline position (process() block start), for unstamped messages
    // messages from the Virus' panel (EP 0x86 cable 2: knob moves as CC / poly pressure / parameter SysEx)
    void on_panel(std::function<void(const std::vector<uint8_t> &)> h) { panel_ = std::move(h); }
    // SysEx replies (dumps, bank info): EP 0x86 cable 3 in a plug-in session, EP 0x85 cable 1 otherwise
    void on_reply(std::function<void(const std::vector<uint8_t> &)> h) { reply_ = std::move(h); }
    // called by the worker every loop (<= 1 ms): non-realtime follow-up of process() (state updates)
    void on_idle(std::function<void()> h) { idle_ = std::move(h); }
    // the Virus went away (USB unplugged / switched off) or came back while the broker connection stays
    void on_device(std::function<void(bool present)> h) { device_ = std::move(h); }
    std::atomic<int> generation{0}; // +1 on every return of the Virus (process() re-anchors its timeline)
    bool connected() const { return broker_.device_present(); }
    AudioIn &audio() { return audio_; } // read from process(); valid once audio_ready()
    bool audio_ready() const { return audio_ok_; }

private:
    std::function<void(bool)> device_;
    int present_seen_ = -1; // worker: last device presence handled (-1 = unknown)
    Broker broker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::vector<uint8_t>> queue_;
    std::atomic<int> queued_{0};
    struct Slot {
        uint8_t len, data[16];
        int64_t stamp;
    };
    static constexpr size_t RT_SLOTS = 1024;
    Slot rt_[RT_SLOTS];
    std::atomic<size_t> rt_w_{0}, rt_r_{0};
    int rt_run_ = 0; // real-time messages sent in a row (worker thread)
    std::atomic<bool> stop_{false};
    bool open_ = false;
    AudioIn audio_;
    std::atomic<bool> audio_ok_{false};
    std::chrono::steady_clock::time_point audio_retry_{}; // next audio start attempt (worker thread)
    std::chrono::steady_clock::time_point stats_next_{};  // VTI_NATIVE_STATS output
    std::function<void(const std::vector<uint8_t> &)> panel_, reply_;
    std::function<void()> idle_;
    bool rx_set_ = false, pending_send_ = false;
    std::thread worker_;
    void run();
};

class VirusPlugin : public Steinberg::Vst::SingleComponentEffect, public Steinberg::Vst::IMidiMapping {
public:
    static Steinberg::FUnknown *create(void *) { return (Steinberg::Vst::IAudioProcessor *)new VirusPlugin; }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup &setup) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData &data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *state) override;

    Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString name) override;

    // editor support (viruseditor.cpp)
    const AccessFiles &files() const { return *files_; }
    const EditorLayout &layout() const { return layout_; }
    const Conditions &conditions() const { return *cond_; }
    const RemoteDevice &device() const { return dev_; }
    const RemoteDevice &editor_device() const { return ed_; }
    int ui_value(const std::string &path, int part);              // device, controller or editor-state value
    void ui_set(const std::string &path, int part, int value);    // a change made in the editor
    std::string patch_name(int part);
    std::string bank_patch(int bank, int program); // stored patch name from the bank cache (RAM A..D)
    bool load_patch(int bank, int program, int part); // a stored patch into a part's edit buffer (and the state)
    bool copy_part(int from, int to);                    // a part's patch onto another part (drag and drop)
    bool load_dump(std::vector<uint8_t> d, int part);    // a single dump into a part's edit buffer
    bool rename_part(int part, const std::string &name); // the part's patch name (10 characters) -> dump + Virus
    bool store_patch(int part, int bank, int program);   // the part's patch into a RAM slot of the Virus (+ cache)
    // the part's patch into a personal library (slot = position, == size: append) - writes the library file
    bool store_to_library(int part, int library, int slot);
    // does a stored patch have category `cat` (filter menu index, 0 = any) and contain `q` in its name?
    bool patch_matches(int bank, int program, int cat, const std::string &q);
    static constexpr int SEARCH_BANK = 1000;             // "Search Results" (search://): the current page
    bool results_step(int dir);                          // previous / next results page (false: none)
    std::string results_title();
    void update_results();
    void warm_categories(); // background: fill the category cache
    std::atomic<bool> stop_warm_{false}; // terminate(): stops the bank sync and the category pass
    std::string category_name_(); // the selected category filter's name
    void set_search(const std::string &s);
    std::string search() { std::lock_guard<std::mutex> lk(values_mu_); return search_; }
    // device banks 0..3 RAM, 4.. ROM; personal libraries from bank id 100 on
    static constexpr int LIB_BASE = 100;
    int bank_count() // device banks only
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        int n = 4;
        for (auto &kv : banks_)
            if (kv.first < LIB_BASE)
                n = std::max(n, kv.first + 1);
        return n;
    }
    int library_count() { std::lock_guard<std::mutex> lk(banks_mu_); return (int)lib_names_.size(); }
    std::string library_name(int i) { std::lock_guard<std::mutex> lk(banks_mu_); return i < (int)lib_names_.size() ? lib_names_[i] : ""; }
    int library_size(int i)
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(LIB_BASE + i);
        return b == banks_.end() ? 0 : (int)b->second.size();
    }
    std::string device_name() const // as the original shows it in the browser and the menus
    {
        return model_ == 1 ? "Virus TI (Virus TI Desktop)" : model_ == 2 ? "Virus TI Snow (Virus TI Snow)"
                                                                          : "Virus TI2 (Virus TI2 Desktop)";
    }
    std::string bank_title(int bank) // browser title
    {
        if (bank == SEARCH_BANK)
            return results_title();
        return bank >= LIB_BASE ? "Personal Libraries/" + library_name(bank - LIB_BASE) : device_name() + "/" + bank_name(bank);
    }
    static std::string bank_name(int bank) // 0..3 = RAM A..D, 4.. = ROM A..
    {
        return bank < 4 ? std::string("RAM ") + (char)('A' + bank) : std::string("ROM ") + (char)('A' + bank - 4);
    }
    bool ui_changed() { return ui_dirty_.exchange(false); }
    // field diagnostics (file ~/.local/share/virus-ti-native/diag.on enables a log every 10 s in diag.log)
    struct Diag {
        std::atomic<long> full_draws{0}, part_draws{0}, draw_us{0}, dirty_ticks{0}, process_calls{0};
        std::atomic<long> process_max_us{0}, process_over{0}; // over = longer than the block's real time
    } diag;
    bool diag_enabled();
    void diag_report(); // editor timer thread
    // MIDI activity meters of the part strips (note-on velocity per MIDI channel, like the original's
    // "ShowMIDIActivityMeters"): tick_meters() decays them and returns the channels whose shown value changed (bits)
    int tick_meters(int step);
    int meter(int channel) const { return channel >= 0 && channel < 16 ? meter_shown_[channel] : 0; }
    std::string live_text(const std::string &param); // LCD texts of the last edited parameter

    // IMidiMapping: channel aftertouch / pitch bend -> the original's per-channel parameters
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID &id) override;

    OBJ_METHODS(VirusPlugin, SingleComponentEffect)
    DEFINE_INTERFACES
    DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

private:
    struct Mapped {
        std::string path;
        int part = 0; // 0-based
        int pending = -1; // automation throttle (audio thread): the latest value waiting to be sent
    };
    // automation budget: a flood of parameter messages (stress test: 32 lanes per block, ~2750/s) hangs the
    // Virus' audio until a power cycle - changes are coalesced per parameter and sent at <= AUTOMATION_RATE/s
    static constexpr double AUTOMATION_RATE = 250; // 1000/s still hung together with 2750 notes/s
    static constexpr int PENDING_MAX = 4096;
    Steinberg::Vst::ParamID pending_ids_[PENDING_MAX];
    int pending_head_ = 0, pending_count_ = 0;
    double automation_budget_ = 0;
    RemoteDevice dev_, ed_;
    std::unique_ptr<AccessFiles> files_;
    EditorLayout layout_;
    std::unique_ptr<Conditions> cond_;
    std::mutex values_mu_;
    std::map<int, std::map<std::string, int>> part_values_; // decoded part dumps + controllers, per part (-1 = global)
    std::map<std::string, int> ui_state_;                   // editor-only parameters (VirusControl/SelectedPage ...)
    // meta parameters (editordevice.xml meta="true", e.g. filter link, FX switches, LFO clocks): per-part editor values
    // of "part all" editor parameters, meta values, last condition state of retrigger metas (values_mu_)
    std::map<std::pair<std::string, int>, int> ed_part_, meta_vals_;
    std::map<std::pair<std::string, int>, bool> meta_cond_;
    std::recursive_mutex meta_mu_;
    int meta_depth_ = 0; // > 0 while the engine sets linked parameters (no LCD update for those)
    void meta_source(const std::string &path, int part, int value, bool full);
    void meta_set(const Parameter &m, int part, int mv, const std::string &except, bool full);
    void meta_retrigger(int part, bool full);
    void meta_init();
    bool meta_cond(const Parameter &m, int part);
    void set_editor_value(const std::string &path, int part, int value);
    std::string lcd_param_, lcd_value_, lcd_part_, lcd_orig_; // the last edited parameter for the LCD
    void set_lcd(const std::string &param, const std::string &value, const std::string &part, const std::string &before);
    std::string search_;                                    // browser search text
    int results_return_ = 0;                                // browser 0's bank before the results
    std::vector<std::vector<uint8_t>> results_;             // all search results (banks_mu_)
    int results_page_ = 0;
    void show_results_page();
    std::map<std::pair<int, int>, std::array<int, 3>> cat_cache_; // (bank, program) -> checksum, category 1, 2
    std::atomic<bool> ui_dirty_{true};
    std::atomic<int> meter_[16] = {};
    int meter_shown_[16] = {}; // editor thread
    // automation values from process() for the part dumps (single producer / single consumer, lock-free)
    struct ParamUpdate {
        Steinberg::Vst::ParamID id;
        int value;
    };
    static constexpr size_t PU_SLOTS = 4096;
    ParamUpdate pu_[PU_SLOTS];
    std::atomic<size_t> pu_w_{0}, pu_r_{0};
    void drain_param_updates();
    std::map<int, std::vector<std::vector<uint8_t>>> banks_; // RAM A..D (bank cache, see load_banks), ROM, libraries
    std::vector<std::string> lib_names_, lib_paths_;
    std::mutex banks_mu_;
    void load_banks();
    std::unique_ptr<MemorySync> sync_;
    std::thread sync_thread_;
    std::atomic<bool> sync_started_{false};
    void sync_banks(); // brings the bank cache up to date with the Virus (background thread)
    void load_rom_banks(); // ROM A.. from the user's firmware.bin for the connected model
    std::atomic<bool> syncing_{false};
    std::atomic<int> model_{0}; // 0 = TI2, 1 = TI, 2 = Snow (from the device descriptor)
    std::map<Steinberg::Vst::ParamID, Mapped> params_;
    std::map<std::pair<std::string, int>, Steinberg::Vst::ParamID> ids_; // (path, part 0-based) -> id
    std::mutex pending_mu_;
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> pending_; // device -> host, handed over in process()
    const MidiMessage *single_template_ = nullptr;
    std::vector<std::vector<uint8_t>> state_; // the state records (start-up messages), see formats.md
    std::mutex state_mu_;
    std::unique_ptr<DeviceOut> out_;
    int block_ = 512;
    double rate_ = 44100;
    // the Virus timeline (docs/protocol.md "timing"): frames of the synth, stamped on every clock tick and event
    int64_t tl_ = 0;
    bool tl_valid_ = false, anchored_ = false;
    double next_tick_ = 0;
    int64_t stream_start_ = -1;
    int generation_seen_ = 0; // DeviceOut::generation handled by process()
    bool device_seen_ = false; // the Virus was present once (worker thread)
    std::set<std::string> dump_paths_; // parameters stored in a part's single dump (the rest: single messages)
    int state_part(const std::string &path, int part) const;
public:
    // parameters changed since the last call (the editor repaints linked controls while a knob is dragged)
    std::vector<std::string> take_changed()
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        std::vector<std::string> v(changed_.begin(), changed_.end());
        changed_.clear();
        return v;
    }

private:
    std::set<std::string> changed_; // values_mu_
    bool state_loaded_ = false;                // setState or the default state was applied
    std::vector<std::vector<uint8_t>> default_; // a new instance's state (from the user's plug-in binary)

    void apply_state_to_params();
    void panel_message(const std::vector<uint8_t> &msg);
    void send_state();
    void set_part_value(int part, const std::string &path, int value); // keeps the part dump in state_ current
};

} // namespace vti
