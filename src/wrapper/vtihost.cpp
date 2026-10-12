// vtihost.cpp - minimal VST3 test host for Windows / Wine (Linux port, findings/design-linux.md): loads a VST3
// (the TI Revive wrapper), activates it at 44.1 kHz / 512, runs process() in real time on its own thread while the
// main thread pumps Windows messages (the Access plug-in's timers send its MIDI and poll the device), plays a C4
// for half of every second after <quiet> seconds, prints the output peak per second and writes output bus 1 to a WAV.
//
// usage: vtihost <plugin.vst3> [seconds=60] [out.wav] [quiet=0]
// build: make -C src/wrapper (x86_64-w64-mingw32-g++, VST3 pluginterfaces from work/vst3sdk, MIT)
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

namespace Steinberg {
DEF_CLASS_IID(FUnknown)
DEF_CLASS_IID(IPluginFactory)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IParameterChanges)
} // namespace Vst
} // namespace Steinberg

#define IMPLEMENT_FUNKNOWN(I)                                                                                          \
    std::atomic<uint32> refs{1};                                                                                       \
    uint32 PLUGIN_API addRef() override { return ++refs; }                                                             \
    uint32 PLUGIN_API release() override { return --refs; }                                                            \
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override                                             \
    {                                                                                                                  \
        if (FUnknownPrivate::iidEqual(iid, I::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {                 \
            addRef();                                                                                                  \
            *obj = this;                                                                                               \
            return kResultOk;                                                                                          \
        }                                                                                                              \
        *obj = nullptr;                                                                                                \
        return kNoInterface;                                                                                           \
    }

struct Host : IHostApplication {
    IMPLEMENT_FUNKNOWN(IHostApplication)
    tresult PLUGIN_API getName(String128 name) override
    {
        const char *n = "TI Revive test host";
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
} g_host;

struct Handler : IComponentHandler {
    IMPLEMENT_FUNKNOWN(IComponentHandler)
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 f) override
    {
        printf("restartComponent 0x%x\n", (int)f);
        return kResultOk;
    }
} g_handler;

struct EventList : IEventList {
    IMPLEMENT_FUNKNOWN(IEventList)
    std::vector<Event> ev;
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

struct NoChanges : IParameterChanges {
    IMPLEMENT_FUNKNOWN(IParameterChanges)
    int32 PLUGIN_API getParameterCount() override { return 0; }
    IParamValueQueue *PLUGIN_API getParameterData(int32) override { return nullptr; }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &, int32 &) override { return nullptr; }
};

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t data = frames * 2 * 2, v32;
    uint16_t v16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f), v32 = 36 + data, fwrite(&v32, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16, fwrite(&v32, 4, 1, f), v16 = 1, fwrite(&v16, 2, 1, f), v16 = 2, fwrite(&v16, 2, 1, f);
    v32 = 44100, fwrite(&v32, 4, 1, f), v32 = 44100 * 4, fwrite(&v32, 4, 1, f);
    v16 = 4, fwrite(&v16, 2, 1, f), v16 = 16, fwrite(&v16, 2, 1, f);
    fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
}

static int g_block = 512; // VTI_BLOCK=64/128/256/512

struct Run {
    IAudioProcessor *proc;
    int seconds, quiet;
    FILE *wav;
    std::vector<int32> out_ch, in_ch;
    std::atomic<bool> done{false};
};

static DWORD WINAPI process_thread(LPVOID arg)
{
    Run &r = *(Run *)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    const int block = g_block;
    const double rate = 44100;
    long blocks = (long)(r.seconds * rate / block), per_sec = (long)(rate / block);
    std::vector<std::vector<std::vector<float>>> obuf(r.out_ch.size()), ibuf(r.in_ch.size());
    std::vector<std::vector<float *>> optr(r.out_ch.size()), iptr(r.in_ch.size());
    std::vector<AudioBusBuffers> outs(r.out_ch.size()), ins(r.in_ch.size());
    for (size_t b = 0; b < r.out_ch.size(); b++) {
        obuf[b].assign(r.out_ch[b], std::vector<float>(block));
        for (auto &c : obuf[b])
            optr[b].push_back(c.data());
        outs[b].numChannels = r.out_ch[b];
        outs[b].channelBuffers32 = optr[b].data();
    }
    for (size_t b = 0; b < r.in_ch.size(); b++) {
        ibuf[b].assign(r.in_ch[b], std::vector<float>(block));
        for (auto &c : ibuf[b])
            iptr[b].push_back(c.data());
        ins[b].numChannels = r.in_ch[b];
        ins[b].channelBuffers32 = iptr[b].data();
    }
    EventList events;
    NoChanges changes;
    ProcessContext ctx{};
    LARGE_INTEGER f, t0, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    float peak = 0;
    uint32_t wav_frames = 0;
    for (long i = 0; i < blocks; i++) {
        events.ev.clear();
        // a C4 from 0.25 s to 0.75 s of every second (exact sample positions; whole blocks would drift)
        for (int on = 1; on >= 0; on--) {
            int64_t first = (int64_t)i * block, last = first + block;
            int64_t sec = first / 44100, at = sec * 44100 + (on ? 11025 : 33075);
            if (at < first)
                at += 44100;
            if (at >= last || at / 44100 < r.quiet)
                continue;
            Event e{};
            e.busIndex = 0;
            e.sampleOffset = (int32)(at - first);
            if (on) {
                e.type = Event::kNoteOnEvent;
                e.noteOn = {0, 60, 0.f, 0.8f, 0, -1};
            } else {
                e.type = Event::kNoteOffEvent;
                e.noteOff = {0, 60, 0.f, -1, 0.f};
            }
            events.ev.push_back(e);
        }
        ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid |
                    ProcessContext::kProjectTimeMusicValid | ProcessContext::kContTimeValid |
                    ProcessContext::kSystemTimeValid;
        ctx.continousTimeSamples = i * block;
        QueryPerformanceCounter(&now);
        ctx.systemTime = (int64)((double)now.QuadPart * 1e9 / f.QuadPart);
        ctx.sampleRate = rate;
        ctx.projectTimeSamples = i * block;
        ctx.tempo = 120;
        ctx.timeSigNumerator = ctx.timeSigDenominator = 4;
        ctx.projectTimeMusic = i * block / rate * 2;
        ProcessData d{};
        d.processMode = kRealtime;
        d.symbolicSampleSize = kSample32;
        d.numSamples = block;
        d.numInputs = (int32)ins.size();
        d.inputs = ins.empty() ? nullptr : ins.data();
        d.numOutputs = (int32)outs.size();
        d.outputs = outs.data();
        d.inputEvents = &events;
        d.inputParameterChanges = &changes;
        d.processContext = &ctx;
        r.proc->process(d);
        if (!obuf.empty() && obuf[0].size() >= 2) {
            std::vector<int16_t> pcm(block * 2);
            for (int k = 0; k < block; k++)
                for (int c = 0; c < 2; c++) {
                    float x = obuf[0][c][k];
                    peak = fmaxf(peak, fabsf(x));
                    x = x > 1 ? 1 : x < -1 ? -1 : x;
                    pcm[k * 2 + c] = (int16_t)lrintf(x * 32767);
                }
            if (r.wav)
                fwrite(pcm.data(), 4, block, r.wav), wav_frames += block;
        }
        if ((i + 1) % per_sec == 0) {
            printf("t=%3lds output peak %.1f dBFS\n", (i + 1) / per_sec, peak > 0 ? 20 * log10f(peak) : -999.f);
            fflush(stdout);
            peak = 0;
        }
        double due = (i + 1) * block / rate;
        for (;;) {
            QueryPerformanceCounter(&now);
            double el = (double)(now.QuadPart - t0.QuadPart) / f.QuadPart;
            if (el >= due)
                break;
            Sleep(due - el > 0.002 ? 1 : 0);
        }
    }
    if (r.wav) {
        wav_header(r.wav, wav_frames);
        fclose(r.wav);
    }
    r.done = true;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: vtihost <plugin.vst3> [seconds=60] [out.wav] [quiet=0]\n");
        return 2;
    }
    int seconds = argc > 2 ? atoi(argv[2]) : 60, quiet = argc > 4 ? atoi(argv[4]) : 0;
    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }
    auto init = (bool (*)())GetProcAddress(m, "InitDll");
    if (init)
        init();
    auto gpf = (IPluginFactory * (PLUGIN_API *)()) GetProcAddress(m, "GetPluginFactory");
    IPluginFactory *fac = gpf ? gpf() : nullptr;
    if (!fac)
        return 1;
    PClassInfo ci{};
    int idx = -1;
    for (int i = 0; i < fac->countClasses() && idx < 0; i++)
        if (fac->getClassInfo(i, &ci) == kResultOk && !strcmp(ci.category, kVstAudioEffectClass))
            idx = i;
    if (idx < 0)
        return 1;
    printf("class \"%s\"\n", ci.name);
    IComponent *comp = nullptr;
    if (fac->createInstance(ci.cid, IComponent::iid, (void **)&comp) != kResultOk || !comp)
        return 1;
    printf("initialize -> %d\n", (int)comp->initialize(&g_host));
    IAudioProcessor *proc = nullptr;
    comp->queryInterface(IAudioProcessor::iid, (void **)&proc);
    IEditController *ctl = nullptr;
    if (comp->queryInterface(IEditController::iid, (void **)&ctl) != kResultOk) {
        TUID cid;
        if (comp->getControllerClassId(cid) == kResultOk &&
            fac->createInstance(cid, IEditController::iid, (void **)&ctl) == kResultOk && ctl)
            ctl->initialize(&g_host);
    }
    if (ctl)
        ctl->setComponentHandler(&g_handler);
    Run r{};
    r.proc = proc;
    r.seconds = seconds;
    r.quiet = quiet;
    r.wav = argc > 3 ? fopen(argv[3], "wb") : nullptr;
    if (r.wav)
        wav_header(r.wav, 0);
    std::vector<SpeakerArrangement> oa, ia;
    for (int32 b = 0; b < comp->getBusCount(kAudio, kOutput); b++) {
        BusInfo bi{};
        comp->getBusInfo(kAudio, kOutput, b, bi);
        r.out_ch.push_back(bi.channelCount);
        oa.push_back(bi.channelCount == 2 ? SpeakerArr::kStereo : SpeakerArr::kMono);
        comp->activateBus(kAudio, kOutput, b, true);
    }
    for (int32 b = 0; b < comp->getBusCount(kAudio, kInput); b++) {
        BusInfo bi{};
        comp->getBusInfo(kAudio, kInput, b, bi);
        r.in_ch.push_back(bi.channelCount);
        ia.push_back(bi.channelCount == 2 ? SpeakerArr::kStereo : SpeakerArr::kMono);
        comp->activateBus(kAudio, kInput, b, true);
    }
    if (comp->getBusCount(kEvent, kInput) > 0)
        comp->activateBus(kEvent, kInput, 0, true);
    printf("%zu output buses, %zu input buses\n", r.out_ch.size(), r.in_ch.size());
    proc->setBusArrangements(ia.empty() ? nullptr : ia.data(), (int32)ia.size(), oa.data(), (int32)oa.size());
    if (getenv("VTI_BLOCK"))
        g_block = atoi(getenv("VTI_BLOCK"));
    ProcessSetup ps{kRealtime, kSample32, g_block, 44100};
    printf("block %d\n", g_block);
    printf("setupProcessing -> %d\n", (int)proc->setupProcessing(ps));
    printf("setActive -> %d\n", (int)comp->setActive(true));
    printf("setProcessing -> %d\n", (int)proc->setProcessing(true));
    fflush(stdout);
    HANDLE th = CreateThread(nullptr, 0, process_thread, &r, 0, nullptr);
    while (!r.done) { // main thread: the plug-in's windows / timers
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        MsgWaitForMultipleObjects(1, &th, FALSE, 10, QS_ALLINPUT);
    }
    WaitForSingleObject(th, INFINITE);
    proc->setProcessing(false);
    comp->setActive(false);
    if (ctl && (void *)ctl != (void *)comp) {
        ctl->terminate();
        ctl->release();
    }
    comp->terminate();
    comp->release();
    printf("done\n");
    return 0;
}
