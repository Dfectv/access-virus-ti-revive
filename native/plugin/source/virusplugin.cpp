// virusplugin.cpp - see virusplugin.h.
#include "virusplugin.h"
#include "access/accessfiles.h"
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
    cv_.notify_one();
}

bool DeviceOut::send_rt(const uint8_t *msg, size_t n)
{
    size_t w = rt_w_.load(std::memory_order_relaxed);
    if (n > sizeof rt_[0].data || w - rt_r_.load(std::memory_order_acquire) >= RT_SLOTS)
        return false;
    Slot &s = rt_[w % RT_SLOTS];
    s.len = (uint8_t)n;
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
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait_for(lk, std::chrono::seconds(2), [this] { return stop_.load(); });
                continue;
            }
        }
        std::vector<uint8_t> msg;
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
                rt_r_.store(r + 1, std::memory_order_release);
            } else if (!queue_.empty()) {
                msg = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (msg.empty())
            continue;
        // channel messages carry a time stamp: F5 lo hi on cable 5 = the synth frame (14 bit) at which the Virus
        // executes the next event (docs/protocol.md). Without one the Virus ignores notes. For now "now"; R5 adds
        // sample-accurate stamps from the block position.
        std::vector<uint8_t> pk;
        int64_t now = audio_ok_ ? audio_.newest_frame() : -1;
        if (getenv("VTI_NATIVE_DEBUG"))
            fprintf(stderr, "send %02x now %lld\n", msg.empty() ? 0 : msg[0], (long long)now);
        if (now >= 0 && !msg.empty() && msg[0] >= 0x80 && msg[0] < 0xF0)
            pk = usbmidi_pack({0xF5, (uint8_t)(now & 0x7F), (uint8_t)((now >> 7) & 0x7F)}, CABLE_TIME);
        auto ev = usbmidi_pack(msg, CABLE_PLUGIN);
        pk.insert(pk.end(), ev.begin(), ev.end());
        if (!broker_.write(EP_PLUGIN_OUT, pk.data(), pk.size())) {
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
    auto files = AccessFiles::open("", err);
    std::vector<AutomationEntry> autom;
    if (!files || !dev_.load(files->get("remotedevice.xml"), err) || !load_automation(files->get("automation.xml"), autom, err)) {
        // without the user's Access installation there is nothing to show; the host still gets a valid instance
        fprintf(stderr, "Virus TI native: %s\n", err.c_str());
        return kResultOk;
    }
    single_template_ = dev_.message("VirusTI_Singledump");
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
    // the original's start-up: GlobalInit (remote mode, cable numbers, clock sync, ...) + ParamInit, before any state
    for (auto &m : dev_.init_messages())
        out_->send(m);
    return kResultOk;
}

tresult PLUGIN_API VirusPlugin::terminate()
{
    out_.reset();
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API VirusPlugin::setActive(TBool state)
{
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
    if (!single_template_)
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    for (auto &r : state_)
        if (is_part_dump(r, part))
            dev_.update_dump(*single_template_, r, {{path, value}});
}

tresult PLUGIN_API VirusPlugin::process(ProcessData &data)
{
    // TODO(R3): realtime-safe queue instead of DeviceOut's mutex; sample-accurate timing with F5 stamps (R5)
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
                    out_->send_rt(m, 2);
                } else {
                    int pb = (int)std::lround(v * 16383);
                    uint8_t m[3] = {(uint8_t)(0xE0 | ch), (uint8_t)(pb & 0x7F), (uint8_t)(pb >> 7)};
                    out_->send_rt(m, 3);
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
                out_->send_rt(m, n);
            set_part_value(it->second.part, it->second.path, value); // TODO(R3): move off the audio thread
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
                out_->send_rt(m, 3);
            } else if (e.type == Event::kNoteOffEvent) {
                uint8_t m[3] = {(uint8_t)(0x80 | (e.noteOff.channel & 15)), (uint8_t)e.noteOff.pitch,
                                (uint8_t)std::lround(e.noteOff.velocity * 127)};
                out_->send_rt(m, 3);
            }
        }
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
            out_->audio().read(o1[0], o1[1], o2[0], o2[1], data.numSamples, 2 * block_ + 256);
    }
    return kResultOk;
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
        for (auto &r : state_)
            for (int p = 0; p < 16; p++)
                if (is_part_dump(r, p)) {
                    std::string err;
                    dev_.decode_dump(*single_template_, r, parts[p], err);
                }
    }
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
