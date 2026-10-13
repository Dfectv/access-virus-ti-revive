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
    bool send_rt(const uint8_t *msg, size_t n);
    // messages from the Virus' panel (EP 0x86 cable 2: knob moves as CC / poly pressure / parameter SysEx)
    void on_panel(std::function<void(const std::vector<uint8_t> &)> h) { panel_ = std::move(h); }
    // SysEx replies (dumps, bank info): EP 0x86 cable 3 in a plug-in session, EP 0x85 cable 1 otherwise
    void on_reply(std::function<void(const std::vector<uint8_t> &)> h) { reply_ = std::move(h); }
    bool connected() const { return broker_.device_present(); }
    AudioIn &audio() { return audio_; } // read from process(); valid once audio_ready()
    bool audio_ready() const { return audio_ok_; }

private:
    Broker broker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::vector<uint8_t>> queue_;
    struct Slot {
        uint8_t len, data[16];
    };
    static constexpr size_t RT_SLOTS = 1024;
    Slot rt_[RT_SLOTS];
    std::atomic<size_t> rt_w_{0}, rt_r_{0};
    std::atomic<bool> stop_{false};
    bool open_ = false;
    AudioIn audio_;
    std::atomic<bool> audio_ok_{false};
    std::function<void(const std::vector<uint8_t> &)> panel_, reply_;
    bool rx_set_ = false;
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
    bool ui_changed() { return ui_dirty_.exchange(false); }

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
    std::atomic<bool> ui_dirty_{true};
    std::map<int, std::vector<std::vector<uint8_t>>> banks_; // RAM A..D (bank cache, see load_banks)
    std::mutex banks_mu_;
    void load_banks();
    std::unique_ptr<MemorySync> sync_;
    std::thread sync_thread_;
    std::atomic<bool> sync_started_{false};
    void sync_banks(); // brings the bank cache up to date with the Virus (background thread)
    std::map<Steinberg::Vst::ParamID, Mapped> params_;
    std::map<std::pair<std::string, int>, Steinberg::Vst::ParamID> ids_; // (path, part 0-based) -> id
    std::mutex pending_mu_;
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> pending_; // device -> host, handed over in process()
    const MidiMessage *single_template_ = nullptr;
    std::vector<std::vector<uint8_t>> state_; // the state records (start-up messages), see formats.md
    std::mutex state_mu_;
    std::unique_ptr<DeviceOut> out_;
    int block_ = 512;
    bool state_loaded_ = false;                // setState or the default state was applied
    std::vector<std::vector<uint8_t>> default_; // a new instance's state (from the user's plug-in binary)

    void apply_state_to_params();
    void panel_message(const std::vector<uint8_t> &msg);
    void send_state();
    void set_part_value(int part, const std::string &path, int value); // keeps the part dump in state_ current
};

} // namespace vti
