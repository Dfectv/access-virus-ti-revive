// nativehost - minimal VST3 test host for the native plug-in on Linux/macOS (R3 checks).
//   nativehost <plugin.vst3> [--state FILE] [--param ID=NORM]... [--save FILE] [--seconds N]
// --state loads a state (a raw component state, or vtihost's VTISTATE file: the original's state), checks that
// getState returns it unchanged, --param applies parameter changes through process(), --save writes getState.
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include <chrono>
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
    const char *state = nullptr, *save = nullptr;
    double seconds = 1;
    std::vector<std::pair<ParamID, double>> changes;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--state") && i + 1 < argc)
            state = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc)
            save = argv[++i];
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
    int blocks = (int)(seconds * 44100 / 512);
    for (int b = 0; b < blocks; b++) {
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
        std::this_thread::sleep_for(std::chrono::microseconds(11600));
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
