// virusplugin.cpp - see virusplugin.h.
#include "virusplugin.h"
#include "viruseditor.h"
#include "access/accessfiles.h"
#include "access/firmware.h"
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
                int r = broker_.audio(true);
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
            // the realtime ring cannot signal; poll it every millisecond
            cv_.wait_for(lk, std::chrono::milliseconds(1), [this] {
                return stop_ || !queue_.empty() || rt_r_.load() != rt_w_.load(std::memory_order_acquire);
            });
            if (stop_)
                break;
            size_t r = rt_r_.load(std::memory_order_relaxed);
            if (r != rt_w_.load(std::memory_order_acquire)) { // events from process() first
                const Slot &sl = rt_[r % RT_SLOTS];
                msg.assign(sl.data, sl.data + sl.len);
                stamp = sl.stamp;
                rt_r_.store(r + 1, std::memory_order_release);
            } else if (!queue_.empty()) {
                msg = std::move(queue_.front());
                queue_.pop_front();
                pending_send_ = true; // counted down after the write, so busy() covers the transfer
            }
        }
        if (idle_)
            idle_();
        if (msg.empty())
            continue;
        // channel messages and clock ticks carry a time stamp: F5 lo hi on cable 5 = the frame on the Virus timeline
        // (docs/protocol.md "timing"); process() stamps its events, other threads use the latest timeline position
        std::vector<uint8_t> pk;
        if (stamp < 0)
            stamp = timeline.load();
        if (stamp < 0 && audio_ok_)
            stamp = audio_.newest_frame();
        if (getenv("VTI_NATIVE_DEBUG"))
            fprintf(stderr, "send %02x stamp %lld newest %lld\n", msg[0], (long long)stamp,
                    (long long)(audio_ok_ ? audio_.newest_frame() : -1));
        if (stamp >= 0 && ((msg[0] >= 0x80 && msg[0] < 0xF0) || msg[0] == 0xF8 || msg[0] == 0xFE || msg[0] == 0xF2))
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
    load_banks();
    if (ed_.load(files->get("editordevice.xml"), err) && layout_.load(files->get("editor.xml"), err))
        cond_ = std::make_unique<Conditions>(dev_, ed_);
    else
        fprintf(stderr, "Virus TI native: no editor (%s)\n", err.c_str());
    // a new instance's state like the original's: empty record, the fixed multi, 16 parts = the init patch,
    // Output Control 2/3 globals, 14 controllers x 16 channels (formats.md)
    auto multi = files->init_multi();
    auto init = files->init_single();
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
    // the original's start-up: GlobalInit (remote mode, cable numbers, clock sync, ...) + ParamInit, before any state
    for (auto &m : dev_.init_messages())
        out_->send(m);
    return kResultOk;
}

tresult PLUGIN_API VirusPlugin::terminate()
{
    if (sync_thread_.joinable())
        sync_thread_.join(); // TODO: make the sync cancellable (up to ~10 min on an empty cache)
    out_.reset();
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API VirusPlugin::setActive(TBool state)
{
    if (state && sync_ && !sync_started_.exchange(true))
        sync_thread_ = std::thread([this] { sync_banks(); });
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

void VirusPlugin::set_part_value(int part, const std::string &path, int value)
{
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        part_values_[part][path] = value;
    }
    ui_dirty_ = true;
    if (!single_template_)
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    for (auto &r : state_)
        if (is_part_dump(r, part))
            dev_.update_dump(*single_template_, r, {{path, value}});
}

tresult PLUGIN_API VirusPlugin::process(ProcessData &data)
{
    // the Virus timeline: block start = newest ring frame - S, continued by the block size, re-anchored on drift
    static const int S = getenv("VTI_NATIVE_S") ? atoi(getenv("VTI_NATIVE_S")) : 5000;
    const double ratio = 44100.0 / rate_; // Virus frames per host sample
    int64_t newest = out_ && out_->audio_ready() ? out_->audio().newest_frame() : -1;
    if (newest >= 0) {
        // anchor once the stream has run for ~0.5 s (the first blocks arrive in bursts), then follow drift gently:
        // one frame per block, re-anchor (new song position) only on a large jump (DAW or USB stall)
        if (stream_start_ < 0)
            stream_start_ = newest;
        int64_t target = newest - S, err = target - tl_;
        if (!tl_valid_ && out_->busy())
            stream_start_ = newest; // wait until the start-up messages and the state are through
        if (!tl_valid_ ? newest - stream_start_ > 22050 : std::llabs(err) > 8192) {
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
    Ev evs[128];
    int nev = 0;
    auto add = [&](int64_t stamp, const uint8_t *m, size_t n) {
        if (nev < 128 && n <= 16) {
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
            uint8_t m[16];
            size_t n = dev_.encode_into(it->second.path, value, it->second.part, m, sizeof m);
            if (n)
                add(at(offset), m, n);
            size_t w = pu_w_.load(std::memory_order_relaxed); // part dump update: off the audio thread
            if (w - pu_r_.load(std::memory_order_acquire) < PU_SLOTS) {
                pu_[w % PU_SLOTS] = {id, value};
                pu_w_.store(w + 1, std::memory_order_release);
            }
        }
    }
    if (data.inputEvents && out_) {
        for (int32 i = 0; i < data.inputEvents->getEventCount(); i++) {
            Event e;
            if (data.inputEvents->getEvent(i, e) != kResultOk)
                continue;
            if (e.type == Event::kNoteOnEvent) {
                uint8_t m[3] = {(uint8_t)(0x90 | (e.noteOn.channel & 15)), (uint8_t)e.noteOn.pitch,
                                (uint8_t)std::max(1L, std::lround(e.noteOn.velocity * 127))};
                add(at(e.sampleOffset), m, 3);
            } else if (e.type == Event::kNoteOffEvent) {
                uint8_t m[3] = {(uint8_t)(0x80 | (e.noteOff.channel & 15)), (uint8_t)e.noteOff.pitch,
                                (uint8_t)std::lround(e.noteOff.velocity * 127)};
                add(at(e.sampleOffset), m, 3);
            }
        }
    }
    // to the Virus in time order (clock ticks and events share the timeline)
    std::sort(evs, evs + nev, [](const Ev &a, const Ev &b) { return a.stamp < b.stamp; });
    for (int i = 0; out_ && i < nev; i++)
        out_->send_rt(evs[i].m, evs[i].n, evs[i].stamp);
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
                out_->audio().read(o1[0], o1[1], o2[0], o2[1], data.numSamples, 2 * block_ + 256);
        }
    }
    return kResultOk;
}

void VirusPlugin::drain_param_updates()
{
    for (size_t r = pu_r_.load(std::memory_order_relaxed); r != pu_w_.load(std::memory_order_acquire); r++) {
        const ParamUpdate u = pu_[r % PU_SLOTS];
        pu_r_.store(r + 1, std::memory_order_release);
        auto it = params_.find(u.id);
        if (it != params_.end())
            set_part_value(it->second.part, it->second.path, u.value);
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
    // the original's internal browser parameters: browser N shows RAM bank N until changed
    if (path.rfind("Browser ", 0) == 0 && path.find("/Selected Bank") != std::string::npos &&
        !ui_state_.count(path))
        return path[8] - '0';
    if (const Parameter *p = cond_ ? cond_->find(path) : nullptr)
        if (p->part == "none")
            part = -1; // global parameters (Panel Part, globals) are not per part
    {
        auto it = ui_state_.find(path);
        if (it != ui_state_.end())
            return it->second;
    }
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        for (int p : {part, -1}) {
            auto pv = part_values_.find(p);
            if (pv != part_values_.end()) {
                auto v = pv->second.find(path);
                if (v != pv->second.end())
                    return v->second;
            }
        }
    }
    return cond_ ? cond_->default_value(path) : 0;
}

std::string VirusPlugin::live_text(const std::string &param)
{
    if (param.rfind("Browser ", 0) == 0 && param.find("/Selected Bank") != std::string::npos) // the bank menu
        return "Virus TI/" + bank_name(ui_value(param, -1));
    if (param == "Repository Actions/Category Filter") {
        int c = ui_value(param, -1);
        if (c == 0)
            return "";
        auto vl = dev_.valuelists.find("Val Name Categories");
        return vl != dev_.valuelists.end() ? vl->second.text(c) : std::to_string(c);
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
    return {};
}

void VirusPlugin::ui_set(const std::string &path, int part, int value)
{
    if (!dev_.find(path) && !ed_.find(path)) { // the original's internal parameters (browser state)
        ui_state_[path] = value;
        ui_dirty_ = true;
        return;
    }
    if (const Parameter *p = dev_.find(path)) { // LCD: name, value text and part of the edited parameter
        std::string v = std::to_string(value);
        for (const RemoteDevice *d : {&dev_, &ed_}) {
            auto vl = d->valuelists.find(p->valuelist);
            if (vl != d->valuelists.end())
                for (auto &en : vl->second.entries)
                    if (en.index == value && !en.text.empty())
                        v = en.text;
        }
        std::lock_guard<std::mutex> lk(values_mu_);
        lcd_param_ = p->displayname.empty() ? path : p->displayname;
        lcd_value_ = v;
        lcd_part_ = p->part == "none" ? std::string() : "Part " + std::to_string(part + 1);
    }
    if (const Parameter *p = cond_ ? cond_->find(path) : nullptr)
        if (p->part == "none")
            part = -1;
    if (ed_.find(path) && !dev_.find(path)) { // editor state (page selection, ...)
        ui_state_[path] = value;
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

void VirusPlugin::sync_banks()
{
    std::this_thread::sleep_for(std::chrono::seconds(4)); // after the start-up messages and the state
    if (!out_ || !out_->connected())
        return;
    std::string dir = own_cache_dir();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    load_rom_banks();
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
            {
                std::lock_guard<std::mutex> lk(values_mu_); // progress on the LCD
                lcd_param_ = "Synchronizing " + bank_name(b);
                lcd_value_ = std::to_string(p + 1) + " / 128";
                lcd_part_.clear();
            }
            if (fetched) {
                std::lock_guard<std::mutex> lk(banks_mu_);
                auto &v = banks_[b];
                v.resize(128);
                v[p] = bank[p];
            }
            ui_dirty_ = true;
        });
        fprintf(stderr, "Virus TI native: RAM %c sync: %d fetched\n", 'A' + b, n);
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
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        lcd_param_ = "Synchronization done";
        lcd_value_.clear();
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

void VirusPlugin::set_search(const std::string &s)
{
    std::lock_guard<std::mutex> lk(values_mu_);
    search_ = s;
    ui_dirty_ = true;
}

bool VirusPlugin::browser_match(int bank, int program)
{
    int cat = ui_value("Repository Actions/Category Filter", -1);
    std::string q = search();
    if (cat == 0 && q.empty())
        return true;
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
        if (c[1] != cat && c[2] != cat)
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
    for (auto &c : dev_.decode(msg)) {
        if (c.part > 15)
            continue; // globals are not plug-in parameters
        set_part_value(c.part, c.path, c.value);
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
            if (!dump) // controllers and parameter SysEx of the state (volume, pan, globals)
                for (auto &c : dev_.decode(r))
                    parts[c.part > 15 ? -1 : c.part][c.path] = c.value;
        }
    }
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        part_values_ = parts;
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
}

void VirusPlugin::send_state()
{
    if (!out_)
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    for (auto &r : state_)
        if (!r.empty())
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
