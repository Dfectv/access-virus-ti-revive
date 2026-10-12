// nativehost - minimal VST3 test host for the native plug-in on Linux/macOS (R3 checks).
//   nativehost <plugin.vst3> [--state FILE] [--param ID=NORM]... [--save FILE] [--seconds N] [--notes] [--wav FILE]
// --notes plays C4 (channel 1) for the first half of every second, --wav writes Out 1 (16-bit stereo 44.1 kHz).
// --state loads a state (a raw component state, or vtihost's VTISTATE file: the original's state), checks that
// getState returns it unchanged, --param applies parameter changes through process(), --save writes getState.
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static std::vector<uint8_t> read_file(const char *path)
{
    std::vector<uint8_t> d;
    if (FILE *f = fopen(path, "rb")) {
        for (int c; (c = fgetc(f)) != EOF;)
            d.push_back((uint8_t)c);
        fclose(f);
    }
    if (d.size() > 12 && !memcmp(d.data(), "VTISTATE", 8)) { // vtihost: u32 n + component state, ...
        uint32_t n;
        memcpy(&n, d.data() + 8, 4);
        d = std::vector<uint8_t>(d.begin() + 12, d.begin() + 12 + std::min<size_t>(n, d.size() - 12));
    }
    return d;
}

static std::vector<uint8_t> get_state(IComponent *c)
{
    auto *s = new MemoryStream;
    std::vector<uint8_t> d;
    if (c->getState(s) == kResultOk) {
        int64 size = 0;
        s->seek(0, IBStream::kIBSeekEnd, &size);
        d.resize((size_t)size);
        s->seek(0, IBStream::kIBSeekSet, nullptr);
        int32 got = 0;
        s->read(d.data(), (int32)d.size(), &got);
    }
    s->release();
    return d;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: nativehost <plugin.vst3> [--state FILE] [--param ID=NORM]... [--save FILE] [--seconds N]\n");
        return 2;
    }
    const char *state = nullptr, *save = nullptr, *wav = nullptr;
    bool notes = false;
    double seconds = 1;
    std::vector<std::pair<ParamID, double>> changes;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--state") && i + 1 < argc)
            state = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc)
            save = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wav = argv[++i];
        else if (!strcmp(argv[i], "--notes"))
            notes = true;
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--param") && i + 1 < argc) {
            const char *a = argv[++i], *eq = strchr(a, '=');
            if (eq)
                changes.push_back({(ParamID)strtoul(a, nullptr, 0), atof(eq + 1)});
        }
    }
    std::string err;
    auto module = VST3::Hosting::Module::create(argv[1], err);
    if (!module) {
        fprintf(stderr, "nativehost: %s\n", err.c_str());
        return 1;
    }
    auto factory = module->getFactory();
    auto infos = factory.classInfos();
    VST3::Hosting::ClassInfo audio;
    for (auto &ci : infos) {
        printf("class %s \"%s\" %s\n", ci.ID().toString().c_str(), ci.name().c_str(), ci.category().c_str());
        if (ci.category() == kVstAudioEffectClass)
            audio = ci;
    }
    auto comp = factory.createInstance<IComponent>(audio.ID());
    if (!comp) {
        fprintf(stderr, "nativehost: createInstance failed\n");
        return 1;
    }
    auto *host = new HostApplication;
    printf("initialize -> %d\n", (int)comp->initialize(host));
    FUnknownPtr<IAudioProcessor> proc(comp);
    FUnknownPtr<IEditController> ctl(comp);
    if (ctl)
        printf("parameters: %d\n", (int)ctl->getParameterCount());

    if (state) {
        auto in = read_file(state);
        auto *s = new MemoryStream(in.data(), (TSize)in.size());
        printf("setState(%zu bytes) -> %d\n", in.size(), (int)comp->setState(s));
        s->release();
        auto out = get_state(comp);
        printf("getState: %zu bytes, %s\n", out.size(), out == in ? "IDENTICAL to the loaded state" : "differs");
    }

    ProcessSetup ps{kRealtime, kSample32, 512, 44100};
    proc->setupProcessing(ps);
    comp->setActive(true);
    proc->setProcessing(true);
    HostProcessData data;
    data.prepare(*comp, 512, kSample32);
    data.numSamples = 512;
    ParameterChanges pc;
    EventList ev;
    std::vector<int16_t> pcm;
    float peak = 0;
    int blocks = (int)(seconds * 44100 / 512);
    auto t_next = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; b++) {
        ev.clear();
        if (notes) { // C4 on at the start of every second, off at the half
            long f0 = (long)b * 512, f1 = f0 + 512;
            for (long t = (f0 / 22050) * 22050; t < f1; t += 22050)
                if (t >= f0) {
                    Event e{};
                    e.sampleOffset = (int32)(t - f0);
                    if ((t / 22050) % 2 == 0) {
                        e.type = Event::kNoteOnEvent;
                        e.noteOn = {0, 60, 0, 0.8f, 0, -1};
                    } else {
                        e.type = Event::kNoteOffEvent;
                        e.noteOff = {0, 60, 0.f, -1, 0};
                    }
                    ev.addEvent(e);
                }
        }
        data.inputEvents = &ev;
        pc.clearQueue();
        if (b == 1)
            for (auto &c : changes) {
                int32 idx;
                if (auto *q = pc.addParameterData(c.first, idx)) {
                    q->addPoint(0, c.second, idx);
                    printf("param %u = %.4f\n", (unsigned)c.first, c.second);
                }
            }
        data.inputParameterChanges = &pc;
        proc->process(data);
        if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2) {
            float *l = data.outputs[0].channelBuffers32[0], *r = data.outputs[0].channelBuffers32[1];
            for (int i = 0; i < 512; i++) {
                peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
                pcm.push_back((int16_t)std::max(-32768.f, std::min(32767.f, l[i] * 32768)));
                pcm.push_back((int16_t)std::max(-32768.f, std::min(32767.f, r[i] * 32768)));
            }
        }
        if (b % 86 == 85) {
            printf("t=%.0f s peak %.3f\n", (b + 1) * 512 / 44100.0, peak);
            fflush(stdout);
            peak = 0;
        }
        t_next += std::chrono::microseconds(11610); // real time: 512 frames at 44.1 kHz
        std::this_thread::sleep_until(t_next);
    }
    if (wav) {
        if (FILE *f = fopen(wav, "wb")) {
            uint32_t bytes = (uint32_t)(pcm.size() * 2), rate = 44100, brate = 44100 * 4, sz = 36 + bytes, fmt = 16;
            uint16_t pcmfmt = 1, ch = 2, align = 4, bits = 16;
            fwrite("RIFF", 1, 4, f), fwrite(&sz, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
            fwrite(&pcmfmt, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&rate, 4, 1, f), fwrite(&brate, 4, 1, f);
            fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&bytes, 4, 1, f);
            fwrite(pcm.data(), 2, pcm.size(), f);
            fclose(f);
            printf("wrote %s\n", wav);
        }
    }
    proc->setProcessing(false);
    comp->setActive(false);
    if (save) {
        auto out = get_state(comp);
        if (FILE *f = fopen(save, "wb")) {
            fwrite(out.data(), 1, out.size(), f);
            fclose(f);
            printf("saved %zu bytes to %s\n", out.size(), save);
        }
    }
    comp->terminate();
    return 0;
}
