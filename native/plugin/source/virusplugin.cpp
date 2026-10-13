// virusplugin.cpp - see virusplugin.h.
#include "virusplugin.h"
#include <ctime>
#include <set>
#include "viruseditor.h"
#include "access/accessfiles.h"
#include "access/firmware.h"
#include "access/library.h"
#include "base/source/fstreamer.h"
#include "device/usbmidi.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vti {

#ifdef VTI_NATIVE_DROPIN
const FUID kVirusTIClassId(0xB49F90D9, 0x735F4305, 0xAC5397BE, 0x8958CD31);
#else
const FUID kVirusTIClassId(0xB49F90D9, 0x735F4305, 0xAC5397BE, 0x8958CD31 ^ 0xA5);
#endif

static bool env_debug()
{
    static const bool on = getenv("VTI_NATIVE_DEBUG") != nullptr; // read once (called on hot paths)
    return on;
}

static bool env_panellog()
{
    static const bool on = getenv("VTI_NATIVE_PANELLOG") != nullptr; // read once (called on hot paths)
    return on;
}

static bool env_ping()
{
    static const bool on = getenv("VTI_NATIVE_PING") != nullptr; // read once (called on hot paths)
    return on;
}

static bool env_ping3072()
{
    static const bool on = getenv("VTI_NATIVE_PING3072") != nullptr; // read once (called on hot paths)
    return on;
}

static bool env_stats()
{
    static const bool on = getenv("VTI_NATIVE_STATS") != nullptr; // read once (called on hot paths)
    return on;
}

static const uint8_t EP_PLUGIN_OUT = 0x06, CABLE_PLUGIN = 2, CABLE_TIME = 5;
static const ParamID MIDI_MAP_BASE = 0x100000; // + channel * 0x1000 + 0x80 aftertouch / 0x81 pitch bend

// ---- DeviceOut ----------------------------------------------------------------------------------------------------

DeviceOut::DeviceOut() : worker_([this] { run(); }) {}

DeviceOut::~DeviceOut()
{
    stop_ = true;
    cv_.notify_all();
    worker_.join();
}

void DeviceOut::send(std::vector<uint8_t> msg)
{
    std::lock_guard<std::mutex> lk(mu_);
    queue_.push_back(std::move(msg));
    queued_++;
    cv_.notify_one();
}

bool DeviceOut::send_rt(const uint8_t *msg, size_t n, int64_t stamp)
{
    size_t w = rt_w_.load(std::memory_order_relaxed);
    if (n > sizeof rt_[0].data || w - rt_r_.load(std::memory_order_acquire) >= RT_SLOTS)
        return false;
    Slot &s = rt_[w % RT_SLOTS];
    s.len = (uint8_t)n;
    s.stamp = stamp;
    memcpy(s.data, msg, n);
    rt_w_.store(w + 1, std::memory_order_release);
    return true;
}

void DeviceOut::run()
{
    while (!stop_) {
        if (!open_) {
            std::string err;
            if (!rx_set_) {
                auto parser = std::make_shared<UsbMidiParser>([this](uint8_t ep, int cable, const std::vector<uint8_t> &m) {
                    if (ep == 0x86 && cable == 2 && panel_)
                        panel_(m);
                    else if (((ep == 0x86 && cable == 3) || (ep == 0x85 && cable == 1)) && reply_ && !m.empty() &&
                             m[0] == 0xF0)
                        reply_(m);
                });
                broker_.on_rx([parser](uint8_t ep, const uint8_t *d, size_t len) { parser->feed(ep, d, len); });
                rx_set_ = true;
            }
            open_ = broker_.open(err);
            if (open_ && !audio_ok_) { // the owner starts the Virus audio stream (Linux: shm ring)
#ifdef __APPLE__
                int r = 1; // macOS: the plug-in reads the Virus' Core Audio input itself (the broker carries MIDI)
#else
                int r = broker_.audio(true);
#endif
                audio_ok_ = r == 1 && audio_.open();
                fprintf(stderr, "Virus TI native: audio %s (%d)\n", audio_ok_ ? "streaming" : "not available", r);
            }
            if (!open_) {
                fprintf(stderr, "Virus TI native: %s\n", err.c_str());
                for (int i = 0; i < 200 && !stop_; i++) { // keep the state current while there is no broker
                    if (idle_)
                        idle_();
                    std::unique_lock<std::mutex> lk(mu_);
                    cv_.wait_for(lk, std::chrono::milliseconds(10), [this] { return stop_.load(); });
                }
                continue;
            }
        }
        std::vector<uint8_t> msg;
        int64_t stamp = -1;
        {
            std::unique_lock<std::mutex> lk(mu_);
            // process() wakes it once per block with events (wake_rt); the timeout is only a backstop and runs the idle
            // work (parameter drain) - before: a 1 ms poll, 1000 wake-ups/s even without anything to send
            cv_.wait_for(lk, std::chrono::milliseconds(5), [this] {
                return stop_ || !queue_.empty() || rt_r_.load() != rt_w_.load(std::memory_order_acquire);
            });
            if (stop_)
                break;
            size_t r = rt_r_.load(std::memory_order_relaxed);
            // events from process() first, but after 8 in a row one queued message (state, dumps, sync requests),
            // else dense MIDI starves the queue (stress test: queue stuck, bank sync timed out)
            bool rt = r != rt_w_.load(std::memory_order_acquire) && !(rt_run_ >= 8 && !queue_.empty());
            if (rt) {
                const Slot &sl = rt_[r % RT_SLOTS];
                msg.assign(sl.data, sl.data + sl.len);
                stamp = sl.stamp;
                rt_r_.store(r + 1, std::memory_order_release);
                rt_run_++;
            } else if (!queue_.empty()) {
                rt_run_ = 0;
                msg = std::move(queue_.front());
                queue_.pop_front();
                pending_send_ = true; // counted down after the write, so busy() covers the transfer
            }
        }
        if (idle_)
            idle_();
        // the Virus unplugged / switched off and back while the broker connection stays (user test): stop the audio,
        // and on its return let the plug-in send the start-up messages and its state again; the audio start retries
        if (int present = broker_.device_present() ? 1 : 0; present != present_seen_) {
            bool was = present_seen_ == 1;
            present_seen_ = present;
            if (!present) {
                audio_ok_ = false;
                audio_.close();
            } else
                generation++;
            if (device_ && (was || present))
                device_(present != 0);
        }
        if (env_stats() && std::chrono::steady_clock::now() >= stats_next_) { // endurance tests
            stats_next_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            int64_t newest = audio_ok_ ? audio_.newest_frame() : -1, pos = audio_ok_ ? audio_.position() : -1;
            fprintf(stderr, "stats: audio %d drops %lld distance %lld queued %d\n", (int)audio_ok_.load(),
                    (long long)(audio_ok_ ? audio_.drops() : -1), (long long)(newest >= 0 && pos >= 0 ? newest - pos : -1),
                    queued_.load());
        }
        // the broker may refuse the audio start right after another session ended (stress test: every second
        // quick re-open had no sound): retry every 500 ms while connected without audio
        if (!audio_ok_ && present_seen_ == 1 && std::chrono::steady_clock::now() >= audio_retry_) {
            audio_retry_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
#ifdef __APPLE__
            int r = 1;
#else
            int r = broker_.audio(true);
#endif
            audio_ok_ = r == 1 && audio_.open();
            if (audio_ok_)
                fprintf(stderr, "Virus TI native: audio streaming (retry)\n");
        }
        if (msg.empty())
            continue;
        // channel messages and clock ticks carry a time stamp: F5 lo hi on cable 5 = the frame on the Virus timeline
        // (docs/protocol.md "timing"); process() stamps its events, other threads use the latest timeline position
        std::vector<uint8_t> pk;
        if (stamp < 0)
            stamp = timeline.load();
        if (stamp < 0 && audio_ok_)
            stamp = audio_.newest_frame();
        if (env_debug())
            fprintf(stderr, "send %02x stamp %lld newest %lld\n", msg[0], (long long)stamp,
                    (long long)(audio_ok_ ? audio_.newest_frame() : -1));
        if (stamp >= 0 && ((msg[0] >= 0x80 && msg[0] < 0xF0) || msg[0] == 0xF8 || msg[0] == 0xFE || msg[0] == 0xF2 ||
                           (msg.size() == 11 && msg[6] == 0x73 && msg[8] == 0x22)))
            pk = usbmidi_pack({0xF5, (uint8_t)(stamp & 0x7F), (uint8_t)((stamp >> 7) & 0x7F)}, CABLE_TIME);
        auto ev = usbmidi_pack(msg, CABLE_PLUGIN);
        pk.insert(pk.end(), ev.begin(), ev.end());
        bool ok = broker_.write(EP_PLUGIN_OUT, pk.data(), pk.size());
        if (pending_send_) {
            pending_send_ = false;
            queued_--;
        }
        if (!ok) {
            audio_ok_ = false;
            audio_.close();
            broker_.close();
            open_ = false;
        }
    }
}

// ---- state container ("MIDI" + u32 BE length, records of u32 BE length + message) ----------------------------------

static std::vector<std::vector<uint8_t>> parse_container(const std::vector<uint8_t> &d)
{
    std::vector<std::vector<uint8_t>> out;
    if (d.size() < 8 || memcmp(d.data(), "MIDI", 4))
        return out;
    size_t end = std::min(d.size(), 8 + ((size_t)d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7]));
    for (size_t pos = 8; pos + 4 <= end;) {
        size_t n = (size_t)d[pos] << 24 | d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3];
        pos += 4;
        if (pos + n > end)
            break;
        out.emplace_back(d.begin() + pos, d.begin() + pos + n);
        pos += n;
    }
    return out;
}

static std::vector<uint8_t> build_container(const std::vector<std::vector<uint8_t>> &recs)
{
    std::vector<uint8_t> d = {'M', 'I', 'D', 'I', 0, 0, 0, 0};
    auto be32 = [&](size_t v) {
        for (int s = 24; s >= 0; s -= 8)
            d.push_back((uint8_t)(v >> s));
    };
    for (auto &r : recs) {
        be32(r.size());
        d.insert(d.end(), r.begin(), r.end());
    }
    size_t n = d.size() - 8;
    for (int i = 0; i < 4; i++)
        d[4 + i] = (uint8_t)(n >> (24 - 8 * i));
    return d;
}

static bool is_part_dump(const std::vector<uint8_t> &m, int part)
{
    return m.size() == 524 && m[0] == 0xF0 && m[6] == 0x10 && m[7] == 0x00 && m[8] == part;
}

// ---- VirusPlugin ----------------------------------------------------------------------------------------------------

tresult PLUGIN_API VirusPlugin::initialize(FUnknown *context)
{
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk)
        return r;
    // buses as the original: audio in "Dummy In" + "Sidechain", event in "Midi In" 16 ch, audio out "Out 1".."Out 3"
    addAudioInput(STR16("Dummy In"), SpeakerArr::kStereo);
    addAudioInput(STR16("Sidechain"), SpeakerArr::kStereo, BusTypes::kAux, 0);
    addEventInput(STR16("Midi In"), 16);
    addAudioOutput(STR16("Out 1"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Out 2"), SpeakerArr::kStereo, BusTypes::kMain, 0);
    addAudioOutput(STR16("Out 3"), SpeakerArr::kStereo, BusTypes::kMain, 0);

    std::string err;
    files_ = AccessFiles::open("", err);
    auto &files = files_;
    std::vector<AutomationEntry> autom;
    if (!files || !dev_.load(files->get("remotedevice.xml"), err) || !load_automation(files->get("automation.xml"), autom, err)) {
        // without the user's Access installation there is nothing to show; the host still gets a valid instance
        fprintf(stderr, "Virus TI native: %s\n", err.c_str());
        return kResultOk;
    }
    single_template_ = dev_.message("VirusTI_Singledump");
    ui_state_["VirusControl/InitialSyncProgress"] = 127; // exists before the sync thread updates it
    load_banks();
    if (ed_.load(files->get("editordevice.xml"), err) && layout_.load(files->get("editor.xml"), err))
        cond_ = std::make_unique<Conditions>(dev_, ed_);
    else
        fprintf(stderr, "Virus TI native: no editor (%s)\n", err.c_str());
    // a new instance's state like the original's: empty record, the fixed multi, 16 parts = the init patch,
    // Output Control 2/3 globals, 14 controllers x 16 channels (formats.md)
    auto multi = files->init_multi();
    auto init = files->init_single();
    if (single_template_ && init.size() == 524) { // which parameters a part's single dump carries
        std::map<std::string, int> v;
        std::string e;
        auto d = init;
        RemoteDevice::upgrade_single(d);
        if (dev_.decode_dump(*single_template_, d, v, e))
            for (auto &kv : v)
                dump_paths_.insert(kv.first);
    }
    if (multi.size() == 267 && init.size() == 524) {
        RemoteDevice::upgrade_single(init);
        default_.push_back({});
        auto smulti = multi; // the original's state keeps the multi with bytes 25..40 and the checksum zeroed
        std::fill(smulti.begin() + 25, smulti.begin() + 41, 0);
        smulti[265] = 0;
        default_.push_back(smulti);
        for (int p = 0; p < 16; p++) {
            auto d = init;
            d[7] = 0, d[8] = (uint8_t)p;
            unsigned a = 0;
            for (int i = 5; i < 265; i++)
                a += d[i];
            d[265] = a & 0x7F;
            for (int i = 265; i < 522; i++)
                a += d[i];
            d[522] = a & 0x7F;
            default_.push_back(d);
        }
        for (int i = 0; i < 2; i++)
            default_.push_back({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x2D, 0x02, 0xF7});
        static const uint8_t ccs[14][2] = {{1, 0},  {2, 0},   {3, 0},  {4, 0},  {6, 0},  {7, 100}, {8, 64},
                                           {9, 0},  {11, 127}, {12, 0}, {13, 0}, {14, 0}, {15, 0},   {16, 0}};
        for (auto &cc : ccs)
            for (int ch = 0; ch < 16; ch++)
                default_.push_back({(uint8_t)(0xB0 | ch), cc[0], cc[1]});
    }
    for (auto &e : autom) {
        char title[128], units[16];
        snprintf(title, sizeof title, "[%d] %s", e.part, automation_title(dev_, e).c_str());
        snprintf(units, sizeof units, "Part %02d/", e.part);
        parameters.addParameter(UString128(title), UString128(units), 0, 0.0, ParameterInfo::kCanAutomate, e.id);
        params_[(ParamID)e.id] = {e.path, e.part - 1};
        ids_[{e.path, e.part - 1}] = (ParamID)e.id;
    }
    for (int ch = 0; ch < 16; ch++) {
        parameters.addParameter(STR16("Aftertouch"), STR16("Aftertouch"), 0, 0.0, ParameterInfo::kCanAutomate,
                                MIDI_MAP_BASE + ch * 0x1000 + 0x80);
        parameters.addParameter(STR16("Pitchbend"), STR16("Pitchbend"), 0, 0.5, ParameterInfo::kCanAutomate,
                                MIDI_MAP_BASE + ch * 0x1000 + 0x81);
    }
    out_ = std::make_unique<DeviceOut>();
    out_->on_panel([this](const std::vector<uint8_t> &m) { panel_message(m); });
    out_->on_idle([this] { drain_param_updates(); });
    sync_ = std::make_unique<MemorySync>([this](const std::vector<uint8_t> &m) {
        out_->send(m);
        return true;
    });
    out_->on_reply([this](const std::vector<uint8_t> &m) { sync_->feed(m); });
    out_->on_device([this](bool present) { // worker thread
        if (!present) {
            set_lcd("Virus disconnected", std::string(), std::string(), std::string());
            return;
        }
        if (!device_seen_) { // the first connection: the start-up messages are already queued
            device_seen_ = true;
            return;
        }
        set_lcd("Virus reconnected", std::string(), std::string(), std::string());
        for (auto &m : dev_.init_messages())
            out_->send(m);
        send_state();
    });
    // the original's start-up: GlobalInit (remote mode, cable numbers, clock sync, ...) + ParamInit, before any state
    for (auto &m : dev_.init_messages())
        out_->send(m);
    return kResultOk;
}

tresult PLUGIN_API VirusPlugin::terminate()
{
    stop_warm_ = true;
    if (sync_thread_.joinable())
        sync_thread_.join(); // stop_warm_ also cancels the bank sync between two requests (<= ~1 s)
    out_.reset();
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API VirusPlugin::setActive(TBool state)
{
    if (state && sync_ && !sync_started_.exchange(true))
        sync_thread_ = std::thread([this] {
            sync_banks();
            warm_categories();
        });
    if (state && !state_loaded_ && !default_.empty()) { // new instance: the original's default project
        {
            std::lock_guard<std::mutex> lk(state_mu_);
            state_ = default_;
        }
        state_loaded_ = true;
        apply_state_to_params();
        send_state();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API VirusPlugin::setupProcessing(ProcessSetup &setup)
{
    block_ = setup.maxSamplesPerBlock;
    rate_ = setup.sampleRate;
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API VirusPlugin::canProcessSampleSize(int32 s) { return s == kSample32 ? kResultTrue : kResultFalse; }

tresult PLUGIN_API VirusPlugin::getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber ctl, ParamID &id)
{
    if (bus != 0 || channel < 0 || channel > 15 || (ctl != kAfterTouch && ctl != kPitchBend))
        return kResultFalse;
    id = MIDI_MAP_BASE + channel * 0x1000 + (ctl == kAfterTouch ? 0x80 : 0x81);
    return kResultTrue;
}

// the part slot of a decoded message: globals and "part none" parameters (selected part, ...) are part -1
int VirusPlugin::state_part(const std::string &path, int part) const
{
    const Parameter *p = dev_.find(path);
    return part > 15 || (p && p->part == "none") ? -1 : part;
}

void VirusPlugin::set_part_value(int part, const std::string &path, int value)
{
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        part_values_[part][path] = value;
        if (changed_.size() < 256)
            changed_.insert(path);
        else
            changed_overflow_ = true;
    }
    values_dirty_ = true; // a partial repaint of the controls showing it (was a full repaint per change)
    if (!single_template_)
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    if (state_.empty())
        state_ = default_;
    if (part >= 0 && part < 16 && dump_paths_.count(path)) { // inside the part's single dump
        for (auto &r : state_)
            if (is_part_dump(r, part))
                dev_.update_dump(*single_template_, r, {{path, value}});
        return;
    }
    // everything else (selected part, part volume/pan, globals) lives as single messages in the state: replace the
    // message of this parameter, else add one - otherwise a patch load or project reload brought the old value back
    // (user test: part 3 selected, load a patch -> back on part 1)
    auto msg = dev_.encode(path, value, part < 0 ? 0 : part);
    if (msg.empty())
        return;
    // index (path, part) -> record, rebuilt when the record list was replaced or grew (patch / project load); a
    // linear decode of all ~240 records per change cost ~45 us - with automation thousands of times per second
    if (state_index_data_ != state_.data() || state_index_size_ != state_.size()) {
        state_index_.clear();
        for (size_t i = 0; i < state_.size(); i++) {
            const auto &r = state_[i];
            if (r.empty() || r.size() == 524 || r.size() == 267 || (r.size() > 1 && r[1] == 0x7D))
                continue;
            for (auto &c : dev_.decode(r))
                state_index_[c.path + '#' + std::to_string(state_part(c.path, c.part))] = i;
        }
        state_index_data_ = state_.data();
        state_index_size_ = state_.size();
    }
    std::string key = path + '#' + std::to_string(part);
    auto it = state_index_.find(key);
    if (it != state_index_.end() && it->second < state_.size()) {
        state_[it->second] = msg;
        return;
    }
    state_.push_back(msg);
    state_index_[key] = state_.size() - 1;
    state_index_data_ = state_.data();
    state_index_size_ = state_.size();
}

static std::string own_cache_dir();

bool VirusPlugin::diag_enabled()
{
    static int on = -1;
    if (on < 0) {
        std::error_code ec;
        on = std::filesystem::exists(own_cache_dir() + "/../diag.on", ec) ? 1 : 0;
    }
    return on == 1;
}

void VirusPlugin::diag_report()
{
    static auto next = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    if (!diag_enabled() || std::chrono::steady_clock::now() < next)
        return;
    next += std::chrono::seconds(10);
    if (FILE *f = fopen((own_cache_dir() + "/../diag.log").c_str(), "a")) {
        fprintf(f, "%ld s: draws full %ld partial %ld, draw time %ld ms, dirty ticks %ld, process calls %ld max %ld us,"
                   " over block time %ld, audio %d drops %lld, queued %d\n",
                (long)std::time(nullptr), diag.full_draws.exchange(0), diag.part_draws.exchange(0),
                diag.draw_us.exchange(0) / 1000, diag.dirty_ticks.exchange(0), diag.process_calls.exchange(0),
                diag.process_max_us.exchange(0), diag.process_over.exchange(0),
                out_ ? (int)out_->audio_ready() : -1, (long long)(out_ && out_->audio_ready() ? out_->audio().drops() : -1),
                out_ ? (int)out_->busy() : -1);
        fclose(f);
    }
}

tresult PLUGIN_API VirusPlugin::process(ProcessData &data)
{
    struct ProcessTimer { // field diagnostics: duration of every process() call
        VirusPlugin *p;
        int samples;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~ProcessTimer()
        {
            long us = (long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
            p->diag.process_calls++;
            long m = p->diag.process_max_us.load();
            while (us > m && !p->diag.process_max_us.compare_exchange_weak(m, us)) {
            }
            if (us > (long)(samples * 1000000.0 / (p->rate_ > 0 ? p->rate_ : 44100)))
                p->diag.process_over++;
        }
    } process_timer{this, data.numSamples};
    if ((data.processContext && (data.processContext->state & ProcessContext::kPlaying)) ||
        (data.inputEvents && data.inputEvents->getEventCount() > 0))
        last_play_ms_.store(now_ms(), std::memory_order_relaxed);
    // the Virus timeline: block start = newest ring frame - S, continued by the block size, re-anchored on drift
    static const int S = getenv("VTI_NATIVE_S") ? atoi(getenv("VTI_NATIVE_S")) : 5000;
    const double ratio = 44100.0 / rate_; // Virus frames per host sample
    int64_t newest = out_ && out_->audio_ready() ? out_->audio().newest_frame() : -1;
    if (out_ && out_->generation.load() != generation_seen_) { // the Virus came back: a new audio stream
        generation_seen_ = out_->generation.load();
        tl_valid_ = false;
        stream_start_ = -1;
    }
    if (newest >= 0) {
        // anchor once the stream has run for ~0.5 s (the first blocks arrive in bursts), then follow drift gently:
        // one frame per block, re-anchor (new song position) only on a large jump (DAW or USB stall)
        if (stream_start_ < 0)
            stream_start_ = newest;
        int64_t target = newest - S, err = target - tl_;
        if (!tl_valid_ && out_->busy())
            stream_start_ = newest; // wait until the start-up messages and the state are through
        if (!tl_valid_ ? newest - stream_start_ > 22050 : std::llabs(err) > 8192) {
            if (env_stats())
                fprintf(stderr, "timeline %s: err %lld newest %lld block %d\n", tl_valid_ ? "re-anchor" : "anchor",
                        (long long)err, (long long)newest, data.numSamples);
            tl_ = target;
            next_tick_ = (double)tl_;
            tl_valid_ = true;
            anchored_ = true; // FE + song position at the new anchor, like the original's start
        } else if (tl_valid_ && std::llabs(err) > 256) {
            tl_ += err > 0 ? 1 : -1;
            next_tick_ += err > 0 ? 1 : -1;
        }
    }
    struct Ev {
        int64_t stamp;
        uint8_t n, m[16];
    };
    // automation takes at most 96 slots and clock/bend/aftertouch a few dozen, so notes always keep 100+ (a dropped
    // note-off would hang a note)
    constexpr int EV_MAX = 256;
    Ev evs[EV_MAX];
    int nev = 0;
    auto add = [&](int64_t stamp, const uint8_t *m, size_t n) {
        if (nev < EV_MAX && n <= 16) {
            evs[nev].stamp = stamp, evs[nev].n = (uint8_t)n;
            memcpy(evs[nev].m, m, n);
            nev++;
        }
    };
    int64_t block_frames = (int64_t)std::lround(data.numSamples * ratio);
    if (anchored_) {
        anchored_ = false;
        // the Virus anchors its timeline on the song position's arrival: stamping it M frames early puts every later
        // event M frames into the Virus' future, so the stamps are honoured (constant latency) instead of "late"
        static const int M = getenv("VTI_NATIVE_M") ? atoi(getenv("VTI_NATIVE_M")) : 1024;
        static const uint8_t fe = 0xFE;
        add(tl_ - M, &fe, 1);
        int spp = 0; // song position (sixteenths) from the host, else 0
        if (data.processContext && (data.processContext->state & ProcessContext::kProjectTimeMusicValid))
            spp = (int)std::max(0.0, data.processContext->projectTimeMusic * 4.0);
        uint8_t f2[3] = {0xF2, (uint8_t)(spp & 0x7F), (uint8_t)((spp >> 7) & 0x7F)};
        add(tl_ - M, f2, 3);
        if (env_ping()) { // the original's sync ping (73 00 22 00): the Virus marks it in input ch 0/1
            static const uint8_t ping[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7};
            add(env_ping3072() ? 3072 : tl_, ping, 11);
            fprintf(stderr, "Virus TI native: sync ping stamped %lld (newest %lld)\n", (long long)tl_, (long long)newest);
        }
    }
    if (tl_valid_) { // MIDI clock at the host tempo (120 BPM without a process context), 24 ticks per quarter
        double tempo = data.processContext && (data.processContext->state & ProcessContext::kTempoValid)
                           ? data.processContext->tempo
                           : 120.0;
        double per_tick = 44100.0 * 60.0 / (tempo * 24.0);
        static const uint8_t f8 = 0xF8;
        while (next_tick_ < (double)(tl_ + block_frames)) {
            add((int64_t)std::lround(next_tick_), &f8, 1);
            next_tick_ += per_tick;
        }
    }
    auto at = [&](int32 offset) { return tl_valid_ ? tl_ + (int64_t)std::lround(offset * ratio) : -1; };
    if (data.inputParameterChanges && out_) {
        for (int32 i = 0; i < data.inputParameterChanges->getParameterCount(); i++) {
            IParamValueQueue *q = data.inputParameterChanges->getParameterData(i);
            int32 offset;
            ParamValue v;
            if (!q || q->getPointCount() <= 0 || q->getPoint(q->getPointCount() - 1, offset, v) != kResultOk)
                continue;
            ParamID id = q->getParameterId();
            if (id >= MIDI_MAP_BASE) { // channel aftertouch / pitch bend of channel (id - base) / 0x1000
                uint8_t ch = (uint8_t)(((id - MIDI_MAP_BASE) >> 12) & 15);
                if ((id & 0xFFF) == 0x80) {
                    uint8_t m[2] = {(uint8_t)(0xD0 | ch), (uint8_t)std::lround(v * 127)};
                    add(at(offset), m, 2);
                } else {
                    int pb = (int)std::lround(v * 16383);
                    uint8_t m[3] = {(uint8_t)(0xE0 | ch), (uint8_t)(pb & 0x7F), (uint8_t)(pb >> 7)};
                    add(at(offset), m, 3);
                }
                continue;
            }
            auto it = params_.find(id);
            if (it == params_.end())
                continue;
            int value = (int)std::lround(v * 127); // the original's automation scale (docs/formats.md)
            if (const Parameter *p = dev_.find(it->second.path))
                value = std::min(std::max(value, p->min), p->max);
            // coalesce: only the latest value per parameter waits for the budget below
            if (it->second.pending < 0 && pending_count_ < PENDING_MAX)
                pending_ids_[(pending_head_ + pending_count_++) % PENDING_MAX] = id;
            it->second.pending = value;
            size_t w = pu_w_.load(std::memory_order_relaxed); // part dump update: off the audio thread
            if (w - pu_r_.load(std::memory_order_acquire) < PU_SLOTS) {
                pu_[w % PU_SLOTS] = {id, value};
                pu_w_.store(w + 1, std::memory_order_release);
            }
        }
    }
    // send waiting automation within the budget, oldest first (at the block start)
    automation_budget_ = std::min(64.0, automation_budget_ + AUTOMATION_RATE * data.numSamples / rate_);
    while (out_ && pending_count_ > 0 && automation_budget_ >= 1 && nev < 96) {
        ParamID id = pending_ids_[pending_head_];
        pending_head_ = (pending_head_ + 1) % PENDING_MAX;
        pending_count_--;
        auto it = params_.find(id);
        if (it == params_.end() || it->second.pending < 0)
            continue;
        int value = it->second.pending;
        it->second.pending = -1;
        uint8_t m[16];
        size_t n = dev_.encode_into(it->second.path, value, it->second.part, m, sizeof m);
        if (n)
            add(at(0), m, n);
        automation_budget_ -= 1;
    }
    if (data.inputEvents && out_) {
        for (int32 i = 0; i < data.inputEvents->getEventCount(); i++) {
            Event e;
            if (data.inputEvents->getEvent(i, e) != kResultOk)
                continue;
            if (e.type == Event::kNoteOnEvent && syncing_.load(std::memory_order_relaxed))
                continue; // the start-up sync has priority (the Virus delays notes while it answers dump requests)
            if (e.type == Event::kNoteOnEvent) {
                uint8_t m[3] = {(uint8_t)(0x90 | (e.noteOn.channel & 15)), (uint8_t)e.noteOn.pitch,
                                (uint8_t)std::max(1L, std::lround(e.noteOn.velocity * 127))};
                add(at(e.sampleOffset), m, 3);
                auto &mt = meter_[e.noteOn.channel & 15]; // MIDI activity meter: the highest velocity, decays in tick_meters
                if (mt.load(std::memory_order_relaxed) < m[2])
                    mt.store(m[2], std::memory_order_relaxed);
            } else if (e.type == Event::kNoteOffEvent) {
                uint8_t m[3] = {(uint8_t)(0x80 | (e.noteOff.channel & 15)), (uint8_t)e.noteOff.pitch,
                                (uint8_t)std::lround(e.noteOff.velocity * 127)};
                add(at(e.sampleOffset), m, 3);
            }
        }
    }
    // to the Virus in time order (clock ticks and events share the timeline); stable: at the same stamp automation
    // stays before the notes, so a note starts with the new values (insertion sort: stable, no allocation on the
    // audio thread, and the events come nearly in order)
    for (int i = 1; i < nev; i++)
        for (int j = i; j > 0 && evs[j].stamp < evs[j - 1].stamp; j--)
            std::swap(evs[j], evs[j - 1]);
    for (int i = 0; out_ && i < nev; i++)
        out_->send_rt(evs[i].m, evs[i].n, evs[i].stamp);
    if (out_ && nev > 0)
        out_->wake_rt();
    int64_t block_start = tl_;
    if (tl_valid_) {
        tl_ += block_frames;
        if (out_)
            out_->timeline = tl_;
    }
    // device -> host: knob moves on the Virus as output parameter changes (the host updates the controller)
    if (data.outputParameterChanges && pending_mu_.try_lock()) {
        for (auto &pc : pending_) {
            int32 idx;
            if (IParamValueQueue *q = data.outputParameterChanges->addParameterData(pc.first, idx))
                q->addPoint(0, pc.second, idx);
        }
        pending_.clear();
        pending_mu_.unlock();
    }
    // audio: Out 1 = USB 1, Out 2 = USB 2 of the Virus (broker ring), Out 3 silent for now
    for (int32 b = 0; b < data.numOutputs; b++)
        for (int32 c = 0; c < data.outputs[b].numChannels; c++)
            if (data.outputs[b].channelBuffers32 && data.outputs[b].channelBuffers32[c])
                memset(data.outputs[b].channelBuffers32[c], 0, sizeof(float) * data.numSamples);
    if (out_ && out_->audio_ready() && data.numOutputs >= 2 && data.outputs[0].numChannels >= 2 &&
        data.outputs[1].numChannels >= 2 && data.numSamples > 0) {
        float **o1 = data.outputs[0].channelBuffers32, **o2 = data.outputs[1].channelBuffers32;
        if (o1 && o2)
        {
            static const char *k = getenv("VTI_NATIVE_K"); // read at timeline + K (stamp-aligned audio), test
            if (k && tl_valid_)
                out_->audio().read_at(o1[0], o1[1], o2[0], o2[1], data.numSamples, block_start + atoi(k));
            else
                // at least 768 frames: with 64-sample blocks 2 * 64 + 256 gave a ring re-anchor (click) every ~30 s
                out_->audio().read(o1[0], o1[1], o2[0], o2[1], data.numSamples, std::max(2 * block_ + 256, 768));
        }
    }
    return kResultOk;
}

void VirusPlugin::drain_param_updates()
{
    // at most every 5 ms, and per parameter only the latest value (automation arrives up to every block for each
    // lane - the editor and the state only need the newest)
    auto now = std::chrono::steady_clock::now();
    if (now < drain_next_)
        return;
    drain_next_ = now + std::chrono::milliseconds(5);
    latest_.clear();
    for (size_t r = pu_r_.load(std::memory_order_relaxed); r != pu_w_.load(std::memory_order_acquire); r++) {
        const ParamUpdate u = pu_[r % PU_SLOTS];
        pu_r_.store(r + 1, std::memory_order_release);
        latest_[u.id] = u.value;
    }
    for (auto &kv : latest_) {
        auto it = params_.find(kv.first);
        if (it != params_.end())
            set_part_value(it->second.part, it->second.path, kv.second);
    }
}

IPlugView *PLUGIN_API VirusPlugin::createView(FIDString name)
{
    if (FIDStringsEqual(name, ViewType::kEditor) && files_ && cond_)
        return new VirusEditor(this);
    return nullptr;
}

int VirusPlugin::ui_value(const std::string &path, int part)
{
    int browser_bank;
    // the original's internal browser parameters: browser N shows RAM bank N until changed
    if (path.rfind("Browser ", 0) == 0 && path.find("/Selected Bank") != std::string::npos &&
        !ui_state_get(path, browser_bank))
        return path[8] - '0';
    const Parameter *param = cond_ ? cond_->find(path) : nullptr;
    if (param && param->part == "none")
        part = -1; // global parameters (Panel Part, globals) are not per part
    if (param && !dev_.find(path) && param->part == "all") { // per-part editor values and meta parameters
        std::lock_guard<std::mutex> lk(values_mu_);
        auto &store = param->meta ? meta_vals_ : ed_part_;
        auto it = store.find({path, part < 0 ? 0 : part});
        return it != store.end() ? it->second : param->def;
    }
    if (int v; ui_state_get(path, v))
        return v;
    // stored values outside the parameter's range (e.g. Vocoder/Bands 127 of 0..31 in old patches) are clamped like
    // the original shows them
    auto clamp = [param](int v) { return param && param->max > param->min ? std::max(param->min, std::min(param->max, v)) : v; };
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        for (int p : {part, -1}) {
            auto pv = part_values_.find(p);
            if (pv != part_values_.end()) {
                auto v = pv->second.find(path);
                if (v != pv->second.end())
                    return clamp(v->second);
            }
        }
    }
    return cond_ ? cond_->default_value(path) : 0;
}

std::string VirusPlugin::live_text(const std::string &param)
{
    if (param.rfind("Browser ", 0) == 0 && param.find("/Selected Bank") != std::string::npos) // the bank menu
        return bank_title(ui_value(param, -1));
    if (param == "Repository Actions/Category Filter") {
        int c = ui_value(param, -1);
        if (c == 0)
            return "";
        auto vl = dev_.valuelists.find("Val Name Categories");
        return vl != dev_.valuelists.end() ? category_name_() : std::to_string(c);
    }
    if (param == "Repository Actions/Search")
        return search();
    std::lock_guard<std::mutex> lk(values_mu_);
    if (param == "VirusControl/Display Parameter")
        return lcd_param_;
    if (param == "VirusControl/Display Value")
        return lcd_value_;
    if (param == "VirusControl/Display Part")
        return lcd_part_;
    if (param == "VirusControl/Display Original Value")
        return lcd_orig_;
    return {};
}

void VirusPlugin::ui_set(const std::string &path, int part, int value)
{
    if (!dev_.find(path) && !ed_.find(path)) { // the original's internal parameters (browser state)
        if (path == "Repository Actions/Category Filter") { // the original shows it on the LCD like a parameter
            std::string before = category_name_();
            ui_state_set(path, value);
            std::string name = path;
            std::replace(name.begin(), name.end(), '/', ' ');
            set_lcd(name, category_name_(), "Part " + std::to_string(ui_value(layout_.frame_parameter, -1) + 1), before);
            update_results();
            return;
        }
        ui_state_set(path, value);
        ui_dirty_ = true;
        return;
    }
    if (const Parameter *p = meta_depth_ ? nullptr : dev_.find(path)) { // LCD: the edited parameter (not linked ones)
        auto text = [&](int val) {
            std::string v = std::to_string(val);
            for (const RemoteDevice *d : {&dev_, &ed_}) {
                auto vl = d->valuelists.find(p->valuelist);
                if (vl != d->valuelists.end())
                    if (const Valuelist::Entry *en = vl->second.find(val))
                        if (!en->text.empty())
                            v = en->text;
            }
            return v;
        };
        std::string before = text(ui_value(path, part));
        set_lcd(p->displayname.empty() ? path : p->displayname, text(value),
                p->part == "none" ? std::string() : "Part " + std::to_string(part + 1), before);
    }
    if (const Parameter *p = cond_ ? cond_->find(path) : nullptr)
        if (p->part == "none")
            part = -1;
    if (const Parameter *ep = ed_.find(path); ep && !dev_.find(path)) {
        if (ep->meta) { // e.g. the LFO clock menus: set the meta value and pass it to its clients
            std::lock_guard<std::recursive_mutex> mk(meta_mu_);
            meta_set(*ep, part < 0 ? 0 : part, std::max(ep->min, std::min(ep->max, value)), std::string(), true);
            meta_retrigger(part < 0 ? 0 : part, true);
        } else if (ep->part == "all") { // per-part editor values (FX switches, link buttons, helpers)
            set_editor_value(path, part, value, true);
            std::lock_guard<std::recursive_mutex> mk(meta_mu_);
            meta_source(path, part < 0 ? 0 : part, value, true);
        } else
            ui_state_set(path, value); // editor state (page selection, ...)
        ui_dirty_ = true;
        return;
    }
    auto id = ids_.find({path, part});
    if (id != ids_.end()) { // automatable: through the host, which hands it to process()
        ParamValue n = value / 127.0;
        beginEdit(id->second);
        setParamNormalized(id->second, n);
        performEdit(id->second, n);
        endEdit(id->second);
    } else if (out_) { // not automatable (menus, globals): straight to the Virus
        auto msg = dev_.encode(path, value, part < 0 ? 0 : part);
        if (!msg.empty())
            out_->send(msg);
    }
    set_part_value(part, path, value);
    if (env_debug())
        fprintf(stderr, "ui_set %s part %d = %d -> parts 0..2: %d %d %d\n", path.c_str(), part, value,
                ui_value(path, 0), ui_value(path, 1), ui_value(path, 2));
    if (part >= 0) { // linked parameters (filter link, FX switches, LFO clocks)
        std::lock_guard<std::recursive_mutex> mk(meta_mu_);
        meta_source(path, part, value, true);
    }
}

// ---- meta parameters (editordevice.xml): a source client sets the meta value (client value -> meta through the
// maps or (v - b) / a), the meta passes it to its trigger clients (maps or a * meta + b) while its condition holds;
// "retrigger" metas fire again when their condition becomes true (FX switch on/off, LFO clock switch).
// full = false (changes reported by the Virus, loaded states): only editor-side clients are updated.

// editor-only per-part settings (LFO link buttons, ...) have no Virus message: they are kept in the state as
// F0 7D 'V' 'T' 'N' 01 <part> <path ASCII> 00 <value> F7 (7D = non-commercial ID: the original and the Virus ignore it)
static std::vector<uint8_t> editor_record(const std::string &path, int part, int value)
{
    std::vector<uint8_t> m = {0xF0, 0x7D, 'V', 'T', 'N', 0x01, (uint8_t)(part & 0x7F)};
    for (char ch : path)
        m.push_back((uint8_t)ch & 0x7F);
    m.push_back(0);
    m.push_back((uint8_t)(value & 0x7F));
    m.push_back(0xF7);
    return m;
}

static bool parse_editor_record(const std::vector<uint8_t> &m, std::string &path, int &part, int &value)
{
    if (m.size() < 11 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 'V' || m[3] != 'T' || m[4] != 'N' || m[5] != 0x01)
        return false;
    part = m[6];
    size_t i = 7;
    path.clear();
    while (i < m.size() && m[i] != 0)
        path += (char)m[i++];
    if (i + 2 >= m.size())
        return false;
    value = m[i + 1];
    return true;
}

void VirusPlugin::set_editor_value(const std::string &path, int part, int value, bool persist)
{
    part = part < 0 ? 0 : part;
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        ed_part_[{path, part}] = value;
        if (changed_.size() < 256)
            changed_.insert(path);
        else
            changed_overflow_ = true;
        values_dirty_ = true;
    }
    // persist the user's own settings (link buttons, clicked in the editor); values the meta engine derives from
    // the patch (Filter Link from Filters/Select, FX switches) are not stored
    const Parameter *p = ed_.find(path);
    if (!persist || !p || p->group != "VirusControl Links")
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    if (state_.empty())
        state_ = default_;
    auto rec = editor_record(path, part, value);
    for (auto &r : state_) {
        std::string rp;
        int rpart, rv;
        if (parse_editor_record(r, rp, rpart, rv) && rp == path && rpart == part) {
            r = rec;
            return;
        }
    }
    state_.push_back(rec);
}

bool VirusPlugin::meta_cond(const Parameter &m, int part)
{
    if (m.condition.empty() || !cond_)
        return true;
    return cond_->eval(m.condition, [this](const std::string &p, int pt) { return ui_value(p, pt); }, part);
}

static int meta_to_client(const MetaClient &c, int mv)
{
    for (auto &m : c.maps)
        if (m.second == mv)
            return m.first;
    return (int)std::lround(c.a * mv + c.b);
}

static bool client_to_meta(const MetaClient &c, int cv, int &mv)
{
    for (auto &m : c.maps)
        if (m.first == cv)
            return mv = m.second, true;
    if (!c.maps.empty() || c.a == 0)
        return false;
    mv = (int)std::lround((cv - c.b) / c.a);
    return true;
}

void VirusPlugin::meta_set(const Parameter &m, int part, int mv, const std::string &except, bool full)
{
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        meta_vals_[{m.path, part}] = mv;
    }
    if (meta_depth_ > 6 || !meta_cond(m, part))
        return;
    meta_depth_++;
    for (auto &c : m.clients) {
        if (c.notrigger || c.path == except)
            continue;
        const Parameter *cp = cond_ ? cond_->find(c.path) : nullptr;
        int cv = meta_to_client(c, mv);
        if (cp)
            cv = std::max(cp->min, std::min(cp->max, cv));
        if (ui_value(c.path, part) == cv)
            continue;
        if (dev_.find(c.path)) {
            if (full)
                ui_set(c.path, part, cv); // to the Virus (and on to further metas)
            else
                set_part_value(part, c.path, cv);
        } else if (cp && cp->meta) {
            meta_set(*cp, part, cv, std::string(), full);
        } else {
            set_editor_value(c.path, part, cv);
            meta_source(c.path, part, cv, full);
        }
    }
    meta_depth_--;
}

void VirusPlugin::meta_source(const std::string &path, int part, int value, bool full)
{
    if (meta_depth_ > 6)
        return;
    meta_index();
    auto src = meta_sources_.find(path);
    if (src != meta_sources_.end())
        for (auto [m, c] : src->second) {
            if (!m->catch_always && !meta_cond(*m, part))
                continue;
            int mv;
            if (!client_to_meta(*c, value, mv))
                continue;
            mv = std::max(m->min, std::min(m->max, mv));
            meta_set(*m, part, mv, path, full);
        }
    meta_retrigger(part, full);
}

// path -> (meta, client) of the source clients, and the retrigger metas (meta_mu_; built once - meta_source ran
// through all editor-device parameters on every change before)
void VirusPlugin::meta_index()
{
    if (meta_indexed_)
        return;
    for (auto &m : ed_.parameters) {
        if (!m.meta)
            continue;
        for (auto &c : m.clients)
            if (!c.nosource)
                meta_sources_[c.path].push_back({&m, &c});
        if (m.retrigger)
            meta_retriggers_.push_back(&m);
    }
    meta_indexed_ = true;
}

void VirusPlugin::meta_retrigger(int part, bool full)
{
    meta_index();
    for (const Parameter *mp : meta_retriggers_) {
        const Parameter &m = *mp;
        bool now = meta_cond(m, part), before;
        {
            std::lock_guard<std::mutex> lk(values_mu_);
            auto it = meta_cond_.find({m.path, part});
            before = it == meta_cond_.end() ? now : it->second;
            meta_cond_[{m.path, part}] = now;
        }
        if (now && !before)
            meta_set(m, part, ui_value(m.path, part), std::string(), full);
    }
}

// after loading a state: meta values from their first source client, passed to the editor-side clients only
void VirusPlugin::meta_init()
{
    std::lock_guard<std::recursive_mutex> mk(meta_mu_);
    for (int part = 0; part < 16; part++) {
        for (auto &m : ed_.parameters) {
            if (!m.meta || (!m.catch_always && !meta_cond(m, part))) // e.g. "Trigger On" keeps its default (64)
                continue;
            for (auto &c : m.clients) {
                int mv;
                if (c.nosource || !client_to_meta(c, ui_value(c.path, part), mv))
                    continue;
                meta_set(m, part, std::max(m.min, std::min(m.max, mv)), c.path, false);
                break;
            }
        }
        for (auto &m : ed_.parameters) // the retrigger metas start from the loaded state's conditions
            if (m.meta && m.retrigger) {
                bool c = meta_cond(m, part);
                std::lock_guard<std::mutex> lk(values_mu_);
                meta_cond_[{m.path, part}] = c;
            }
    }
}

// our own bank cache: Linux ~/.local/share/virus-ti-native/cache, Mac ~/Library/Application Support/virus-ti-native/
// cache, Windows %LOCALAPPDATA%\virus-ti-native\cache
static std::string own_cache_dir()
{
#ifdef _WIN32
    if (const char *la = getenv("LOCALAPPDATA"))
        return std::string(la) + "/virus-ti-native/cache";
#elif defined(__APPLE__)
    if (const char *home = getenv("HOME"))
        return std::string(home) + "/Library/Application Support/virus-ti-native/cache";
#else
    if (const char *home = getenv("HOME"))
        return std::string(home) + "/.local/share/virus-ti-native/cache";
#endif
    return {};
}

// The Virus' RAM banks: our cache (tools/virussync, later the plug-in's own sync), else the original's Access cache
// (read only) - Windows/Wine: ProgramData/Access Music/Virus TI/Cache/<device>, Mac: /Library/Application Support/...
void VirusPlugin::load_banks()
{
    for (auto &lib : load_libraries(library_dir(), files_ ? files_->default_page_b() : std::vector<uint8_t>())) {
        banks_[LIB_BASE + (int)lib_names_.size()] = lib.dumps;
        lib_names_.push_back(lib.name);
        lib_paths_.push_back(lib.path);
    }
    // like the original: only the plug-in's own cache; without one the first start syncs the whole RAM
    std::string d = own_cache_dir();
    if (d.empty())
        return;
    for (int b = 0; b < 4; b++) {
        Bank bank;
        if (load_bank_file(d + "/RAM " + (char)('A' + b), bank))
            banks_[b].assign(bank.begin(), bank.end());
    }
}

void VirusPlugin::load_rom_banks()
{
    // the model decides the firmware image: bcdDevice 0x100 = TI (Snow if the product string says so), 0x300 = TI2
    VirusModel model = VirusModel::TI2;
    uint8_t dd[18] = {0};
    if (out_ && out_->control(0x80, 6, 0x0100, 0, dd, sizeof dd) >= 14) {
        uint16_t bcd = (uint16_t)(dd[12] | dd[13] << 8);
        std::string product;
        uint8_t sd[256] = {0};
        int n = dd[15] ? out_->control(0x80, 6, (uint16_t)(0x0300 | dd[15]), 0x0409, sd, 255) : -1;
        for (int i = 2; i + 1 < n; i += 2)
            product += (char)sd[i];
        model = bcd >= 0x300 ? VirusModel::TI2 : product.find("Snow") != std::string::npos ? VirusModel::Snow : VirusModel::TI;
        model_ = model == VirusModel::TI2 ? 0 : model == VirusModel::TI ? 1 : 2;
        fprintf(stderr, "Virus TI native: device bcdDevice %04x \"%s\"\n", bcd, product.c_str());
    }
    std::string err;
    auto roms = vti::load_rom_banks(firmware_path(files_ ? files_->source() : std::string()), model, err);
    if (roms.empty()) {
        fprintf(stderr, "Virus TI native: no ROM banks (%s)\n", err.c_str());
        return;
    }
    std::lock_guard<std::mutex> lk(banks_mu_);
    for (size_t i = 0; i < roms.size(); i++)
        banks_[4 + (int)i] = std::move(roms[i]);
    ui_dirty_ = true;
}

void VirusPlugin::set_sync_progress(int v)
{
    const std::string path = "VirusControl/InitialSyncProgress";
    if (ui_state_set(path, v))
        ui_dirty_ = true;
}

bool VirusPlugin::wait_until_quiet()
{
    bool shown = false;
    while (!stop_warm_ && now_ms() - last_play_ms_.load(std::memory_order_relaxed) < 2000) {
        if (!shown) {
            set_lcd("Bank sync paused (playing)", std::string(), std::string(), std::string());
            shown = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return !stop_warm_;
}

void VirusPlugin::sync_banks()
{
    for (int i = 0; i < 40 && !stop_warm_; i++) // 4 s: after the start-up messages and the state
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (stop_warm_ || !out_ || !out_->connected())
        return;
    std::string dir = own_cache_dir();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    load_rom_banks();
    // like the original: the sync has priority - its start-up page ("Initializing Virus TI and syncing memory",
    // VirusControl/InitialSyncProgress 0..126) covers the editor and notes are held back until it is done
    set_sync_progress(0);
    syncing_ = true;
    // the original keeps "synchronising" on the Virus display while it syncs (74 01 7E 07 about once a second)
    std::atomic<bool> rise{true};
    std::thread riser([this, &rise] {
        while (rise) {
            out_->send({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x07, 0xF7});
            for (int i = 0; i < 10 && rise; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    for (int b = 0; b < 4; b++) {
        Bank bank;
        {
            std::lock_guard<std::mutex> lk(banks_mu_);
            auto it = banks_.find(b);
            if (it != banks_.end())
                for (size_t p = 0; p < 128 && p < it->second.size(); p++)
                    bank[p] = it->second[p];
        }
        int n = sync_->sync_bank(b + 1, bank, [this, b, &bank](int p, bool fetched) {
            set_sync_progress((b * 128 + p + 1) * 126 / 512);
            {
                std::lock_guard<std::mutex> lk(values_mu_); // progress on the LCD
                lcd_param_ = "Synchronizing " + bank_name(b);
                lcd_value_ = std::to_string(p + 1) + " / 128";
                lcd_part_.clear();
                lcd_orig_.clear();
            }
            if (fetched) {
                std::lock_guard<std::mutex> lk(banks_mu_);
                auto &v = banks_[b];
                v.resize(128);
                v[p] = bank[p];
                ui_dirty_ = true; // a new patch name in the browser
            } else
                lcd_dirty_ = true; // only the progress on the LCD (a full repaint costs ~77 ms in REAPER)
        }, &stop_warm_);
        fprintf(stderr, "Virus TI native: RAM %c sync: %d fetched\n", 'A' + b, n);
        if (n == -2 && !dir.empty()) // closed while syncing: keep what was fetched, the next start continues
            save_bank_file(dir + "/RAM " + (char)('A' + b), bank);
        if (n < 0)
            break;
        {
            std::lock_guard<std::mutex> lk(banks_mu_);
            banks_[b].assign(bank.begin(), bank.end());
        }
        if (!dir.empty())
            save_bank_file(dir + "/RAM " + (char)('A' + b), bank);
        ui_dirty_ = true;
    }
    rise = false;
    riser.join();
    syncing_ = false;
    set_sync_progress(127);
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        lcd_param_ = "Synchronization done";
        lcd_value_.clear();
        lcd_orig_.clear();
    }
    ui_dirty_ = true;
}

std::string VirusPlugin::bank_patch(int bank, int program)
{
    std::lock_guard<std::mutex> lk(banks_mu_);
    auto b = banks_.find(bank);
    if (b == banks_.end() || program < 0 || program >= (int)b->second.size() || b->second[program].size() != 524)
        return {};
    return std::string(b->second[program].begin() + 249, b->second[program].begin() + 259);
    // (library banks hold fewer than 128 patches: the rest of the browser stays empty)
}

bool VirusPlugin::load_patch(int bank, int program, int part)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(bank);
        if (b == banks_.end() || program < 0 || program >= (int)b->second.size() || b->second[program].size() != 524 ||
            part < 0 || part > 15)
            return false;
        d = b->second[program];
    }
    return load_dump(d, part);
}

// a part's patch onto another part (drag and drop between the part strips, like the original)
bool VirusPlugin::copy_part(int from, int to)
{
    if (from == to || from < 0 || from > 15 || to < 0 || to > 15)
        return false;
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, from))
                d = r;
    }
    return d.size() == 524 && load_dump(d, to);
}

bool VirusPlugin::load_dump(std::vector<uint8_t> d, int part)
{
    if (d.size() != 524 || part < 0 || part > 15)
        return false;
    RemoteDevice::upgrade_single(d);
    d[7] = 0, d[8] = (uint8_t)part; // edit buffer of the part
    unsigned a = 0;
    for (int i = 5; i < 265; i++)
        a += d[i];
    d[265] = a & 0x7F;
    for (int i = 265; i < 522; i++)
        a += d[i];
    d[522] = a & 0x7F;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, part))
                r = d;
    }
    if (out_)
        out_->send(d);
    apply_state_to_params(); // parameters + editor values of the new patch
    return true;
}

bool VirusPlugin::rename_part(int part, const std::string &name)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, part)) {
                for (int i = 0; i < 10; i++) { // printable ASCII, padded with spaces
                    char ch = i < (int)name.size() ? name[i] : ' ';
                    r[249 + i] = (uint8_t)(ch >= 32 && ch < 127 ? ch : ' ');
                }
                unsigned a = 0;
                for (int i = 5; i < 265; i++)
                    a += r[i];
                r[265] = a & 0x7F;
                for (int i = 265; i < 522; i++)
                    a += r[i];
                r[522] = a & 0x7F;
                d = r;
            }
    }
    if (d.empty())
        return false;
    if (out_)
        out_->send(d);
    apply_state_to_params();
    return true;
}

bool VirusPlugin::store_patch(int part, int bank, int program)
{
    if (bank < 0 || bank > 3 || program < 0 || program > 127)
        return false;
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, part))
                d = r;
    }
    if (d.empty())
        return false;
    d[7] = (uint8_t)(bank + 1), d[8] = (uint8_t)program; // a dump into RAM bank 1..4 = store
    unsigned a = 0;
    for (int i = 5; i < 265; i++)
        a += d[i];
    d[265] = a & 0x7F;
    for (int i = 265; i < 522; i++)
        a += d[i];
    d[522] = a & 0x7F;
    if (out_)
        out_->send(d);
    Bank cache;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto &b = banks_[bank];
        b.resize(128);
        b[program] = d;
        for (int p = 0; p < 128; p++)
            cache[p] = b[p];
    }
    std::string dir = own_cache_dir();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        save_bank_file(dir + "/RAM " + (char)('A' + bank), cache);
    }
    ui_dirty_ = true;
    return true;
}

bool VirusPlugin::store_to_library(int part, int library, int slot)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, part))
                d = r;
    }
    if (d.empty())
        return false;
    PatchLibrary lib;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        if (library < 0 || library >= (int)lib_paths_.size())
            return false;
        auto &b = banks_[LIB_BASE + library];
        if (slot < 0 || slot > (int)b.size() || slot >= 128)
            return false;
        if (slot == (int)b.size())
            b.push_back(d);
        else
            b[slot] = d;
        lib.name = lib_names_[library], lib.path = lib_paths_[library], lib.dumps = b;
    }
    std::string err;
    bool ok = save_library(lib, err);
    if (!ok)
        fprintf(stderr, "Virus TI native: %s\n", err.c_str());
    ui_dirty_ = true;
    return ok;
}

int VirusPlugin::tick_meters(int step)
{
    int changed = 0;
    for (int ch = 0; ch < 16; ch++) {
        int v = meter_[ch].load(std::memory_order_relaxed);
        if (v != meter_shown_[ch])
            changed |= 1 << ch;
        meter_shown_[ch] = v;
        if (v > 0) // the original lowers every channel by 1 per editor idle tick
            meter_[ch].compare_exchange_strong(v, std::max(0, v - step), std::memory_order_relaxed);
    }
    return changed;
}

// the LCD: "[original value]" is the value the parameter had when its editing started (a new parameter on the LCD)
void VirusPlugin::set_lcd(const std::string &param, const std::string &value, const std::string &part,
                          const std::string &before)
{
    std::lock_guard<std::mutex> lk(values_mu_);
    if (param != lcd_param_ || part != lcd_part_)
        lcd_orig_ = "[" + before + "]";
    lcd_param_ = param, lcd_value_ = value, lcd_part_ = part;
    lcd_dirty_ = true; // only the LCD (value changes set ui_dirty_ themselves)
}

void VirusPlugin::set_search(const std::string &s)
{
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        search_ = s;
    }
    update_results();
}

// like the original (repository "Search Results", search://): a search text and/or a category collect the matching
// patches of all banks, sorted by name (byte order, duplicates kept), in pages of 128 shown in the upper browser
// (browser 0); cleared, browser 0 returns to the bank it showed before
void VirusPlugin::update_results()
{
    int cat = ui_value("Repository Actions/Category Filter", -1);
    std::string q = search();
    bool active = cat != 0 || !q.empty();
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        for (auto &kv : banks_)
            if (kv.first != SEARCH_BANK)
                ids.push_back(kv.first);
    }
    // copies of the same sound in several banks count once: key = page A without its first two bytes (dump bytes
    // 11..264; checked against the original's results: RAM + ROM + all libraries, 70 piano patches, same doubles)
    std::vector<std::vector<uint8_t>> found;
    std::set<std::string> seen;
    for (int b = 0; active && b < (int)ids.size(); b++)
        for (int p = 0; p < 128; p++) {
            if (!patch_matches(ids[b], p, cat, q))
                continue;
            std::lock_guard<std::mutex> lk(banks_mu_);
            auto it = banks_.find(ids[b]);
            if (it == banks_.end() || p >= (int)it->second.size() || it->second[p].size() != 524)
                continue;
            const auto &d = it->second[p];
            if (seen.insert(std::string(d.begin() + 11, d.begin() + 265)).second)
                found.push_back(d);
        }
    std::stable_sort(found.begin(), found.end(), [](const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
        return std::lexicographical_compare(a.begin() + 249, a.begin() + 259, b.begin() + 249, b.begin() + 259);
    });
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        results_ = std::move(found);
        results_page_ = 0;
        show_results_page();
        if (!active)
            banks_.erase(SEARCH_BANK);
    }
    const std::string b0 = "Browser 0/Selected Bank";
    int shown = ui_value(b0, -1);
    if (active && shown != SEARCH_BANK) {
        results_return_ = shown;
        ui_state_set(b0, SEARCH_BANK);
    } else if (!active && shown == SEARCH_BANK)
        ui_state_set(b0, results_return_);
    ui_dirty_ = true;
}

void VirusPlugin::show_results_page() // banks_mu_ held
{
    int n = (int)results_.size(), from = results_page_ * 128;
    banks_[SEARCH_BANK].assign(results_.begin() + std::min(n, from), results_.begin() + std::min(n, from + 128));
}

bool VirusPlugin::results_step(int dir)
{
    std::lock_guard<std::mutex> lk(banks_mu_);
    int pages = std::max(1, ((int)results_.size() + 127) / 128), p = results_page_ + dir;
    if (p < 0 || p >= pages)
        return false;
    results_page_ = p;
    show_results_page();
    ui_dirty_ = true;
    return true;
}

std::string VirusPlugin::results_title()
{
    int cat = ui_value("Repository Actions/Category Filter", -1);
    std::string q = search(), t;
    if (!q.empty() && cat)
        t = "Name contains \"" + q + "\" and category is \"" + category_name_() + "\"";
    else if (!q.empty())
        t = "Name contains \"" + q + "\"";
    else
        t = "Category is \"" + category_name_() + "\"";
    std::lock_guard<std::mutex> lk(banks_mu_);
    int pages = std::max(1, ((int)results_.size() + 127) / 128);
    return "Search Results/" + t + " (page " + std::to_string(results_page_ + 1) + " of " + std::to_string(pages) + ")";
}

// decode the categories of all stored patches in the background (after the bank sync), so the first category
// filter does not stall the editor (~11000 patches with the factory backups)
void VirusPlugin::warm_categories()
{
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        for (auto &kv : banks_)
            if (kv.first != SEARCH_BANK)
                ids.push_back(kv.first);
    }
    auto t0 = std::chrono::steady_clock::now();
    for (int b : ids)
        for (int p = 0; p < 128 && !stop_warm_; p++)
            patch_matches(b, p, 1, std::string());
    if (env_debug())
        fprintf(stderr, "Virus TI native: categories of %zu banks decoded in %.0f ms\n", ids.size(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

std::string VirusPlugin::category_name_()
{
    int cat = ui_value("Repository Actions/Category Filter", -1);
    auto vl = ed_.valuelists.find("Val Name Category Filter");
    if (vl != ed_.valuelists.end())
        for (auto &en : vl->second.entries)
            if (en.index == cat)
                return en.text;
    return std::to_string(cat);
}

bool VirusPlugin::patch_matches(int bank, int program, int cat, const std::string &q)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(bank);
        if (b == banks_.end() || program >= (int)b->second.size())
            return false;
        d = b->second[program];
    }
    if (d.size() != 524)
        return false;
    if (cat && single_template_) { // categories of a stored patch, decoded once per dump (checksum)
        std::array<int, 3> c{-1, 0, 0};
        {
            std::lock_guard<std::mutex> lk(values_mu_);
            auto it = cat_cache_.find({bank, program});
            if (it != cat_cache_.end() && it->second[0] == d[522])
                c = it->second;
        }
        if (c[0] < 0) {
            std::map<std::string, int> v;
            std::string err;
            dev_.decode_dump(*single_template_, d, v, err);
            c = {d[522], v["Categories/Category 1"], v["Categories/Category 2"]};
            std::lock_guard<std::mutex> lk(values_mu_);
            cat_cache_[{bank, program}] = c;
        }
        // the filter menu lists the categories alphabetically ("Val Name Category Filter": index -> stored value)
        int want = cat;
        auto vl = ed_.valuelists.find("Val Name Category Filter");
        if (vl != ed_.valuelists.end())
            for (auto &en : vl->second.entries)
                if (en.index == cat && en.value >= 0)
                    want = en.value;
        if (c[1] != want && c[2] != want)
            return false;
    }
    if (!q.empty()) {
        std::string name(d.begin() + 249, d.begin() + 259), a = name, b = q;
        for (auto &ch : a)
            ch = (char)tolower(ch);
        for (auto &ch : b)
            ch = (char)tolower(ch);
        if (a.find(b) == std::string::npos)
            return false;
    }
    return true;
}

std::string VirusPlugin::patch_name(int part)
{
    std::string n;
    std::lock_guard<std::mutex> lk(values_mu_);
    auto p = part_values_.find(part);
    if (p == part_values_.end())
        return n;
    for (int i = 1; i <= 10; i++) {
        char k[48];
        snprintf(k, sizeof k, "Single Patch Name/Character %02d", i);
        auto c = p->second.find(k);
        n += c == p->second.end() ? ' ' : (char)c->second;
    }
    while (!n.empty() && n.back() == ' ')
        n.pop_back();
    return n;
}

void VirusPlugin::panel_message(const std::vector<uint8_t> &msg)
{
    if (env_panellog()) {
        static int n;
        if (n++ < 40) {
            fprintf(stderr, "panel:");
            for (auto b : msg)
                fprintf(stderr, " %02x", b);
            fprintf(stderr, "\n");
        }
    }
    for (auto &c : dev_.decode(msg)) {
        if (c.part > 15)
            continue; // globals are not plug-in parameters
        set_part_value(c.part, c.path, c.value);
        {
            std::lock_guard<std::recursive_mutex> mk(meta_mu_);
            meta_source(c.path, c.part, c.value, false); // e.g. the FX switches follow the send knobs on the Virus
        }
        auto it = ids_.find({c.path, c.part});
        if (it != ids_.end()) {
            std::lock_guard<std::mutex> lk(pending_mu_);
            pending_.push_back({it->second, c.value / 127.0});
        }
    }
}

void VirusPlugin::apply_state_to_params()
{
    if (!single_template_)
        return;
    std::map<int, std::map<std::string, int>> parts;
    std::vector<std::pair<std::pair<std::string, int>, int>> editor_vals;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        for (auto &r : state_) {
            bool dump = false;
            for (int p = 0; p < 16; p++)
                if (is_part_dump(r, p)) {
                    std::string err;
                    dev_.decode_dump(*single_template_, r, parts[p], err);
                    dump = true;
                }
            std::string ep;
            int epart, ev;
            if (!dump && parse_editor_record(r, ep, epart, ev)) { // editor-only settings (link buttons)
                editor_vals.push_back({{ep, epart}, ev});
                continue;
            }
            if (!dump) // controllers and parameter SysEx of the state (volume, pan, globals)
                for (auto &c : dev_.decode(r))
                    parts[state_part(c.path, c.part)][c.path] = c.value;
        }
    }
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        part_values_ = parts;
        for (auto &kv : ed_part_) // link buttons come from the state (default off)
            if (const Parameter *p = ed_.find(kv.first.first); p && p->group == "VirusControl Links")
                kv.second = p->def;
        for (auto &e : editor_vals)
            ed_part_[e.first] = e.second;
    }
    ui_dirty_ = true;
    for (auto &kv : params_) {
        auto part = parts.find(kv.second.part);
        if (part == parts.end())
            continue;
        auto v = part->second.find(kv.second.path);
        if (v != part->second.end())
            setParamNormalized(kv.first, v->second / 127.0);
    }
    meta_init();
}

void VirusPlugin::send_state()
{
    if (!out_)
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    for (auto &r : state_)
        if (!r.empty() && !(r.size() > 2 && r[0] == 0xF0 && r[1] == 0x7D)) // not our editor-only records
            out_->send(r);
}

tresult PLUGIN_API VirusPlugin::setState(IBStream *state)
{
    std::vector<uint8_t> d;
    char buf[4096];
    int32 got = 0;
    while (state->read(buf, sizeof buf, &got) == kResultOk && got > 0)
        d.insert(d.end(), buf, buf + got);
    auto recs = parse_container(d);
    if (recs.empty())
        return kResultFalse;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        state_ = std::move(recs);
    }
    state_loaded_ = true;
    apply_state_to_params();
    send_state();
    return kResultOk;
}

tresult PLUGIN_API VirusPlugin::getState(IBStream *state)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        d = build_container(state_.empty() ? default_ : state_);
    }
    int32 done = 0;
    return state->write(d.data(), (int32)d.size(), &done);
}

} // namespace vti
