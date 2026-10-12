// vtibridgetest.mm - arm64 test host for the bridge (tirbridge_vst3.cpp): loads "Virus TI (TI Revive).vst3" like a
// DAW, opens its editor in a window, runs for a few seconds and saves what the window shows as PNG. Lets the editor
// mirror be debugged over SSH without Ableton.
// Build: clang++ -arch arm64 -std=c++17 -fobjc-arc -I vst3sdk -o vtibridgetest vtibridgetest.mm \
//          vst3sdk/pluginterfaces/base/funknown.cpp vst3sdk/pluginterfaces/base/coreiids.cpp -framework Cocoa
// usage: vtibridgetest <bridge.vst3> <out.png> [seconds]
#import <Cocoa/Cocoa.h>
#include <cstdio>
#include <cstring>

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace Steinberg {
DEF_CLASS_IID(IPlugView)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IEditController)
} // namespace Vst
} // namespace Steinberg

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: vtibridgetest <bridge.vst3> <out.png> [seconds]\n");
        return 1;
    }
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *)argv[1], strlen(argv[1]), true);
        CFBundleRef b = CFBundleCreate(nullptr, url);
        if (!b || !CFBundleLoadExecutable(b))
            return printf("cannot load bundle\n"), 1;
        auto entry = (bool (*)(CFBundleRef))CFBundleGetFunctionPointerForName(b, CFSTR("bundleEntry"));
        if (entry)
            entry(b);
        auto get = (IPluginFactory * (*)()) CFBundleGetFunctionPointerForName(b, CFSTR("GetPluginFactory"));
        IPluginFactory *f = get();
        PClassInfo ci{};
        f->getClassInfo(0, &ci);
        IComponent *comp = nullptr;
        f->createInstance(ci.cid, IComponent::iid, (void **)&comp);
        printf("initialize -> %d\n", comp->initialize(nullptr));
        IEditController *ctl = nullptr;
        comp->queryInterface(IEditController::iid, (void **)&ctl);
        IPlugView *v = ctl ? ctl->createView(ViewType::kEditor) : nullptr;
        printf("createView -> %p\n", (void *)v);
        if (!v)
            return 1;
        ViewRect r{};
        printf("getSize -> %d (%dx%d)\n", v->getSize(&r), r.getWidth(), r.getHeight());
        NSWindow *w = [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, r.getWidth(), r.getHeight())
                                                  styleMask:NSWindowStyleMaskTitled
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
        [w makeKeyAndOrderFront:nil];
        printf("attached -> %d\n", v->attached((__bridge void *)[w contentView], kPlatformTypeNSView));
        int secs = argc > 3 ? atoi(argv[3]) : 4;
        [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:secs]];
        NSView *cv = [w contentView];
        printf("content subviews %lu, first %s frame %s\n", (unsigned long)[[cv subviews] count],
               [[cv subviews] count] ? [NSStringFromClass([[[cv subviews] firstObject] class]) UTF8String] : "-",
               [cv subviews].count ? [NSStringFromRect([[[cv subviews] firstObject] frame]) UTF8String] : "-");
        NSBitmapImageRep *rep = [cv bitmapImageRepForCachingDisplayInRect:[cv bounds]];
        [cv cacheDisplayInRect:[cv bounds] toBitmapImageRep:rep];
        NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        printf("png %s\n", [png writeToFile:[NSString stringWithUTF8String:argv[2]] atomically:YES] ? "written" : "FAILED");
        v->removed();
        v->release();
        comp->terminate();
    }
    return 0;
}
