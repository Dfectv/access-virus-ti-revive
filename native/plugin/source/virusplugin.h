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
    bool connected() const { return broker_.device_present(); }
    AudioIn &audio() { return audio_; } // read from process(); valid once audio_ready()
    bool audio_ready() const { return audio_ok_; }

private:
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
    std::atomic<bool> stop_{false};
    bool open_ = false;
    AudioIn audio_;
    std::atomic<bool> audio_ok_{false};
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
    bool rename_part(int part, const std::string &name); // the part's patch name (10 characters) -> dump + Virus
    bool store_patch(int part, int bank, int program);   // the part's patch into a RAM slot of the Virus (+ cache)
    // the part's patch into a personal library (slot = position, == size: append) - writes the library file
    bool store_to_library(int part, int library, int slot);
    bool browser_match(int bank, int program);          // category filter + search of the browser
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
        return bank >= LIB_BASE ? "Personal Libraries/" + library_name(bank - LIB_BASE) : device_name() + "/" + bank_name(bank);
    }
    static std::string bank_name(int bank) // 0..3 = RAM A..D, 4.. = ROM A..
    {
        return bank < 4 ? std::string("RAM ") + (char)('A' + bank) : std::string("ROM ") + (char)('A' + bank - 4);
    }
    bool ui_changed() { return ui_dirty_.exchange(false); }
    // MIDI activity meters of the part strips (note-on velocity per MIDI channel, like the original's
    // "ShowMIDIActivityMeters"): tick_meters() decays them and says whether the shown values changed
    bool tick_meters(int step);
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
    };
    RemoteDevice dev_, ed_;
    std::unique_ptr<AccessFiles> files_;
    EditorLayout layout_;
    std::unique_ptr<Conditions> cond_;
    std::mutex values_mu_;
    std::map<int, std::map<std::string, int>> part_values_; // decoded part dumps + controllers, per part (-1 = global)
    std::map<std::string, int> ui_state_;                   // editor-only parameters (VirusControl/SelectedPage ...)
    std::string lcd_param_, lcd_value_, lcd_part_;          // the last edited parameter for the LCD
    std::string search_;                                    // browser search text
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
    bool state_loaded_ = false;                // setState or the default state was applied
    std::vector<std::vector<uint8_t>> default_; // a new instance's state (from the user's plug-in binary)

    void apply_state_to_params();
    void panel_message(const std::vector<uint8_t> &msg);
    void send_state();
    void set_part_value(int part, const std::string &path, int value); // keeps the part dump in state_ current
};

} // namespace vti
