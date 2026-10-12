// vtiautest.mm - test host for the Audio Unit bridge (tirbridge_au.cpp) that loads the component bundle directly and
// calls its factory, so it works over SSH (auval needs the user's GUI session to see the AU registry). Exercises
// what auval and Logic do: open, properties, parameters, ClassInfo save/restore, MIDI + render on all buses, and the
// Cocoa view (saved as PNG).
// Build: clang++ -arch arm64 -std=c++17 -fobjc-arc -o vtiautest vtiautest.mm -framework Cocoa -framework AudioToolbox
//          -framework AudioUnit -framework CoreAudio
// usage: vtiautest <X.component> <out.png> [seconds of rendering]
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#include <cmath>
#include <objc/message.h>
#include <cstdio>
#include <dlfcn.h>
#include <vector>

typedef OSStatus (*Fn_init)(void *);
typedef OSStatus (*Fn_pinfo)(void *, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, UInt32 *, Boolean *);
typedef OSStatus (*Fn_get)(void *, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, void *, UInt32 *);
typedef OSStatus (*Fn_set)(void *, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, const void *, UInt32);
typedef OSStatus (*Fn_render)(void *, AudioUnitRenderActionFlags *, const AudioTimeStamp *, UInt32, UInt32,
                              AudioBufferList *);
typedef OSStatus (*Fn_midi)(void *, UInt32, UInt32, UInt32, UInt32);
typedef OSStatus (*Fn_getp)(void *, AudioUnitParameterID, AudioUnitScope, AudioUnitElement, AudioUnitParameterValue *);

int main(int argc, char **argv)
{
    if (argc < 3)
        return fprintf(stderr, "usage: vtiautest <X.component> <out.png> [seconds]\n"), 1;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
        std::string bin = std::string(argv[1]) + "/Contents/MacOS/Virus TI (TI Revive)";
        void *h = dlopen(bin.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h)
            return printf("dlopen: %s\n", dlerror()), 1;
        auto factory = (void *(*)(const AudioComponentDescription *))dlsym(h, "TIReviveVTIFactory");
        AudioComponentDescription desc{kAudioUnitType_MusicDevice, 'VTIr', 'TIRv', 0, 0};
        auto *pi = (AudioComponentPlugInInterface *)factory(&desc);
        printf("Open -> %d\n", (int)pi->Open(pi, nullptr));
        auto get = (Fn_get)pi->Lookup(kAudioUnitGetPropertySelect);
        auto set = (Fn_set)pi->Lookup(kAudioUnitSetPropertySelect);
        auto pinfo = (Fn_pinfo)pi->Lookup(kAudioUnitGetPropertyInfoSelect);
        auto init = (Fn_init)pi->Lookup(kAudioUnitInitializeSelect);
        auto render = (Fn_render)pi->Lookup(kAudioUnitRenderSelect);
        auto midi = (Fn_midi)pi->Lookup(kMusicDeviceMIDIEventSelect);
        auto getp = (Fn_getp)pi->Lookup(kAudioUnitGetParameterSelect);

        UInt32 n = 0, sz = sizeof n;
        get(pi, kAudioUnitProperty_ElementCount, kAudioUnitScope_Output, 0, &n, &sz);
        printf("output buses: %u\n", n);
        UInt32 psize = 0;
        Boolean w;
        OSStatus st = pinfo(pi, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &psize, &w);
        std::vector<AudioUnitParameterID> ids(psize / sizeof(AudioUnitParameterID));
        sz = psize;
        get(pi, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &sz);
        printf("ParameterList -> %d, %zu parameters\n", (int)st, ids.size());
        for (size_t i = 0; i < ids.size() && i < 5; i++) {
            AudioUnitParameterInfo info{};
            sz = sizeof info;
            get(pi, kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, ids[i], &info, &sz);
            AudioUnitParameterValue v = 0;
            getp(pi, ids[i], kAudioUnitScope_Global, 0, &v);
            printf("  param %u \"%s\" default %.3f value %.3f\n", ids[i], info.name, info.defaultValue, v);
            if (info.cfNameString)
                CFRelease(info.cfNameString);
        }
        Float64 rate = 44100;
        set(pi, kAudioUnitProperty_SampleRate, kAudioUnitScope_Output, 0, &rate, sizeof rate);
        const UInt32 F = getenv("VTI_FRAMES") ? (UInt32)atoi(getenv("VTI_FRAMES")) : 512; // host block size
        UInt32 maxf = F;
        set(pi, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sizeof maxf);
        printf("Initialize -> %d\n", (int)init(pi));

        CFPropertyListRef state = nullptr;
        sz = sizeof state;
        st = get(pi, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, &sz);
        CFDataRef blob = state ? (CFDataRef)CFDictionaryGetValue((CFDictionaryRef)state, CFSTR(kAUPresetDataKey)) : nullptr;
        printf("ClassInfo get -> %d, state %ld bytes\n", (int)st, blob ? (long)CFDataGetLength(blob) : -1L);
        if (state) {
            st = set(pi, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &state, sizeof state);
            printf("ClassInfo set -> %d\n", (int)st);
            CFRelease(state);
        }

        // render: a note per second on all three buses
        int secs = argc > 3 ? atoi(argv[3]) : 4;
        std::vector<float> buf[6];
        for (auto &b : buf)
            b.assign(F, 0);
        double peak = 0;
        AudioTimeStamp ts{};
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        long blocks = (long)(secs * rate / F);
        CFAbsoluteTime t0 = CFAbsoluteTimeGetCurrent();
        for (long i = 0; i < blocks; i++) {
            long per_s = (long)(rate / F);
            if (i % per_s == per_s / 4)
                midi(pi, 0x90, 60, 100, F - 10); // late in the block: lands in the last piece
            if (i % per_s == 3 * per_s / 4)
                midi(pi, 0x80, 60, 0, 0);
            for (UInt32 bus = 0; bus < 3; bus++) {
                struct {
                    UInt32 n;
                    AudioBuffer b[2];
                } abl{2, {{1, F * 4, buf[bus * 2].data()}, {1, F * 4, buf[bus * 2 + 1].data()}}};
                AudioUnitRenderActionFlags fl = 0;
                st = render(pi, &fl, &ts, bus, F, (AudioBufferList *)&abl);
                if (st)
                    return printf("Render bus %u -> %d\n", bus, (int)st), 1;
            }
            for (UInt32 k = 0; k < F; k++)
                peak = std::max(peak, (double)std::fabs(buf[0][k]));
            ts.mSampleTime += F;
            CFAbsoluteTime due = t0 + (i + 1) * F / rate; // real time, with the run loop serviced
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceReferenceDate:due]];
        }
        printf("rendered %ld blocks x 3 buses, main peak %.1f dBFS\n", blocks, peak > 0 ? 20 * log10(peak) : -999.0);

        // Cocoa view
        AudioUnitCocoaViewInfo vi{};
        sz = sizeof vi;
        st = get(pi, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &vi, &sz);
        NSBundle *vb = [NSBundle bundleWithURL:(__bridge NSURL *)vi.mCocoaAUViewBundleLocation];
        [vb load];
        Class fc = NSClassFromString((__bridge NSString *)vi.mCocoaAUViewClass[0]);
        printf("CocoaUI -> %d, class %s %s\n", (int)st, [(__bridge NSString *)vi.mCocoaAUViewClass[0] UTF8String],
               fc ? "found" : "MISSING");
        if (fc) {
            // the factory asks the AU for our private core property; without a real AudioUnit handle, call the
            // container directly through the same path: AudioUnitGetProperty needs the registry, so emulate it
            void *core = nullptr;
            sz = sizeof core;
            get(pi, 0x54495276, kAudioUnitScope_Global, 0, &core, &sz);
            Class cv = NSClassFromString(@"TIRAUContainerView");
            NSView *view = ((id(*)(id, SEL, void *))objc_msgSend)([cv alloc], @selector(initWithCore:), core);
            NSWindow *win = [[NSWindow alloc] initWithContentRect:[view frame]
                                                        styleMask:NSWindowStyleMaskTitled
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];
            [win setContentView:view];
            [win makeKeyAndOrderFront:nil];
            [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:3]];
            printf("view %.0fx%.0f, %lu subviews\n", [view frame].size.width, [view frame].size.height,
                   (unsigned long)[[view subviews] count]);
            NSBitmapImageRep *rep = [view bitmapImageRepForCachingDisplayInRect:[view bounds]];
            [view cacheDisplayInRect:[view bounds] toBitmapImageRep:rep];
            NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            printf("png %s\n", [png writeToFile:[NSString stringWithUTF8String:argv[2]] atomically:YES] ? "written" : "FAILED");
            [win setContentView:[[NSView alloc] init]];
            [win close];
        }
        ((Fn_init)pi->Lookup(kAudioUnitUninitializeSelect))(pi);
        printf("Close -> %d\n", (int)pi->Close(pi));
    }
    return 0;
}
