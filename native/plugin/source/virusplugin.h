// virusplugin.h - the native Virus TI plug-in (R3): single-component VST3 like the original, same parameter ids
// (automation.xml + 32 MIDI-mapping targets) and the same state format (docs/formats.md), talking to the Virus
// through the TI Revive broker. Audio and editor follow in later steps.
#pragma once
#include "access/accessfiles.h"
#include "device/audioin.h"
#include "editor/editormodel.h"
#include "device/automation.h"
#include "device/broker.h"
#include "device/memsync.h"
#include "device/resampler.h"
#include "model/panelfollow.h"
#include "model/remotedevice.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
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
    // Connect to the broker (and send what is queued) only from now on: the plug-in calls it on its first
    // setActive(true). A host's plug-in scan (initialize + terminate, no activation) never reaches the Virus.
    void start() { go_ = true; }
    void send(std::vector<uint8_t> msg); // a complete MIDI message (SysEx or channel message); not from process()
    // a whole state upload: replaces the records of an earlier upload that are still queued (only the newest state
    // counts; a waiting instance got two before its takeover, E149). Returns how many queued records it dropped.
    int send_state(std::vector<std::vector<uint8_t>> records);
    // from process() only (single producer, lock-free): a message of up to 16 bytes; false if the ring is full
    // `stamp` = frame on the Virus timeline (F5 time stamp before the message), -1 = the latest timeline position
    bool send_rt(const uint8_t *msg, size_t n, int64_t stamp = -1);
    // after a block's send_rt calls: wakes the worker (no lock; one notify per block instead of a 1 ms poll)
    void wake_rt() { cv_.notify_one(); }
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
    // another instance in this process owns the Virus: this one waits (true), or takes over after the wait (false)
    void on_waiting(std::function<void(bool waiting)> h) { on_waiting_ = std::move(h); }
    bool waiting() const { return wait_reported_.load(); }
    std::atomic<int> generation{0}; // +1 on every return of the Virus (process() re-anchors its timeline)
    bool connected() const { return broker_.device_present(); }
    AudioIn &audio() { return audio_; } // read from process(); valid once audio_ready()
    bool audio_ready() const { return audio_ok_; }
    // R5 sync marker (docs/protocol.md "Sync marker"): after the sync ping the Virus puts one non-zero sample on USB 2
    // where its 14-bit time stamp clock is 0, so a stamp s plays at the ring frame marker + s (mod 16384).
    // process() calls calibrate() with the ring frame of its ping; the worker finds the marker (-1 = searching or
    // unknown, -2 = not found: stamps stay relative to the ring like before)
    // Every ping RESETS that clock, and the Virus can answer seconds late (Windows: start-up dumps still being
    // digested), so a ping waits up to 5 s for its marker and the newest marker always wins
    void calibrate(int64_t from, bool new_stream)
    {
        marker_from_ = from;
        pings_out_.fetch_add(1);
        if (new_stream || marker.load() < 0) // a new audio stream (start, the Virus came back): the old one is void
            marker = -1;
    }
    bool calibrating() const { return pings_out_.load() > 0; }
    std::atomic<int64_t> marker{-2};
    std::atomic<int64_t> lead{0}; // frames from a timeline position to its sound in the ring (VirusPlugin::lead())
    // the newest ring frame that had sound on USB 1 / USB 2 (the ping waits for 1 s of silence, like the original)
    std::atomic<int64_t> last_sound{-1};
    // R5 diagnostics (VTI_NATIVE_STATS, every 10 s): stamp-aligned reads, their smallest distance to the newest frame,
    // timeline drift steps
    std::atomic<int64_t> aligned_blocks{0}, read_margin_min{INT64_MAX}, tl_steps{0};
    // process() calls/samples and timeline events (anticipative hosts call in bursts ahead of real time)
    std::atomic<int64_t> host_frames{0}, anchors{0}, err_min{INT64_MAX}, err_max{INT64_MIN};
    // R5 lines also go to this file (diag.on, field tests); set once after construction, read by the worker
    void set_diag_path(const std::string &p)
    {
        snprintf(diag_path_, sizeof diag_path_, "%s", p.c_str());
        diag_on_.store(true, std::memory_order_release);
        // klog.on next to diag.log also enables the R5 log (a DAW started from the desktop misses new env variables)
        std::string k = p.substr(0, p.find_last_of("/\\") + 1) + "klog.on";
        if (FILE *f = fopen(k.c_str(), "rb")) {
            fclose(f);
            klog_.store(true);
        }
    }
    void log(const char *fmt, ...);

private:
    std::function<void(bool)> device_;
    std::function<void(bool)> on_waiting_;
    // One instance per process talks to the Virus (README "One plug-in instance per synth"; issue #6). The Virus has
    // one stamp clock and every sync ping resets it, so a second instance (Ableton: duplicated track) shifted the
    // first one's time stamps for good. The Windows broker shares the Virus between all clients (the ASIO driver
    // runs in the same process), so the guard lives here; macOS/Linux brokers also give it to one client only.
    bool claim_owner();
    void release_owner();
    bool owner_ = false;         // worker: this instance holds the process-wide claim
    std::atomic<bool> wait_reported_{false}; // waiting for the other instance (set by the worker)
    int present_seen_ = -1; // worker: last device presence handled (-1 = unknown)
    Broker broker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<uint8_t> pk_; // worker: the packets of one broker write
    std::atomic<bool> klog_{getenv("VTI_NATIVE_KLOG") != nullptr}; // R5 diagnostic, see klog_check; or klog.on
    struct KlogNote {
        int64_t stamp, sent, scanned; // stamp, newest frame when sent, ring frames scanned so far
        int quiet;                    // quiet frames before the scan position
    };
    std::vector<KlogNote> klog_stamps_;
    void klog_check();
    std::chrono::steady_clock::time_point queue_hold_until_{}; // queued messages wait (the Virus busy with a multi)
    char diag_path_[1024] = {};
    std::atomic<bool> diag_on_{false};
    void scan_audio(); // worker: silence tracking + marker search
    std::atomic<int64_t> marker_from_{-1};
    std::atomic<int> pings_out_{0}; // pings sent whose marker has not been seen yet
    int64_t marker_skip_ = -1;      // worker: frames up to here belong to the last marker
    int64_t scanned_ = -1; // ring frames up to here were looked at
    struct Queued {
        std::vector<uint8_t> msg;
        bool state; // part of a state upload (send_state)
    };
    std::deque<Queued> queue_;
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
    int64_t stats_newest_ = -1; // worker: the newest ring frame at the last stats line (per instance, issue #6)
    std::function<void(const std::vector<uint8_t> &)> panel_, reply_;
    std::function<void()> idle_;
    bool rx_set_ = false, pending_send_ = false;
    std::atomic<bool> go_{false};
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
    // the host plays (transport running or notes): steady_clock milliseconds of the last activity (audio thread)
    std::atomic<int64_t> last_play_ms_{0};
    std::atomic<int64_t> last_note_ms_{0}; // last note-on from the host (pauses the bank sync)
    int64_t sync_paused_ms_ = 0;           // bank sync thread only: time paused for notes so far
    std::atomic<bool> sync_paused_{false}; // the bank sync waits for a gap between notes
    std::atomic<int> sync_eta_s_{-1};       // see sync_eta()
    int marker_retries_ = 0; // process(): sync pings sent again after "no marker"
    static int64_t now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    bool wait_until_quiet(); // the bank sync's before_request: waits while notes arrive (playing has priority)
    void wait_virus_ready(); // the bank sync thread at start-up / takeover: probe, then the sync marker (see .cpp)
    void set_sync_progress(int v); // the original's start-up page: 0..126 shown, 127 hidden
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
    bool sync_paused() const { return syncing_.load() && sync_paused_.load(); } // the sync page's note
    // estimated seconds the bank sync still needs (-1: unknown or done); a first sync reads 512 programs at ~1 s each
    int sync_eta() const { return syncing_.load() ? sync_eta_s_.load() : -1; }
    bool lcd_changed() { return lcd_dirty_.exchange(false); } // only the LCD texts changed
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
    // "Softknobs/Softknob N" of a part: the parameter its "Soft Knobs/Destination N" points to (SoftknobMapping),
    // "" when unassigned or for other paths. The original's soft knob is a proxy of that parameter (E150).
    std::string softknob_target(const std::string &path, int part);

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
    AutomationQueue automation_{AUTOMATION_RATE};
    RemoteDevice dev_, ed_;
    std::unique_ptr<AccessFiles> files_;
    EditorLayout layout_;
    std::unique_ptr<Conditions> cond_;
    std::mutex values_mu_;
    std::map<int, std::map<std::string, int>> part_values_; // decoded part dumps + controllers, per part (-1 = global)
    std::map<std::string, int> ui_state_;                   // editor-only parameters (VirusControl/SelectedPage ...)
    std::mutex ui_state_mu_; // ui_state_ is used by the UI, worker (meta conditions) and sync threads
    bool ui_state_get(const std::string &path, int &v)
    {
        std::lock_guard<std::mutex> lk(ui_state_mu_);
        auto it = ui_state_.find(path);
        if (it == ui_state_.end())
            return false;
        v = it->second;
        return true;
    }
    bool ui_state_set(const std::string &path, int v) // false: unchanged
    {
        std::lock_guard<std::mutex> lk(ui_state_mu_);
        auto [it, added] = ui_state_.emplace(path, v);
        if (!added && it->second == v)
            return false;
        it->second = v;
        return true;
    }
    // meta parameters (editordevice.xml meta="true", e.g. filter link, FX switches, LFO clocks): per-part editor values
    // of "part all" editor parameters, meta values, last condition state of retrigger metas (values_mu_)
    std::map<std::pair<std::string, int>, int> ed_part_, meta_vals_;
    std::map<std::pair<std::string, int>, bool> meta_cond_;
    std::recursive_mutex meta_mu_;
    void meta_index();
    bool meta_indexed_ = false; // meta_mu_
    std::unordered_map<std::string, std::vector<std::pair<const Parameter *, const MetaClient *>>> meta_sources_;
    std::vector<const Parameter *> meta_retriggers_;
    int meta_depth_ = 0; // > 0 while the engine sets linked parameters (no LCD update for those)
    void meta_source(const std::string &path, int part, int value, bool full);
    void meta_set(const Parameter &m, int part, int mv, const std::string &except, bool full);
    void meta_retrigger(int part, bool full);
    void meta_init();
    bool meta_cond(const Parameter &m, int part);
    void set_editor_value(const std::string &path, int part, int value, bool persist = false);
    std::string lcd_param_, lcd_value_, lcd_part_, lcd_orig_; // the last edited parameter for the LCD
    void set_lcd(const std::string &param, const std::string &value, const std::string &part, const std::string &before);
    std::string search_;                                    // browser search text
    int results_return_ = 0;                                // browser 0's bank before the results
    std::vector<std::vector<uint8_t>> results_;             // all search results (banks_mu_)
    int results_page_ = 0;
    void show_results_page();
    std::map<std::pair<int, int>, std::array<int, 3>> cat_cache_; // (bank, program) -> checksum, category 1, 2
    std::atomic<bool> ui_dirty_{true}, lcd_dirty_{false};
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
    // a preset chosen on the Virus itself (it reports the part's Bank / Patch): like the original, 150 ms after the
    // last report the stored patch from the bank cache goes into that part's edit buffer (sender thread, idle hook)
    vti::PanelFollow follow_;
    void load_reported_presets();
    std::vector<std::vector<uint8_t>> startup_messages() const; // GlobalInit + ParamInit (+ USB IO configuration)
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
    std::atomic<bool> starting_{false}; // start-up messages + state on their way: note-ons are held back
    // the Virus answered the readiness probe (wait_virus_ready): it has worked through the start-up messages and the
    // state, so the timeline may anchor and send the sync ping
    std::atomic<bool> ready_{false};
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
    // R5 timing with the sync marker: an event at timeline position t sounds at ring frame t + lead() (S behind the
    // newest frame + A to reach the Virus in time), and process() reads the ring latency_frames() behind that, so the
    // host gets it exactly latency_frames() late in every session (reported as the plug-in latency)
    // A: time for a stamped event to reach the Virus. Linux 1024 (exact down to 768); Windows needs more (win11 VM:
    // 2048 had late notes, 3072 exact for 16/16) - close to the original's reported 3840 + block
#ifdef _WIN32
    static constexpr int TIMING_A_DEFAULT = 3072;
#else
    static constexpr int TIMING_A_DEFAULT = 1024;
#endif
    static int timing_a()
    {
        static const int a = getenv("VTI_NATIVE_A") ? atoi(getenv("VTI_NATIVE_A")) : TIMING_A_DEFAULT;
        return a;
    }
    static int timing_s() { static const int s = getenv("VTI_NATIVE_S") ? atoi(getenv("VTI_NATIVE_S")) : 5000; return s; }
    int64_t lead() const { return timing_s() + timing_a(); }
    // the read position stays block + 640 - |timeline jitter (<= ~400)| behind the newest frame
    // (+ the resampler's look-ahead when the host does not run at 44.1 kHz)
    int64_t latency_frames() const
    {
        return timing_a() + std::max(block_, 256) + 640 + (resampling() ? Resampler::HALF + 2 : 0);
    }
    bool resampling() const { return std::fabs(rate_ - 44100.0) > 0.5; }
    void read_resampled(float **o1, float **o2, int n, double ratio, int64_t newest, int64_t target);
public:
    Steinberg::uint32 PLUGIN_API getLatencySamples() override
    {
        return (Steinberg::uint32)std::lround(latency_frames() * rate_ / 44100.0);
    }
private:
    double rate_ = 44100;
    // the Virus timeline (docs/protocol.md "timing"): frames of the synth, stamped on every clock tick and event
    int64_t tl_ = 0;
    bool tl_valid_ = false, anchored_ = false;
    double next_tick_ = 0;
    double tl_acc_ = 0; // fractional Virus frames of the timeline (host rates other than 44.1 kHz)
    double stat_acc_ = 0; // the same for the host-frames statistic
    int64_t stream_start_ = -1;
    // host rate != 44.1 kHz: the Virus' audio through the resampler, read from a steered fractional position
    Resampler resampler_;
    std::vector<float> rs_src_[4];
    double rs_pos_ = 0;
    bool rs_valid_ = false;
    int generation_seen_ = 0; // DeviceOut::generation handled by process()
    bool device_seen_ = false; // the Virus was present once (worker thread)
    std::set<std::string> dump_paths_; // parameters stored in a part's single dump (the rest: single messages)
    std::unordered_map<std::string, size_t> state_index_; // "path#part" -> state record (state_mu_)
    const std::vector<uint8_t> *state_index_data_ = nullptr;
    size_t state_index_size_ = 0;
    std::chrono::steady_clock::time_point drain_next_{}; // worker: next automation drain
    std::map<Steinberg::Vst::ParamID, int> latest_;       // worker: coalesced automation values
    int state_part(const std::string &path, int part) const;
public:
    // parameters changed since the last call (the editor repaints linked controls while a knob is dragged)
    std::vector<std::string> take_changed(bool *overflow = nullptr)
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        std::vector<std::string> v(changed_.begin(), changed_.end());
        changed_.clear();
        if (overflow)
            *overflow = changed_overflow_;
        changed_overflow_ = false;
        return v;
    }
    // only parameter values changed (automation, Virus panel): the editor repaints the controls that show them
    bool values_changed() { return values_dirty_.exchange(false); }

private:
    std::set<std::string> changed_; // values_mu_
    bool changed_overflow_ = false; // values_mu_: more than 256 changes, the editor repaints everything
    std::atomic<bool> values_dirty_{false};
    bool state_loaded_ = false;                // setState or the default state was applied
    std::vector<std::vector<uint8_t>> default_; // a new instance's state (from the user's plug-in binary)

    void apply_state_to_params();
    void panel_message(const std::vector<uint8_t> &msg);
    void send_state();
    void set_part_value(int part, const std::string &path, int value); // keeps the part dump in state_ current
};

} // namespace vti
