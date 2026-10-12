// vtihelper.cpp - TI Revive macOS stage 2 helper (findings/design-macos-bridge.md): an x86_64 process (runs under
// Rosetta) that hosts the original Intel-only Access "Virus TI" VST3 for the native arm64 bridge plug-in.
//
// Step 1 (this file so far): probe mode. Loads the original like a host, prints its buses and parameters, then runs
// IAudioProcessor::process in real time for a few seconds with a test note, printing the output peak per second.
//
// Build (on the Mac; P = vst3sdk/pluginterfaces, MIT): clang++ -arch x86_64 -std=c++17 -O2 -I vst3sdk -o vtihelper \
//   vtihelper.cpp $P/base/funknown.cpp $P/base/coreiids.cpp -framework CoreFoundation
// usage: vtihelper --probe "/Library/Audio/Plug-Ins/VST3/Virus TI.vst3" [seconds]
#include <CoreFoundation/CoreFoundation.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <string>
#include <unistd.h>
#include <vector>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

// interface IDs used here (the SDK declares them; one translation unit defines them)
namespace Steinberg { namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
}}

static std::string utf8(const TChar *s)
{
    std::string r;
    for (; s && *s; s++)
        r += *s < 0x80 ? (char)*s : '?';
    return r;
}

// ------------------------------------------------------------------------------- minimal host-side objects
#define REF_COUNTING                                                                                                   \
    std::atomic<uint32> refs{1};                                                                                       \
    uint32 PLUGIN_API addRef() override { return ++refs; }                                                             \
    uint32 PLUGIN_API release() override { return --refs; } // owned by the helper, never deleted through release

struct Host : IHostApplication {
    REF_COUNTING
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IHostApplication::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            addRef();
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    tresult PLUGIN_API getName(String128 name) override
    {
        const char *n = "TI Revive";
        int i = 0;
        for (; n[i]; i++)
            name[i] = (TChar)n[i];
        name[i] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID, TUID, void **obj) override
    {
        *obj = nullptr;
        return kNotImplemented;
    }
};

struct ComponentHandler : IComponentHandler {
    REF_COUNTING
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            addRef();
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        printf("restartComponent 0x%x\n", flags);
        return kResultOk;
    }
};

struct EventList : IEventList {
    REF_COUNTING
    std::vector<Event> ev;
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IEventList::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            addRef();
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    int32 PLUGIN_API getEventCount() override { return (int32)ev.size(); }
    tresult PLUGIN_API getEvent(int32 i, Event &e) override
    {
        if (i < 0 || i >= (int32)ev.size())
            return kInvalidArgument;
        e = ev[i];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event &e) override
    {
        ev.push_back(e);
        return kResultOk;
    }
};

struct ParamChanges : IParameterChanges { // always empty in the probe
    REF_COUNTING
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IParameterChanges::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            addRef();
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    int32 PLUGIN_API getParameterCount() override { return 0; }
    IParamValueQueue *PLUGIN_API getParameterData(int32) override { return nullptr; }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &, int32 &) override { return nullptr; }
};

// ------------------------------------------------------------------------------- probe
static void audio_loop(IAudioProcessor *proc, ProcessData &d, ProcessContext &ctx, EventList &events,
                       std::vector<std::vector<float>> &outbuf, int nch, int block, int seconds, double rate);

static int probe(const char *path, int seconds)
{
    CFStringRef p = CFStringCreateWithCString(nullptr, path, kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, true);
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    if (!bundle || !CFBundleLoadExecutable(bundle)) {
        fprintf(stderr, "cannot load %s\n", path);
        return 1;
    }
    auto entry = (bool (*)(CFBundleRef))CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry"));
    if (entry)
        entry(bundle);
    auto getFactory = (IPluginFactory * (*)()) CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory"));
    IPluginFactory *f = getFactory ? getFactory() : nullptr;
    if (!f)
        return 1;
    static Host host;
    static ComponentHandler handler;
    PClassInfo ci{};
    int32 n = f->countClasses();
    int32 k = 0;
    for (; k < n; k++) {
        f->getClassInfo(k, &ci);
        printf("class %d \"%s\" (%s)\n", k, ci.name, ci.category);
        if (!strcmp(ci.category, kVstAudioEffectClass))
            break;
    }
    if (k == n)
        return 1;
    IComponent *comp = nullptr;
    if (f->createInstance(ci.cid, IComponent::iid, (void **)&comp) != kResultOk || !comp)
        return 1;
    printf("initialize -> %d\n", comp->initialize(&host));

    // buses
    for (int dir = 0; dir < 2; dir++)
        for (int type = 0; type < 2; type++) {
            int32 nb = comp->getBusCount(type, dir);
            for (int32 b = 0; b < nb; b++) {
                BusInfo bi{};
                comp->getBusInfo(type, dir, b, bi);
                printf("bus %s %s %d \"%s\" ch %d type %d flags 0x%x\n", type == kAudio ? "audio" : "event",
                       dir == kInput ? "in" : "out", b, utf8(bi.name).c_str(), bi.channelCount, bi.busType, bi.flags);
            }
        }

    // controller: single component or separate class
    IEditController *ctl = nullptr;
    if (comp->queryInterface(IEditController::iid, (void **)&ctl) != kResultOk || !ctl) {
        TUID cid;
        if (comp->getControllerClassId(cid) == kResultOk &&
            f->createInstance(cid, IEditController::iid, (void **)&ctl) == kResultOk && ctl) {
            printf("separate controller, initialize -> %d\n", ctl->initialize(&host));
        }
    } else
        printf("single-component controller\n");
    if (ctl) {
        ctl->setComponentHandler(&handler);
        int32 np = ctl->getParameterCount();
        printf("%d parameters\n", np);
        for (int32 i = 0; i < np; i++) {
            ParameterInfo pi{};
            ctl->getParameterInfo(i, pi);
            if (i < 12 || i == np - 1)
                printf("  param %d id %u \"%s\" steps %d flags 0x%x\n", i, pi.id, utf8(pi.title).c_str(),
                       pi.stepCount, pi.flags);
        }
    }

    IAudioProcessor *proc = nullptr;
    if (comp->queryInterface(IAudioProcessor::iid, (void **)&proc) != kResultOk || !proc)
        return 1;
    const int block = 512;
    const double rate = 44100;
    ProcessSetup setup{kRealtime, kSample32, block, rate};
    printf("setupProcessing -> %d\n", proc->setupProcessing(setup));
    int32 nout = comp->getBusCount(kAudio, kOutput), nin = comp->getBusCount(kAudio, kInput);
    for (int32 b = 0; b < nout; b++)
        comp->activateBus(kAudio, kOutput, b, b == 0);
    for (int32 b = 0; b < nin; b++)
        comp->activateBus(kAudio, kInput, b, false);
    if (comp->getBusCount(kEvent, kInput) > 0)
        comp->activateBus(kEvent, kInput, 0, true);
    printf("setActive -> %d\n", comp->setActive(true));
    printf("setProcessing -> %d\n", proc->setProcessing(true));

    BusInfo ob{};
    comp->getBusInfo(kAudio, kOutput, 0, ob);
    int nch = ob.channelCount > 0 ? ob.channelCount : 2;
    std::vector<std::vector<float>> outbuf(nch, std::vector<float>(block));
    std::vector<float *> outptr(nch);
    for (int c = 0; c < nch; c++)
        outptr[c] = outbuf[c].data();
    std::vector<AudioBusBuffers> outs(nout > 0 ? nout : 1);
    outs[0].numChannels = nch;
    outs[0].channelBuffers32 = outptr.data();
    for (int32 b = 1; b < nout; b++) // inactive buses still get a (null-channel) entry
        outs[b].numChannels = 0;
    static EventList events;
    static ParamChanges pin, pout;
    ProcessContext ctx{};
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid;
    ctx.sampleRate = rate;
    ctx.tempo = 120;
    ctx.timeSigNumerator = ctx.timeSigDenominator = 4;
    ProcessData d{};
    d.processMode = kRealtime;
    d.symbolicSampleSize = kSample32;
    d.numSamples = block;
    d.numOutputs = nout;
    d.outputs = outs.data();
    d.inputEvents = &events;
    d.inputParameterChanges = &pin;
    d.outputParameterChanges = &pout;
    d.processContext = &ctx;

    // The audio loop runs on its own thread like a DAW's audio thread; the main thread runs the CFRunLoop, which the
    // plug-in needs for its timers (MIDI to the device, device polling): without it notes never reach the Virus.
    struct Ctx {
        IAudioProcessor *proc;
        ProcessData *d;
        ProcessContext *ctx;
        EventList *events;
        std::vector<std::vector<float>> *outbuf;
        int nch, block, seconds;
        double rate;
        std::atomic<bool> done{false};
    } run{proc, &d, &ctx, &events, &outbuf, nch, block, seconds, rate};
    pthread_t th;
    pthread_create(&th, nullptr, [](void *a) -> void * {
        Ctx &r = *(Ctx *)a;
        audio_loop(r.proc, *r.d, *r.ctx, *r.events, *r.outbuf, r.nch, r.block, r.seconds, r.rate);
        r.done = true;
        CFRunLoopStop(CFRunLoopGetMain());
        return nullptr;
    }, &run);
    while (!run.done)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    pthread_join(th, nullptr);
    proc->setProcessing(false);
    comp->setActive(false);
    if (ctl && (void *)ctl != (void *)comp)
        ctl->terminate();
    printf("terminate -> %d\n", comp->terminate());
    return 0;
}

static void audio_loop(IAudioProcessor *proc, ProcessData &d, ProcessContext &ctx, EventList &events,
                       std::vector<std::vector<float>> &outbuf, int nch, int block, int seconds, double rate)
{
    long blocks = (long)(seconds * rate / block), per_sec = (long)(rate / block);
    double t0 = CFAbsoluteTimeGetCurrent();
    float peak = 0;
    for (long i = 0; i < blocks; i++) {
        events.ev.clear();
        Event e{};
        e.busIndex = 0;
        e.sampleOffset = 0;
        if (i % per_sec == per_sec / 4) { // one note per second, 0.5 s long
            e.type = Event::kNoteOnEvent;
            e.noteOn = {0, 60, 0.f, 0.8f, 0, -1};
            events.ev.push_back(e);
        } else if (i % per_sec == 3 * per_sec / 4) {
            e.type = Event::kNoteOffEvent;
            e.noteOff = {0, 60, 0.f, -1, 0.f}; // channel, pitch, velocity, noteId, tuning
            events.ev.push_back(e);
        }
        ctx.projectTimeSamples = i * block;
        ctx.systemTime = (int64)((CFAbsoluteTimeGetCurrent() - t0) * 1e9);
        proc->process(d);
        for (int c = 0; c < nch; c++)
            for (int s = 0; s < block; s++)
                peak = fmaxf(peak, fabsf(outbuf[c][s]));
        if ((i + 1) % per_sec == 0) {
            printf("t=%2lds output peak %.1f dBFS\n", (i + 1) / per_sec, peak > 0 ? 20 * log10f(peak) : -999.f);
            fflush(stdout);
            peak = 0;
        }
        double due = t0 + (i + 1) * block / rate, now = CFAbsoluteTimeGetCurrent(); // real-time pacing
        if (due > now)
            usleep((useconds_t)((due - now) * 1e6));
    }
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--probe"))
        return probe(argv[2], argc > 3 ? atoi(argv[3]) : 6);
    fprintf(stderr, "usage: vtihelper --probe <plugin.vst3> [seconds]\n");
    return 1;
}
