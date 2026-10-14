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
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <poll.h>
#endif
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

#ifdef __linux__
// Minimal Linux host side of the VST3 editor: IPlugFrame + IRunLoop (fd handlers, timers), pumped from main().
struct RunLoop : Steinberg::Linux::IRunLoop, IPlugFrame {
    struct Fd {
        Steinberg::Linux::IEventHandler *h;
        int fd;
    };
    struct Tm {
        Steinberg::Linux::ITimerHandler *h;
        int ms;
        std::chrono::steady_clock::time_point next;
    };
    std::vector<Fd> fds;
    std::vector<Tm> timers;
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
            *obj = static_cast<Steinberg::Linux::IRunLoop *>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IPlugFrame *>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler *h, Steinberg::Linux::FileDescriptor fd) override
    {
        fds.push_back({h, fd});
        fprintf(stderr, "nativehost: fd handler %d registered\n", fd);
        return kResultOk;
    }
    tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler *h) override
    {
        fds.erase(std::remove_if(fds.begin(), fds.end(), [&](const Fd &f) { return f.h == h; }), fds.end());
        return kResultOk;
    }
    tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler *h, Steinberg::Linux::TimerInterval ms) override
    {
        timers.push_back({h, (int)ms, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms)});
        fprintf(stderr, "nativehost: timer %d ms registered\n", (int)ms);
        return kResultOk;
    }
    tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler *h) override
    {
        timers.erase(std::remove_if(timers.begin(), timers.end(), [&](const Tm &t) { return t.h == h; }), timers.end());
        return kResultOk;
    }
    tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *r) override { return view->onSize(r); }
    void pump()
    {
        for (size_t i = 0; i < fds.size(); i++) {
            pollfd p{fds[i].fd, POLLIN, 0};
            if (poll(&p, 1, 0) > 0)
                fds[i].h->onFDIsSet(fds[i].fd);
        }
        auto now = std::chrono::steady_clock::now();
        for (size_t i = 0; i < timers.size(); i++)
            if (now >= timers[i].next) {
                timers[i].next = now + std::chrono::milliseconds(timers[i].ms);
                timers[i].h->onTimer();
            }
    }
};

static void save_window_ppm(Display *d, Window w, const char *path)
{
    XWindowAttributes a;
    XGetWindowAttributes(d, w, &a);
    XImage *img = XGetImage(d, w, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
    if (!img)
        return;
    if (FILE *f = fopen(path, "wb")) {
        fprintf(f, "P6\n%d %d\n255\n", a.width, a.height);
        for (int y = 0; y < a.height; y++)
            for (int x = 0; x < a.width; x++) {
                unsigned long px = XGetPixel(img, x, y);
                unsigned char rgb[3] = {(unsigned char)(px >> 16), (unsigned char)(px >> 8), (unsigned char)px};
                fwrite(rgb, 1, 3, f);
            }
        fclose(f);
    }
    XDestroyImage(img);
}
#endif

#ifdef __linux__
struct LinuxHost : HostApplication {
    RunLoop *loop = nullptr;
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (loop && FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
            fprintf(stderr, "nativehost: run loop requested\n");
            *obj = static_cast<Steinberg::Linux::IRunLoop *>(loop);
            return kResultOk;
        }
        return HostApplication::queryInterface(iid, obj);
    }
};
#endif

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
    bool notes = false, editor = false, stress = false, stress_notes = true, stress_params = true;
    double stress_stop = 1e9;
    std::vector<std::pair<double, double>> aheads; // (at s, ms)
    int block = 512; // --block N: host block size
    struct Action {
        double t;
        char kind; // c = click, d = drag, s = screenshot
        int x, y, dy;
        std::string file;
        bool done = false;
    };
    std::vector<Action> actions;
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
        else if (!strcmp(argv[i], "--stress")) // MIDI load: notes on 16 channels + 32 automated parameters per block
            stress = true;
        else if (!strcmp(argv[i], "--stress-notes")) // only the notes of --stress
            stress = true, stress_params = false;
        else if (!strcmp(argv[i], "--stress-params")) // only the automation of --stress
            stress = true, stress_notes = false;
        else if (!strcmp(argv[i], "--ahead") && i + 1 < argc) { // --ahead MS@T: from T s on process MS ms ahead of
            // real time in a burst, like REAPER's anticipative FX processing after a playback start
            double ms = 0, t = 0;
            sscanf(argv[++i], "%lf@%lf", &ms, &t);
            aheads.push_back({t, ms});
        } else if (!strcmp(argv[i], "--stress-stop") && i + 1 < argc) // the automation of --stress ends after N s
            stress_stop = atof(argv[++i]);
        else if (!strcmp(argv[i], "--block") && i + 1 < argc)
            block = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--editor"))
            editor = true;
        else if ((!strcmp(argv[i], "--click") || !strcmp(argv[i], "--drag") || !strcmp(argv[i], "--shot") ||
                  !strcmp(argv[i], "--twice") || !strcmp(argv[i], "--type") || !strcmp(argv[i], "--move")) &&
                 i + 1 < argc) {
            // --click x,y@t  --drag x,y,dy@t  --shot file.ppm@t
            Action a{};
            a.kind = !strcmp(argv[i], "--type") ? 'y' : !strcmp(argv[i], "--move") ? 'm' : argv[i][2];
            std::string v = argv[++i];
            size_t at = v.rfind('@');
            a.t = at == std::string::npos ? 1 : atof(v.c_str() + at + 1);
            v = v.substr(0, at);
            if (a.kind == 's' || a.kind == 'y') // --shot file, --type text
                a.file = v;
            else
                sscanf(v.c_str(), "%d,%d,%d", &a.x, &a.y, &a.dy);
            actions.push_back(a);
        }
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
#ifdef __linux__
    static RunLoop runloop;
    auto *host = new LinuxHost;
    host->loop = &runloop;
#else
    auto *host = new HostApplication;
#endif
    auto factory = module->getFactory();
    factory.setHostContext(host);
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

#ifdef __linux__
    Display *dpy = nullptr;
    Window win = 0;
    IPlugView *view = nullptr;
    if (editor && ctl) {
        dpy = XOpenDisplay(nullptr);
        view = ctl->createView(ViewType::kEditor);
        if (!dpy || !view) {
            fprintf(stderr, "nativehost: no display or no editor\n");
            return 1;
        }
        ViewRect r{};
        view->getSize(&r);
        win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, r.getWidth(), r.getHeight(), 0, 0, 0);
        XMapWindow(dpy, win);
        XFlush(dpy);
        view->setFrame(&runloop);
        printf("editor %dx%d attached -> %d\n", r.getWidth(), r.getHeight(),
               (int)view->attached((void *)(uintptr_t)win, kPlatformTypeX11EmbedWindowID));
        // XEmbed: the embedder maps the plug-in's window (real hosts do this)
        Window root_ret, parent_ret, *kids = nullptr;
        unsigned nkids = 0;
        if (XQueryTree(dpy, win, &root_ret, &parent_ret, &kids, &nkids)) {
            for (unsigned k = 0; k < nkids; k++)
                XMapWindow(dpy, kids[k]);
            if (kids)
                XFree(kids);
        }
        XFlush(dpy);
    }
#endif
    ProcessSetup ps{kRealtime, kSample32, block, 44100};
    proc->setupProcessing(ps);
    comp->setActive(true);
    proc->setProcessing(true);
    HostProcessData data;
    data.prepare(*comp, block, kSample32);
    data.numSamples = block;
    ParameterChanges pc, outpc;
    EventList ev;
    std::vector<int16_t> pcm;
    float peak = 0;
    int blocks = (int)(seconds * 44100 / block);
    auto t_next = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; b++) {
        ev.clear();
        if (notes) { // C4 on at the start of every second, off at the half
            long f0 = (long)b * block, f1 = f0 + block;
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
        if (stress && stress_notes) { // every block: a note on/off pair on each of the 16 channels (cycling pitches)
            for (int ch = 0; ch < 16; ch++) {
                Event e{};
                e.sampleOffset = ch * 30;
                e.type = Event::kNoteOffEvent;
                e.noteOff = {(int16)ch, (int16)(48 + (b + ch) % 24), 0.f, -1, 0};
                ev.addEvent(e);
                e.sampleOffset = ch * 30 + 5;
                e.type = Event::kNoteOnEvent;
                e.noteOn = {(int16)ch, (int16)(48 + (b + ch + 1) % 24), 0, 0.7f, 0, -1};
                ev.addEvent(e);
            }
        }
        data.inputEvents = &ev;
        pc.clearQueue();
        if (stress && stress_params && ctl && b * (double)block < stress_stop * 44100.0) { // 32 automatable parameters moving every block
            static std::vector<ParamID> ids;
            if (ids.empty())
                for (int32 k = 0; k < ctl->getParameterCount() && ids.size() < 32; k++) {
                    ParameterInfo pi{};
                    if (ctl->getParameterInfo(k, pi) == kResultOk && (pi.flags & ParameterInfo::kCanAutomate))
                        ids.push_back(pi.id);
                }
            for (size_t k = 0; k < ids.size(); k++) {
                int32 idx;
                if (auto *q = pc.addParameterData(ids[k], idx))
                    q->addPoint((int32)(k * 15), 0.5 + 0.4 * std::sin((b + k * 7) * 0.05), idx);
            }
        }
        if (b == 1)
            for (auto &c : changes) {
                int32 idx;
                if (auto *q = pc.addParameterData(c.first, idx)) {
                    q->addPoint(0, c.second, idx);
                    printf("param %u = %.4f\n", (unsigned)c.first, c.second);
                }
            }
        data.inputParameterChanges = &pc;
        outpc.clearQueue();
        data.outputParameterChanges = &outpc;
        proc->process(data);
        for (int32 i = 0; i < outpc.getParameterCount(); i++) { // device -> host (knobs on the Virus)
            IParamValueQueue *q = outpc.getParameterData(i);
            int32 off;
            ParamValue v;
            if (q && q->getPoint(q->getPointCount() - 1, off, v) == kResultOk) {
                String128 title{};
                ParameterInfo pi{};
                if (ctl)
                    for (int32 k = 0; k < ctl->getParameterCount(); k++)
                        if (ctl->getParameterInfo(k, pi) == kResultOk && pi.id == q->getParameterId())
                            break;
                char t[128] = "";
                for (int k = 0; k < 127 && pi.title[k]; k++)
                    t[k] = (char)pi.title[k];
                printf("from the Virus: %u %s = %.3f\n", (unsigned)q->getParameterId(), t, v);
            }
        }
        if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2) {
            float *l = data.outputs[0].channelBuffers32[0], *r = data.outputs[0].channelBuffers32[1];
            for (int i = 0; i < block; i++) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) {
                    static int bad;
                    if (bad++ < 3)
                        fprintf(stderr, "non-finite sample at block %d frame %d: %f %f\n", b, i, l[i], r[i]);
                }
                peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
                pcm.push_back((int16_t)std::max(-32768.f, std::min(32767.f, l[i] * 32768)));
                pcm.push_back((int16_t)std::max(-32768.f, std::min(32767.f, r[i] * 32768)));
            }
        }
        if ((b + 1) * (long)block / 44100 != (long)b * block / 44100) { // once per second
            printf("t=%.0f s peak %.3f\n", (b + 1) * (double)block / 44100.0, peak);
            fflush(stdout);
            peak = 0;
        }
        t_next += std::chrono::microseconds((long long)block * 1000000 / 44100); // real time at 44.1 kHz
        for (auto &ah : aheads)
            if (ah.second > 0 && b * (double)block / 44100.0 >= ah.first) {
                t_next -= std::chrono::microseconds((long long)(ah.second * 1000));
                printf("ahead %.0f ms at %.1f s\n", ah.second, ah.first);
                ah.second = 0;
            }
#ifdef __linux__
        if (view) {
            double now = (b + 1) * (double)block / 44100.0;
            for (auto &a : actions)
                if (!a.done && now >= a.t) {
                    a.done = true;
                    if (a.kind == 's') {
                        save_window_ppm(dpy, win, a.file.c_str());
                        printf("screenshot %s at %.1f s\n", a.file.c_str(), now);
                        continue;
                    }
                    if (a.kind == 'y') { // type text + Return through XTest
                        { // keyboard focus to the (VSTGUI child) window under the pointer - no window manager here
                            Window root, child = win, w = win;
                            int rx, ry, wx, wy;
                            unsigned mask;
                            while (XQueryPointer(dpy, w, &root, &child, &rx, &ry, &wx, &wy, &mask) && child)
                                w = child;
                            XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
                        }
                        auto key = [&](KeySym ks, bool shift) {
                            KeyCode kc = XKeysymToKeycode(dpy, ks), sh = XKeysymToKeycode(dpy, XK_Shift_L);
                            if (shift)
                                XTestFakeKeyEvent(dpy, sh, True, 0);
                            XTestFakeKeyEvent(dpy, kc, True, 0);
                            XTestFakeKeyEvent(dpy, kc, False, 0);
                            if (shift)
                                XTestFakeKeyEvent(dpy, sh, False, 0);
                            XFlush(dpy);
                            for (int k = 0; k < 3; k++) {
                                runloop.pump();
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            }
                        };
                        for (char ch : a.file) {
                            char nm[2] = {ch, 0};
                            KeySym ks = ch == ' ' ? XK_space : XStringToKeysym(nm);
                            key(ks, ch >= 'A' && ch <= 'Z');
                        }
                        key(XK_Return, false);
                        printf("typed \"%s\" at %.1f s\n", a.file.c_str(), now);
                        continue;
                    }
                    int wx, wy;
                    Window child;
                    XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), a.x, a.y, &wx, &wy, &child);
                    XTestFakeMotionEvent(dpy, -1, wx, wy, 0);
                    if (a.kind == 'm') { // hover only
                        XFlush(dpy);
                        printf("move %d,%d at %.1f s\n", a.x, a.y, now);
                        continue;
                    }
                    XTestFakeButtonEvent(dpy, 1, True, 0);
                    XFlush(dpy);
                    for (int k = 1; a.kind == 'd' && k <= 10; k++) { // drag in 10 steps
                        runloop.pump();
                        std::this_thread::sleep_for(std::chrono::milliseconds(15));
                        XTestFakeMotionEvent(dpy, -1, wx, wy - a.dy * k / 10, 0);
                        XFlush(dpy);
                    }
                    runloop.pump();
                    XTestFakeButtonEvent(dpy, 1, False, 0);
                    XFlush(dpy);
                    if (a.kind == 't') { // double click
                        runloop.pump();
                        std::this_thread::sleep_for(std::chrono::milliseconds(60));
                        XTestFakeButtonEvent(dpy, 1, True, 0);
                        XFlush(dpy);
                        runloop.pump();
                        XTestFakeButtonEvent(dpy, 1, False, 0);
                        XFlush(dpy);
                    }
                    printf("%s %d,%d at %.1f s\n", a.kind == 'c' ? "click" : a.kind == 't' ? "double click" : "drag", a.x, a.y, now);
                }
            runloop.pump();
        }
#endif
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
#ifdef __linux__
    if (view) {
        view->removed();
        view->release();
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
    }
#endif
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
