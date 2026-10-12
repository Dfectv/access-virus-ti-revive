// tirbridge_au.cpp - "Virus TI (TI Revive)" as an Audio Unit (AUv2 music device) for Logic Pro, GarageBand and other
// AU hosts (findings/design-macos-bridge.md). Same core as the VST3 (tirbridge_core.cpp): the original Access
// plug-in runs in the Rosetta helper; this file only translates the Audio Unit API. Written directly against
// AudioToolbox's AudioComponentPlugInInterface (no SDK). AUv2, not AUv3: an AUv3 app extension is sandboxed and
// could neither start the helper nor share its memory.
//
// Component: type 'aumu', subtype 'VTIr', manufacturer 'TIRv' (Info.plist AudioComponents, factory TIReviveVTIFactory)
#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioUnitUtilities.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <vector>

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "tirbridge_au.h"
#include "tirbridge_core.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

static const OSType kSubtype = 'VTIr', kManufacturer = 'TIRv';
enum { NUM_BUSES = 3, MAX_FRAMES = TIRB_MAX_BLOCK };

static std::string component_bundle_path() // .../Virus TI (TI Revive).component
{
    Dl_info di{};
    dladdr((void *)&component_bundle_path, &di);
    std::string p = di.dli_fname ? di.dli_fname : "";
    size_t k = p.rfind("/Contents/MacOS/");
    return k == std::string::npos ? "" : p.substr(0, k);
}

struct AUBridge;
struct AUHandle { // what the host holds: a pointer to its first member (plain struct, so the cast back is valid)
    AudioComponentPlugInInterface iface;
    AUBridge *owner;
};

struct AUBridge : BridgeCore {
    AUHandle handle{};
    AudioComponentInstance comp = nullptr;
    std::atomic<int> refs{1};
    bool initialized = false;
    int core_state = 0; // 0 not started, 1 ready, -1 failed
    std::mutex core_mx;
    Float64 sample_rate = 44100;
    UInt32 max_frames = 1156;
    HostCallbackInfo host{};
    struct Listener {
        AudioUnitPropertyID id;
        AudioUnitPropertyListenerProc proc;
        void *user;
    };
    std::vector<Listener> listeners;
    struct Notify {
        AURenderCallback proc;
        void *user;
    };
    std::vector<Notify> notifies;
    std::mutex pend_mx; // MIDI and parameter changes from any thread, applied in the next render
    std::vector<tirb_event> pend_ev;
    std::vector<tirb_pchange> pend_pc;
    std::vector<uint32_t> au_params; // ids exposed to the host (automatable ones)
    Float64 last_time = -1;
    bool last_ok = false;
    std::vector<float> out[NUM_BUSES * 2];
    AUPreset preset{-1, CFSTR("Untitled")};

    void hold() override { ++refs; }
    void unhold() override
    {
        if (--refs == 0)
            delete this;
    }

    bool ready() // lazily: hosts scan Audio Units, so the helper starts only when something needs the original
    {
        std::lock_guard<std::mutex> g(core_mx);
        if (core_state == 0) {
            core_state = init() ? 1 : -1;
            if (core_state == 1)
                for (const tirb_param &p : params)
                    if (p.flags & ParameterInfo::kCanAutomate)
                        au_params.push_back(p.id);
            blog("AU: core %s, %zu automatable parameters", core_state == 1 ? "ready" : "FAILED", au_params.size());
        }
        return core_state == 1;
    }

    void property_changed(AudioUnitPropertyID id, AudioUnitScope scope = kAudioUnitScope_Global,
                          AudioUnitElement elem = 0)
    {
        for (const Listener &l : std::vector<Listener>(listeners))
            if (l.id == id)
                l.proc(l.user, comp, id, scope, elem);
    }

    // ---------------------------------------------------------------- core notifications (main thread)
    void on_param_edit(const tirb_param_edit &e) override
    {
        AudioUnitEvent ev{};
        ev.mArgument.mParameter = {comp, e.id, kAudioUnitScope_Global, 0};
        ev.mEventType = e.what == 0   ? kAudioUnitEvent_BeginParameterChangeGesture
                        : e.what == 1 ? kAudioUnitEvent_ParameterValueChange
                                      : kAudioUnitEvent_EndParameterChangeGesture;
        AUEventListenerNotify(nullptr, nullptr, &ev);
    }
    void on_restart(int32_t flags) override
    {
        if (flags & kParamValuesChanged) {
            AudioUnitParameter p{comp, kAUParameterListener_AnyParameter, kAudioUnitScope_Global, 0};
            AUParameterListenerNotify(nullptr, nullptr, &p);
        }
        if (flags & kLatencyChanged)
            property_changed(kAudioUnitProperty_Latency);
        if (flags & kParamTitlesChanged)
            property_changed(kAudioUnitProperty_ParameterList);
    }

    // ---------------------------------------------------------------- MIDI (VST3 host conventions)
    void midi(UInt32 status, UInt32 d1, UInt32 d2, UInt32 offset)
    {
        int type = status & 0xf0, ch = status & 0x0f;
        tirb_event te{};
        te.sample_offset = (int32_t)offset;
        auto ctrl = [&](int ctl, double v) {
            auto it = midimap.find((uint32_t)ch << 16 | (uint16_t)ctl);
            if (it == midimap.end())
                return;
            std::lock_guard<std::mutex> g(pend_mx);
            pend_pc.push_back({it->second, (int32_t)offset, v});
        };
        if (type == 0x90 && d2) {
            NoteOnEvent n{(int16)ch, (int16)d1, 0.f, d2 / 127.f, 0, -1};
            te.type = Event::kNoteOnEvent;
            memcpy(te.data, &n, sizeof n);
        } else if (type == 0x80 || type == 0x90) {
            NoteOffEvent n{(int16)ch, (int16)d1, d2 / 127.f, -1, 0.f};
            te.type = Event::kNoteOffEvent;
            memcpy(te.data, &n, sizeof n);
        } else if (type == 0xa0) {
            PolyPressureEvent n{(int16)ch, (int16)d1, d2 / 127.f, -1};
            te.type = Event::kPolyPressureEvent;
            memcpy(te.data, &n, sizeof n);
        } else {
            if (type == 0xb0)
                ctrl((int)d1, d2 / 127.0);
            else if (type == 0xd0)
                ctrl(kAfterTouch, d1 / 127.0);
            else if (type == 0xe0)
                ctrl(kPitchBend, (double)((d2 << 7) | d1) / 16383.0);
            else if (type == 0xc0)
                ctrl(kCtrlProgramChange, d1 / 127.0);
            return;
        }
        std::lock_guard<std::mutex> g(pend_mx);
        pend_ev.push_back(te);
    }

    // ---------------------------------------------------------------- render
    void fill_transport(tirb_transport &t)
    {
        memset(&t, 0, sizeof t);
        t.sample_rate = sample_rate;
        Float64 beat = 0, tempo = 0;
        if (host.beatAndTempoProc && !host.beatAndTempoProc(host.hostUserData, &beat, &tempo)) {
            t.project_time_music = beat;
            t.tempo = tempo;
            t.state |= ProcessContext::kProjectTimeMusicValid | ProcessContext::kTempoValid;
        }
        UInt32 delta = 0;
        Float32 num = 0;
        UInt32 den = 0;
        Float64 downbeat = 0;
        if (host.musicalTimeLocationProc &&
            !host.musicalTimeLocationProc(host.hostUserData, &delta, &num, &den, &downbeat)) {
            t.time_sig_num = (int32_t)num;
            t.time_sig_den = (int32_t)den;
            t.bar_position_music = downbeat;
            t.state |= ProcessContext::kTimeSigValid | ProcessContext::kBarPositionValid;
        }
        Boolean playing = false, recording = false, changed = false, cycling = false;
        Float64 pos = 0, cs = 0, ce = 0;
        if (host.transportStateProc2 && !host.transportStateProc2(host.hostUserData, &playing, &recording, &changed,
                                                                  &pos, &cycling, &cs, &ce)) {
            t.project_time_samples = (int64_t)pos;
            t.cycle_start_music = cs;
            t.cycle_end_music = ce;
            t.state |= (playing ? ProcessContext::kPlaying : 0) | (recording ? ProcessContext::kRecording : 0) |
                       (cycling ? ProcessContext::kCycleActive | ProcessContext::kCycleValid : 0);
        }
    }

    // A host block goes to the original in one piece, with the host's maximum block size announced. The original
    // warns about sizes above 512 ("buffer size too high"), but it keeps MIDI and the Virus' real-time USB audio in
    // sync per block: splitting longer host blocks into 512-sample pieces processed back to back made playback in
    // GarageBand noticeably less tight (user, E111) - so no splitting.
    bool process(UInt32 frames)
    {
        tirb_shm *s = shm;
        if (!s || !ok)
            return false;
        {
            std::lock_guard<std::mutex> g(pend_mx);
            uint32_t ne = 0, np = 0;
            for (const tirb_event &e : pend_ev)
                if (ne < TIRB_MAX_EVENTS) {
                    s->events[ne] = e;
                    s->events[ne].sample_offset = std::min<int32_t>(std::max<int32_t>(e.sample_offset, 0), (int32_t)frames - 1);
                    ne++;
                }
            for (const tirb_pchange &p : pend_pc)
                if (np < TIRB_MAX_PCHANGES) {
                    s->pchanges[np] = p;
                    s->pchanges[np].sample_offset = std::min<int32_t>(std::max<int32_t>(p.sample_offset, 0), (int32_t)frames - 1);
                    np++;
                }
            pend_ev.clear();
            pend_pc.clear();
            std::stable_sort(s->events, s->events + ne,
                             [](const tirb_event &a, const tirb_event &b) { return a.sample_offset < b.sample_offset; });
            s->num_events = ne;
            s->num_pchanges = np;
        }
        fill_transport(s->transport);
        if (!run(frames))
            return false;
        for (int c = 0; c < NUM_BUSES * 2; c++)
            memcpy(out[c].data(), s->out[c], frames * sizeof(float));
        const float *main_out[2] = {out[0].data(), out[1].data()};
        note_output_level(main_out, 2, frames);
        return true;
    }

    OSStatus render(AudioUnitRenderActionFlags *flags, const AudioTimeStamp *ts, UInt32 bus, UInt32 frames,
                    AudioBufferList *io)
    {
        if (!initialized)
            return kAudioUnitErr_Uninitialized;
        if (bus >= NUM_BUSES)
            return kAudioUnitErr_InvalidElement;
        if (frames > max_frames)
            return kAudioUnitErr_TooManyFramesToProcess;
        AudioUnitRenderActionFlags pre = (flags ? *flags : 0) | kAudioUnitRenderAction_PreRender;
        for (const Notify &n : notifies)
            n.proc(n.user, &pre, ts, bus, frames, io);
        if (!(ts->mFlags & kAudioTimeStampSampleTimeValid) || ts->mSampleTime != last_time) {
            last_time = (ts->mFlags & kAudioTimeStampSampleTimeValid) ? ts->mSampleTime : -1;
            last_ok = process(frames);
        }
        for (UInt32 b = 0; b < io->mNumberBuffers && b < 2; b++) {
            AudioBuffer &ab = io->mBuffers[b];
            float *src = out[bus * 2 + b].data();
            if (!ab.mData)
                ab.mData = src; // host lets us provide the buffers
            else if (last_ok)
                memcpy(ab.mData, src, frames * sizeof(float));
            if (!last_ok)
                memset(ab.mData, 0, frames * sizeof(float));
            ab.mDataByteSize = frames * sizeof(float);
        }
        if (!last_ok && flags)
            *flags |= kAudioUnitRenderAction_OutputIsSilence;
        AudioUnitRenderActionFlags post = (flags ? *flags : 0) | kAudioUnitRenderAction_PostRender;
        for (const Notify &n : notifies)
            n.proc(n.user, &post, ts, bus, frames, io);
        return noErr;
    }

    // ---------------------------------------------------------------- properties
    static void stream_format(AudioStreamBasicDescription &f, Float64 rate)
    {
        f = {};
        f.mSampleRate = rate;
        f.mFormatID = kAudioFormatLinearPCM;
        f.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
        f.mBytesPerPacket = f.mBytesPerFrame = sizeof(float);
        f.mFramesPerPacket = 1;
        f.mChannelsPerFrame = 2;
        f.mBitsPerChannel = 32;
    }
    bool valid_element(AudioUnitScope scope, AudioUnitElement elem)
    {
        if (scope == kAudioUnitScope_Global)
            return elem == 0;
        if (scope == kAudioUnitScope_Output)
            return elem < NUM_BUSES;
        return false; // no inputs (instrument)
    }

    OSStatus property_info(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem, UInt32 *size,
                           Boolean *writable)
    {
        UInt32 sz = 0;
        Boolean w = false;
        switch (id) { // properties of the unit as a whole: only global scope, element 0 (auval)
        case kAudioUnitProperty_ClassInfo:
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
        case kAudioUnitProperty_MaximumFramesPerSlice:
        case kAudioUnitProperty_SupportedNumChannels:
        case kAudioUnitProperty_CocoaUI:
        case kAudioUnitProperty_HostCallbacks:
        case kAudioUnitProperty_LastRenderError:
        case kAudioUnitProperty_InPlaceProcessing:
        case kAudioUnitProperty_OfflineRender:
        case kAudioUnitProperty_PresentPreset:
        case kAudioUnitProperty_FactoryPresets:
        case kAudioUnitProperty_ContextName:
        case kTIRPropertyCore:
            if (scope != kAudioUnitScope_Global)
                return kAudioUnitErr_InvalidScope;
            if (elem != 0)
                return kAudioUnitErr_InvalidElement;
            break;
        }
        switch (id) {
        case kAudioUnitProperty_ClassInfo:
            sz = sizeof(CFPropertyListRef), w = true;
            break;
        case kAudioUnitProperty_SampleRate:
            if (!valid_element(scope, elem))
                return kAudioUnitErr_InvalidElement;
            sz = sizeof(Float64), w = true;
            break;
        case kAudioUnitProperty_StreamFormat:
            if (scope != kAudioUnitScope_Output && scope != kAudioUnitScope_Global)
                return kAudioUnitErr_InvalidScope;
            if (!valid_element(scope, elem))
                return kAudioUnitErr_InvalidElement;
            sz = sizeof(AudioStreamBasicDescription), w = true;
            break;
        case kAudioUnitProperty_ElementCount:
            sz = sizeof(UInt32);
            break;
        case kAudioUnitProperty_ParameterList:
            if (scope == kAudioUnitScope_Global && !ready())
                return kAudioUnitErr_FailedInitialization;
            sz = scope == kAudioUnitScope_Global ? (UInt32)(au_params.size() * sizeof(AudioUnitParameterID)) : 0;
            break;
        case kAudioUnitProperty_ParameterInfo:
            if (scope != kAudioUnitScope_Global)
                return kAudioUnitErr_InvalidScope;
            sz = sizeof(AudioUnitParameterInfo);
            break;
        case kAudioUnitProperty_ParameterStringFromValue:
            sz = sizeof(AudioUnitParameterStringFromValue);
            break;
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
            sz = sizeof(Float64);
            break;
        case kAudioUnitProperty_MaximumFramesPerSlice:
            sz = sizeof(UInt32), w = true;
            break;
        case kAudioUnitProperty_SupportedNumChannels:
            sz = sizeof(AUChannelInfo);
            break;
        case kAudioUnitProperty_CocoaUI:
            sz = sizeof(AudioUnitCocoaViewInfo);
            break;
        case kAudioUnitProperty_HostCallbacks:
            sz = sizeof(HostCallbackInfo), w = true;
            break;
        case kAudioUnitProperty_LastRenderError:
            sz = sizeof(OSStatus);
            break;
        case kAudioUnitProperty_ShouldAllocateBuffer:
        case kAudioUnitProperty_InPlaceProcessing:
        case kAudioUnitProperty_OfflineRender:
            sz = sizeof(UInt32), w = true;
            break;
        case kAudioUnitProperty_PresentPreset:
            sz = sizeof(AUPreset), w = true;
            break;
        case kAudioUnitProperty_FactoryPresets:
            sz = sizeof(CFArrayRef);
            break;
        case kAudioUnitProperty_ElementName:
            if (scope != kAudioUnitScope_Output || elem >= NUM_BUSES)
                return kAudioUnitErr_InvalidElement;
            sz = sizeof(CFStringRef);
            break;
        case kAudioUnitProperty_ContextName:
            sz = sizeof(CFStringRef), w = true;
            break;
        case kTIRPropertyCore:
            sz = sizeof(void *);
            break;
        default:
            return kAudioUnitErr_InvalidProperty;
        }
        if (size)
            *size = sz;
        if (writable)
            *writable = w;
        return noErr;
    }

    OSStatus get_property(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem, void *data,
                          UInt32 *size)
    {
        UInt32 need = 0;
        OSStatus st = property_info(id, scope, elem, &need, nullptr);
        if (st)
            return st;
        if (!data) {
            *size = need;
            return noErr;
        }
        if (*size < need && id != kAudioUnitProperty_ParameterList)
            return kAudioUnitErr_InvalidPropertyValue;
        switch (id) {
        case kAudioUnitProperty_ClassInfo: {
            std::vector<uint8_t> blob;
            if (!ready() || !get_state(blob))
                return kAudioUnitErr_FailedInitialization;
            CFMutableDictionaryRef d = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                                 &kCFTypeDictionaryValueCallBacks);
            auto num = [&](CFStringRef k, SInt32 v) {
                CFNumberRef n = CFNumberCreate(nullptr, kCFNumberSInt32Type, &v);
                CFDictionarySetValue(d, k, n);
                CFRelease(n);
            };
            num(CFSTR(kAUPresetVersionKey), 0);
            num(CFSTR(kAUPresetTypeKey), (SInt32)kAudioUnitType_MusicDevice);
            num(CFSTR(kAUPresetSubtypeKey), (SInt32)kSubtype);
            num(CFSTR(kAUPresetManufacturerKey), (SInt32)kManufacturer);
            CFDictionarySetValue(d, CFSTR(kAUPresetNameKey), preset.presetName ? preset.presetName : CFSTR("Untitled"));
            CFDataRef cd = CFDataCreate(nullptr, blob.data(), (CFIndex)blob.size());
            CFDictionarySetValue(d, CFSTR(kAUPresetDataKey), cd);
            CFRelease(cd);
            *(CFPropertyListRef *)data = d; // the caller releases it
            break;
        }
        case kAudioUnitProperty_SampleRate:
            *(Float64 *)data = sample_rate;
            break;
        case kAudioUnitProperty_StreamFormat:
            stream_format(*(AudioStreamBasicDescription *)data, sample_rate);
            break;
        case kAudioUnitProperty_ElementCount:
            *(UInt32 *)data = scope == kAudioUnitScope_Output ? NUM_BUSES : scope == kAudioUnitScope_Global ? 1 : 0;
            break;
        case kAudioUnitProperty_ParameterList: {
            if (scope != kAudioUnitScope_Global)
                break;
            UInt32 n = std::min<UInt32>(*size / sizeof(AudioUnitParameterID), (UInt32)au_params.size());
            memcpy(data, au_params.data(), n * sizeof(AudioUnitParameterID));
            *size = n * sizeof(AudioUnitParameterID);
            return noErr;
        }
        case kAudioUnitProperty_ParameterInfo: {
            int32_t i = ready() ? index_of(elem) : -1;
            if (i < 0)
                return kAudioUnitErr_InvalidParameter;
            const tirb_param &p = params[i];
            AudioUnitParameterInfo &pi = *(AudioUnitParameterInfo *)data;
            pi = {};
            strncpy(pi.name, p.title, sizeof pi.name - 1);
            pi.cfNameString = CFStringCreateWithCString(nullptr, p.title, kCFStringEncodingUTF8);
            pi.unit = kAudioUnitParameterUnit_Generic;
            pi.minValue = 0;
            pi.maxValue = 1;
            pi.defaultValue = (AudioUnitParameterValue)p.default_value;
            pi.flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable |
                       kAudioUnitParameterFlag_HasCFNameString | kAudioUnitParameterFlag_CFNameRelease |
                       kAudioUnitParameterFlag_ValuesHaveStrings;
            break;
        }
        case kAudioUnitProperty_ParameterStringFromValue: {
            AudioUnitParameterStringFromValue &sv = *(AudioUnitParameterStringFromValue *)data;
            std::string s;
            double v = sv.inValue ? *sv.inValue : value(sv.inParamID);
            sv.outString = ready() && param_string(sv.inParamID, v, s)
                               ? CFStringCreateWithCString(nullptr, s.c_str(), kCFStringEncodingUTF8)
                               : nullptr;
            break;
        }
        case kAudioUnitProperty_Latency:
            *(Float64 *)data = ok ? layout.latency / sample_rate : 0;
            break;
        case kAudioUnitProperty_TailTime:
            *(Float64 *)data = 0;
            break;
        case kAudioUnitProperty_MaximumFramesPerSlice:
            *(UInt32 *)data = max_frames;
            break;
        case kAudioUnitProperty_SupportedNumChannels: {
            AUChannelInfo ci{0, 2};
            memcpy(data, &ci, sizeof ci);
            break;
        }
        case kAudioUnitProperty_CocoaUI: {
            AudioUnitCocoaViewInfo &vi = *(AudioUnitCocoaViewInfo *)data;
            std::string path = component_bundle_path();
            vi.mCocoaAUViewBundleLocation = CFURLCreateFromFileSystemRepresentation(
                nullptr, (const UInt8 *)path.c_str(), (CFIndex)path.size(), true);
            vi.mCocoaAUViewClass[0] = CFStringCreateWithCString(nullptr, TIR_AU_VIEW_CLASS, kCFStringEncodingUTF8);
            break;
        }
        case kAudioUnitProperty_HostCallbacks:
            memcpy(data, &host, sizeof host);
            break;
        case kAudioUnitProperty_LastRenderError:
            *(OSStatus *)data = noErr;
            break;
        case kAudioUnitProperty_ShouldAllocateBuffer:
            *(UInt32 *)data = 1;
            break;
        case kAudioUnitProperty_InPlaceProcessing: // an instrument has no input to process in place
            *(UInt32 *)data = 0;
            break;
        case kAudioUnitProperty_OfflineRender:
            *(UInt32 *)data = 0;
            break;
        case kAudioUnitProperty_PresentPreset:
            *(AUPreset *)data = preset;
            if (preset.presetName)
                CFRetain(preset.presetName); // the caller releases the name
            break;
        case kAudioUnitProperty_FactoryPresets:
            *(CFArrayRef *)data = CFArrayCreate(nullptr, nullptr, 0, &kCFTypeArrayCallBacks);
            break;
        case kAudioUnitProperty_ElementName: {
            char n[40];
            snprintf(n, sizeof n, "Out %u", (unsigned)elem + 1);
            *(CFStringRef *)data = CFStringCreateWithCString(nullptr, n, kCFStringEncodingUTF8);
            break;
        }
        case kAudioUnitProperty_ContextName:
            *(CFStringRef *)data = nullptr;
            break;
        case kTIRPropertyCore:
            ready();
            *(void **)data = static_cast<BridgeCore *>(this);
            break;
        }
        *size = need;
        return noErr;
    }

    OSStatus set_property(AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement elem, const void *data,
                          UInt32 size)
    {
        switch (id) {
        case kAudioUnitProperty_ClassInfo: {
            if (!data || size < sizeof(CFPropertyListRef))
                return kAudioUnitErr_InvalidPropertyValue;
            CFDictionaryRef d = *(CFDictionaryRef *)data;
            if (!d || CFGetTypeID(d) != CFDictionaryGetTypeID())
                return kAudioUnitErr_InvalidPropertyValue;
            CFDataRef cd = (CFDataRef)CFDictionaryGetValue(d, CFSTR(kAUPresetDataKey));
            if (!cd || CFGetTypeID(cd) != CFDataGetTypeID())
                return kAudioUnitErr_InvalidPropertyValue;
            if (!ready() || !set_state(CFDataGetBytePtr(cd), (size_t)CFDataGetLength(cd)))
                return kAudioUnitErr_FailedInitialization;
            AudioUnitParameter p{comp, kAUParameterListener_AnyParameter, kAudioUnitScope_Global, 0};
            AUParameterListenerNotify(nullptr, nullptr, &p);
            return noErr;
        }
        case kAudioUnitProperty_SampleRate:
        case kAudioUnitProperty_StreamFormat: {
            if (!valid_element(scope, elem))
                return kAudioUnitErr_InvalidElement;
            Float64 r;
            if (id == kAudioUnitProperty_SampleRate) {
                if (size < sizeof(Float64))
                    return kAudioUnitErr_InvalidPropertyValue;
                r = *(const Float64 *)data;
            } else {
                if (size < sizeof(AudioStreamBasicDescription))
                    return kAudioUnitErr_InvalidPropertyValue;
                const AudioStreamBasicDescription &f = *(const AudioStreamBasicDescription *)data;
                AudioStreamBasicDescription ours;
                stream_format(ours, f.mSampleRate);
                if (f.mFormatID != ours.mFormatID || f.mChannelsPerFrame != 2 || f.mBitsPerChannel != 32 ||
                    !(f.mFormatFlags & kAudioFormatFlagIsFloat) || !(f.mFormatFlags & kAudioFormatFlagIsNonInterleaved))
                    return kAudioUnitErr_FormatNotSupported;
                r = f.mSampleRate;
            }
            if (initialized && r != sample_rate)
                return kAudioUnitErr_Initialized;
            if (r > 0 && r != sample_rate) {
                sample_rate = r;
                for (UInt32 b = 0; b < NUM_BUSES; b++)
                    property_changed(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, b);
            }
            return noErr;
        }
        case kAudioUnitProperty_MaximumFramesPerSlice: {
            if (size < sizeof(UInt32))
                return kAudioUnitErr_InvalidPropertyValue;
            UInt32 n = *(const UInt32 *)data;
            if (initialized && n != max_frames)
                return kAudioUnitErr_Initialized;
            if (!n || n > MAX_FRAMES)
                return kAudioUnitErr_InvalidPropertyValue;
            max_frames = n;
            property_changed(id);
            return noErr;
        }
        case kAudioUnitProperty_HostCallbacks:
            memset(&host, 0, sizeof host);
            memcpy(&host, data, std::min<UInt32>(size, sizeof host));
            return noErr;
        case kAudioUnitProperty_PresentPreset: {
            if (size < sizeof(AUPreset))
                return kAudioUnitErr_InvalidPropertyValue;
            const AUPreset &p = *(const AUPreset *)data;
            if (preset.presetName)
                CFRelease(preset.presetName);
            preset = p;
            if (preset.presetName)
                CFRetain(preset.presetName);
            return noErr;
        }
        case kAudioUnitProperty_ShouldAllocateBuffer:
        case kAudioUnitProperty_InPlaceProcessing:
        case kAudioUnitProperty_OfflineRender:
        case kAudioUnitProperty_ContextName:
            return noErr;
        }
        Boolean w = false;
        OSStatus st = property_info(id, scope, elem, nullptr, &w);
        return st ? st : kAudioUnitErr_PropertyNotWritable;
    }
};

static AUBridge *self_of(void *s) { return ((AUHandle *)s)->owner; }

// ------------------------------------------------------------------------------- the Audio Unit selectors
static OSStatus au_open(void *s, AudioComponentInstance ci)
{
    self_of(s)->comp = ci;
    return noErr;
}
static OSStatus au_close(void *s)
{
    AUBridge *b = self_of(s);
    if (b->core_state == 1)
        b->term();
    b->unhold();
    return noErr;
}
static OSStatus au_initialize(void *s)
{
    AUBridge *b = self_of(s);
    if (b->initialized)
        return noErr;
    if (!b->ready())
        return kAudioUnitErr_FailedInitialization;
    for (auto &v : b->out)
        v.assign(MAX_FRAMES, 0.f);
    b->setup(b->sample_rate, (int32_t)b->max_frames, 0 /* kRealtime */);
    b->set_active(true);
    b->last_time = -1;
    b->initialized = true;
    return noErr;
}
static OSStatus au_uninitialize(void *s)
{
    AUBridge *b = self_of(s);
    if (b->initialized)
        b->set_active(false);
    b->initialized = false;
    return noErr;
}
static OSStatus au_get_property_info(void *s, AudioUnitPropertyID id, AudioUnitScope sc, AudioUnitElement el,
                                     UInt32 *size, Boolean *w)
{
    return self_of(s)->property_info(id, sc, el, size, w);
}
static OSStatus au_get_property(void *s, AudioUnitPropertyID id, AudioUnitScope sc, AudioUnitElement el, void *data,
                                UInt32 *size)
{
    return self_of(s)->get_property(id, sc, el, data, size);
}
static OSStatus au_set_property(void *s, AudioUnitPropertyID id, AudioUnitScope sc, AudioUnitElement el,
                                const void *data, UInt32 size)
{
    return self_of(s)->set_property(id, sc, el, data, size);
}
static OSStatus au_add_listener(void *s, AudioUnitPropertyID id, AudioUnitPropertyListenerProc p, void *u)
{
    self_of(s)->listeners.push_back({id, p, u});
    return noErr;
}
static OSStatus au_remove_listener(void *s, AudioUnitPropertyID id, AudioUnitPropertyListenerProc p)
{
    auto &l = self_of(s)->listeners;
    l.erase(std::remove_if(l.begin(), l.end(), [&](const AUBridge::Listener &x) { return x.id == id && x.proc == p; }),
            l.end());
    return noErr;
}
static OSStatus au_remove_listener_ud(void *s, AudioUnitPropertyID id, AudioUnitPropertyListenerProc p, void *u)
{
    auto &l = self_of(s)->listeners;
    l.erase(std::remove_if(l.begin(), l.end(),
                           [&](const AUBridge::Listener &x) { return x.id == id && x.proc == p && x.user == u; }),
            l.end());
    return noErr;
}
static OSStatus au_add_render_notify(void *s, AURenderCallback p, void *u)
{
    self_of(s)->notifies.push_back({p, u});
    return noErr;
}
static OSStatus au_remove_render_notify(void *s, AURenderCallback p, void *u)
{
    auto &n = self_of(s)->notifies;
    n.erase(std::remove_if(n.begin(), n.end(), [&](const AUBridge::Notify &x) { return x.proc == p && x.user == u; }),
            n.end());
    return noErr;
}
static OSStatus au_get_parameter(void *s, AudioUnitParameterID id, AudioUnitScope sc, AudioUnitElement,
                                 AudioUnitParameterValue *v)
{
    AUBridge *b = self_of(s);
    if (sc != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;
    if (!b->ready() || b->index_of(id) < 0)
        return kAudioUnitErr_InvalidParameter;
    *v = (AudioUnitParameterValue)b->value(id);
    return noErr;
}
static OSStatus au_set_parameter(void *s, AudioUnitParameterID id, AudioUnitScope sc, AudioUnitElement,
                                 AudioUnitParameterValue v, UInt32 offset)
{
    AUBridge *b = self_of(s);
    if (sc != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;
    if (!b->ready() || b->index_of(id) < 0)
        return kAudioUnitErr_InvalidParameter;
    double nv = std::min(1.0, std::max(0.0, (double)v));
    b->set_value(id, nv);
    std::lock_guard<std::mutex> g(b->pend_mx);
    b->pend_pc.push_back({id, (int32_t)offset, nv});
    return noErr;
}
static OSStatus au_schedule_parameters(void *s, const AudioUnitParameterEvent *ev, UInt32 n)
{
    for (UInt32 i = 0; i < n; i++) {
        const AudioUnitParameterEvent &e = ev[i];
        if (e.eventType == kParameterEvent_Immediate)
            au_set_parameter(s, e.parameter, e.scope, e.element, e.eventValues.immediate.value,
                             e.eventValues.immediate.bufferOffset);
        else
            au_set_parameter(s, e.parameter, e.scope, e.element, e.eventValues.ramp.endValue,
                             (UInt32)std::max<SInt32>(0, e.eventValues.ramp.startBufferOffset));
    }
    return noErr;
}
static OSStatus au_render(void *s, AudioUnitRenderActionFlags *f, const AudioTimeStamp *ts, UInt32 bus, UInt32 n,
                          AudioBufferList *io)
{
    return self_of(s)->render(f, ts, bus, n, io);
}
static OSStatus au_reset(void *s, AudioUnitScope, AudioUnitElement)
{
    self_of(s)->last_time = -1;
    return noErr;
}
static OSStatus au_midi_event(void *s, UInt32 status, UInt32 d1, UInt32 d2, UInt32 offset)
{
    AUBridge *b = self_of(s);
    if (!b->ok)
        return kAudioUnitErr_Uninitialized;
    b->midi(status, d1, d2, offset);
    return noErr;
}
static OSStatus au_sysex(void *, const UInt8 *, UInt32) { return noErr; } // not used by the Virus TI plug-in

static AudioComponentMethod au_lookup(SInt16 sel)
{
    switch (sel) {
    case kAudioUnitInitializeSelect: return (AudioComponentMethod)au_initialize;
    case kAudioUnitUninitializeSelect: return (AudioComponentMethod)au_uninitialize;
    case kAudioUnitGetPropertyInfoSelect: return (AudioComponentMethod)au_get_property_info;
    case kAudioUnitGetPropertySelect: return (AudioComponentMethod)au_get_property;
    case kAudioUnitSetPropertySelect: return (AudioComponentMethod)au_set_property;
    case kAudioUnitAddPropertyListenerSelect: return (AudioComponentMethod)au_add_listener;
    case kAudioUnitRemovePropertyListenerSelect: return (AudioComponentMethod)au_remove_listener;
    case kAudioUnitRemovePropertyListenerWithUserDataSelect: return (AudioComponentMethod)au_remove_listener_ud;
    case kAudioUnitAddRenderNotifySelect: return (AudioComponentMethod)au_add_render_notify;
    case kAudioUnitRemoveRenderNotifySelect: return (AudioComponentMethod)au_remove_render_notify;
    case kAudioUnitGetParameterSelect: return (AudioComponentMethod)au_get_parameter;
    case kAudioUnitSetParameterSelect: return (AudioComponentMethod)au_set_parameter;
    case kAudioUnitScheduleParametersSelect: return (AudioComponentMethod)au_schedule_parameters;
    case kAudioUnitRenderSelect: return (AudioComponentMethod)au_render;
    case kAudioUnitResetSelect: return (AudioComponentMethod)au_reset;
    case kMusicDeviceMIDIEventSelect: return (AudioComponentMethod)au_midi_event;
    case kMusicDeviceSysExSelect: return (AudioComponentMethod)au_sysex;
    }
    return nullptr;
}

extern "C" __attribute__((visibility("default"))) void *TIReviveVTIFactory(const AudioComponentDescription *)
{
    AUBridge *b = new AUBridge;
    b->handle.iface.Open = au_open;
    b->handle.iface.Close = au_close;
    b->handle.iface.Lookup = au_lookup;
    b->handle.iface.reserved = nullptr;
    b->handle.owner = b;
    return &b->handle;
}
