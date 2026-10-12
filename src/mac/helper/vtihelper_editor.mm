// vtihelper_editor.mm - see vtihelper_editor.h. The original's editor (VSTGUI, CoreGraphics drawing, E104) lives in a
// borderless window with alpha 0 that ignores the mouse, placed exactly behind the bridge's view: invisible and
// click-through, but at the right screen position for VSTGUI's popup menus and text fields. Whenever a view asks for
// a redraw, the window's content is rendered into a double-buffered pixel shm, which the bridge draws; the bridge's
// mouse and key events are replayed into this window.
#import <Cocoa/Cocoa.h>
#include <fcntl.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <algorithm>
#include <atomic>
#include <vector>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#include "pluginterfaces/gui/iplugview.h"
#include "vtihelper_editor.h"

using namespace Steinberg;

namespace Steinberg {
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
}

static IMP g_orig_perform_click; // popup menus, see swz_perform_click
static void swz_perform_click(id self, SEL sel, NSRect frame, NSView *view);

// --- redraw detection: every -setNeedsDisplay(InRect): in this process marks the editors dirty
static volatile bool g_dirty = true;
static IMP g_orig_sndir, g_orig_snd;
static void swz_sndir(id self, SEL sel, NSRect r)
{
    g_dirty = true;
    ((void (*)(id, SEL, NSRect))g_orig_sndir)(self, sel, r);
}
static void swz_snd(id self, SEL sel, BOOL f)
{
    g_dirty = true;
    ((void (*)(id, SEL, BOOL))g_orig_snd)(self, sel, f);
}
static void install_dirty_hooks()
{
    static bool done;
    if (done)
        return;
    done = true;
    Method m1 = class_getInstanceMethod([NSView class], @selector(setNeedsDisplayInRect:));
    g_orig_sndir = method_setImplementation(m1, (IMP)swz_sndir);
    Method m2 = class_getInstanceMethod([NSView class], @selector(setNeedsDisplay:));
    g_orig_snd = method_setImplementation(m2, (IMP)swz_snd);
    Method m3 = class_getInstanceMethod([NSPopUpButtonCell class], @selector(performClickWithFrame:inView:));
    g_orig_perform_click = method_setImplementation(m3, (IMP)swz_perform_click);
}

// --- popup menus: VSTGUI's COptionMenu runs -[NSPopUpButtonCell performClickWithFrame:inView:] (modal menu tracking).
// In this invisible, inactive helper that menu cannot be used and the call never returns (the plug-in "hangs"). The
// swizzled version sends the menu to the bridge, which shows it natively in the DAW, waits for the answer in a nested
// run loop (the plug-in's other main-thread work continues) and then performs the chosen item like a real selection.
struct HelperEditor;
static std::vector<HelperEditor *> g_editors;
static HelperEditor *editor_for_window(NSWindow *w);

// The helper is an internal process of the bridge: it ends when its DAW or its last instance goes away, never through
// AppKit. Inside the DAW's GUI session AppKit delivered a quit to it right after the first input event (helper gone ->
// DAW waiting, E106), so termination requests are refused and logged.
@interface TIRAppDelegate : NSObject <NSApplicationDelegate>
@end
@implementation TIRAppDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    fprintf(stderr, "vtihelper: AppKit asked to terminate: refused\n");
    return NSTerminateCancel;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return NO;
}
@end

// borderless windows cannot become key by default; VSTGUI's frame only works fully in a key window (E107)
@interface TIRKeyWindow : NSWindow
@end
@implementation TIRKeyWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
// VSTGUI's CFrame only carries out focus changes while it is "active" (CFrame +0x158, set from the window's key
// notification via -isKeyWindow; Virus Control 0x9478 setFocusView). The helper is never the active app, so AppKit
// never makes this window key: report it as key, so text edits end on Return / focus loss (E107).
- (BOOL)isKeyWindow { return YES; }
@end

@interface TIRRenderTimer : NSObject
@property(assign) HelperEditor *editor;
- (void)tick:(NSTimer *)t;
@end

struct HelperEditor final : IPlugFrame {
    std::atomic<uint32> refs{1};
    IPlugView *view = nullptr;
    NSWindow *win = nil;
    NSView *container = nil;
    NSTimer *timer = nil;
    TIRRenderTimer *target = nil;
    std::string prefix;
    int gen = 0; // pixel shm generation (a resize creates a new shm)
    tirb_pixels *px = nullptr;
    size_t px_size = 0;
    char px_name[32] = "";
    double scale = 1, top_x = 0, top_y = 0;
    int w = 0, h = 0;
    editor_notify_cb notify = nullptr;
    bool key_focus = false;
    std::atomic<long> win_number{0}; // for posting events from the control thread
    std::atomic<int> height{0};
    std::atomic<uint32_t> menu_id{0};
    std::atomic<bool> menu_done{false};
    tirb_menu_result menu_res{};
    void *ctx = nullptr;

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    tresult PLUGIN_API resizeView(IPlugView *v, ViewRect *r) override // the original changes its size
    {
        if (!r)
            return kInvalidArgument;
        v->onSize(r);
        if (!px) { // still inside editor_open (attached): just take the size, the reply reports it
            w = r->getWidth(), h = r->getHeight();
            height = h;
            [container setFrame:NSMakeRect(0, 0, w, h)];
            place();
            return kResultOk;
        }
        set_size(r->getWidth(), r->getHeight());
        tirb_editor_reply rep{w, h, {}};
        strcpy(rep.pixels_shm, px_name);
        if (notify)
            notify(ctx, TIRB_N_EDITOR_RESIZE, &rep, sizeof rep);
        return kResultOk;
    }

    bool map_pixels()
    {
        if (px) {
            munmap(px, px_size);
            shm_unlink(px_name);
            px = nullptr;
        }
        uint32_t pw = (uint32_t)(w * scale), ph = (uint32_t)(h * scale);
        px_size = sizeof(tirb_pixels) + 2ull * pw * ph * 4;
        snprintf(px_name, sizeof px_name, "%s-v%d", prefix.c_str(), ++gen);
        shm_unlink(px_name);
        int fd = shm_open(px_name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0 || ftruncate(fd, (off_t)px_size)) {
            if (fd >= 0)
                close(fd);
            return false;
        }
        void *m = mmap(nullptr, px_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (m == MAP_FAILED)
            return false;
        px = (tirb_pixels *)m;
        memset(px, 0, sizeof(tirb_pixels));
        px->pw = pw, px->ph = ph, px->scale = (uint32_t)scale;
        g_dirty = true;
        return true;
    }
    void set_size(int nw, int nh)
    {
        w = nw, h = nh;
        height = nh;
        [container setFrame:NSMakeRect(0, 0, w, h)];
        place();
        map_pixels();
    }
    void place() // window top-left at (top_x, top_y), Cocoa screen coordinates
    {
        [win setFrame:NSMakeRect(top_x, top_y - h, w, h) display:NO];
    }
    unsigned long renders = 0;
    void check_focus() // text editing in the original (VSTGUI CTextEdit = NSTextField -> field editor NSText)
    {
        bool f = [[win firstResponder] isKindOfClass:[NSText class]];
        if (f == key_focus)
            return;
        key_focus = f;
        int32_t on = f;
        if (notify)
            notify(ctx, TIRB_N_KEY_FOCUS, &on, 4);
    }
    void render()
    {
        check_focus();
        if (!px || !g_dirty)
            return;
        if (renders++ < 3)
            fprintf(stderr, "vtihelper: render %lu (%ux%u px, %lu subviews)\n", renders, px->pw, px->ph,
                    (unsigned long)[[container subviews] count]);
        g_dirty = false;
        uint32_t back = px->front ^ 1;
        uint8_t *mem = px->data + (size_t)back * px->pw * px->ph * 4;
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef cg = CGBitmapContextCreate(mem, px->pw, px->ph, 8, px->pw * 4, cs,
                                                kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(cs);
        if (!cg)
            return;
        CGContextClearRect(cg, CGRectMake(0, 0, px->pw, px->ph));
        CGContextScaleCTM(cg, scale, scale);
        NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:gc];
        [container displayRectIgnoringOpacity:[container bounds] inContext:gc];
        [NSGraphicsContext restoreGraphicsState];
        CGContextRelease(cg);
        __atomic_store_n(&px->front, back, __ATOMIC_RELEASE);
        __atomic_add_fetch(&px->frame, 1, __ATOMIC_RELEASE);
    }
};

@implementation TIRRenderTimer
- (void)tick:(NSTimer *)t
{
    (void)t;
    if (self.editor)
        self.editor->render();
}
@end

HelperEditor *editor_open(Vst::IEditController *ctl, const char *shm_prefix, double x, double y, double scale,
                          tirb_editor_reply &reply, editor_notify_cb notify, void *ctx)
{
    static bool app_ready;
    if (!app_ready) { // the helper runs a plain CFRunLoop, not -[NSApp run]: set AppKit up once by hand
        [NSApplication sharedApplication];
        static TIRAppDelegate *delegate = [TIRAppDelegate new];
        [NSApp setDelegate:delegate];
        [[NSProcessInfo processInfo] disableAutomaticTermination:@"TI Revive helper"];
        [[NSProcessInfo processInfo] disableSuddenTermination];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; // no Dock icon, no menu bar
        [NSApp finishLaunching];
        app_ready = true;
    }
    install_dirty_hooks();
    IPlugView *v = ctl->createView(Vst::ViewType::kEditor);
    if (!v)
        return nullptr;
    if (v->isPlatformTypeSupported(kPlatformTypeNSView) != kResultTrue) {
        v->release();
        return nullptr;
    }
    HelperEditor *e = new HelperEditor;
    e->view = v;
    e->prefix = shm_prefix;
    e->scale = scale >= 2 ? 2 : 1;
    e->top_x = x, e->top_y = y;
    e->notify = notify, e->ctx = ctx;
    ViewRect r{};
    v->getSize(&r);
    e->w = r.getWidth() > 0 ? r.getWidth() : 800;
    e->h = r.getHeight() > 0 ? r.getHeight() : 600;
    e->win = [[TIRKeyWindow alloc] initWithContentRect:NSMakeRect(x, y - e->h, e->w, e->h)
                                         styleMask:NSWindowStyleMaskBorderless
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    [e->win setReleasedWhenClosed:NO];
    [e->win setAlphaValue:0.0];
    [e->win setIgnoresMouseEvents:YES];
    [e->win setHasShadow:NO];
    [e->win setAcceptsMouseMovedEvents:YES];
    e->container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, e->w, e->h)];
    [e->win setContentView:e->container];
    [e->win orderFront:nil];
    e->win_number = (long)[e->win windowNumber];
    e->height = e->h;
    v->setFrame(e);
    if (v->attached((__bridge void *)e->container, kPlatformTypeNSView) != kResultOk) {
        [e->win close];
        v->release();
        delete e;
        return nullptr;
    }
    // VSTGUI only moves its focus (e.g. ends a text edit on Return: CFrame::setFocusView -> looseFocus) while the frame
    // is active, which it learns from NSWindowDidBecomeKeyNotification. Our window is never key (inactive helper), so
    // text edits never ended (E107): tell VSTGUI the window is active.
    [[NSNotificationCenter defaultCenter] postNotificationName:NSWindowDidBecomeKeyNotification object:e->win];
    if (!e->map_pixels()) {
        editor_close(e);
        return nullptr;
    }
    e->target = [TIRRenderTimer new];
    e->target.editor = e;
    e->timer = [NSTimer timerWithTimeInterval:1.0 / 30
                                       target:e->target
                                     selector:@selector(tick:)
                                     userInfo:nil
                                      repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:e->timer forMode:NSRunLoopCommonModes]; // also while a popup menu tracks
    e->render();
    g_editors.push_back(e);
    reply.width = e->w, reply.height = e->h;
    strcpy(reply.pixels_shm, e->px_name);
    return e;
}

void editor_close(HelperEditor *e)
{
    if (!e)
        return;
    [e->timer invalidate];
    e->target.editor = nullptr;
    g_editors.erase(std::remove(g_editors.begin(), g_editors.end(), e), g_editors.end());
    if (e->menu_id) { // a menu is still waiting: end it
        e->menu_res = {};
        e->menu_done = true;
    }
    e->view->removed();
    e->view->setFrame(nullptr);
    e->view->release();
    [e->win orderOut:nil];
    [e->win close];
    if (e->px) {
        munmap(e->px, e->px_size);
        shm_unlink(e->px_name);
    }
    delete e;
}

void editor_move(HelperEditor *e, double x, double y)
{
    if (!e)
        return;
    e->top_x = x, e->top_y = y;
    e->place();
}

static NSEventModifierFlags mods(int32_t m)
{
    NSEventModifierFlags f = 0;
    if (m & 1)
        f |= NSEventModifierFlagShift;
    if (m & 2)
        f |= NSEventModifierFlagControl;
    if (m & 4)
        f |= NSEventModifierFlagOption;
    if (m & 8)
        f |= NSEventModifierFlagCommand;
    return f;
}

// NSEvent for a forwarded mouse/key input (any thread; nil for the wheel, which is dispatched on the main thread)
static NSEvent *make_event(HelperEditor *e, const tirb_input &in)
{
    NSPoint p = NSMakePoint(in.x, e->height - in.y); // view (top-left origin) -> window coordinates
    NSTimeInterval ts = [[NSProcessInfo processInfo] systemUptime];
    NSInteger wn = (NSInteger)e->win_number.load();
    NSEventModifierFlags mf = mods(in.modifiers);
    bool right = in.button == 1;
    switch (in.kind) {
    case 1: // down
    case 2: // up
    case 3: // drag
    case 4: // move
    case 8: // enter
    case 9: { // exit
        NSEventType t = in.kind == 1   ? (right ? NSEventTypeRightMouseDown : NSEventTypeLeftMouseDown)
                        : in.kind == 2 ? (right ? NSEventTypeRightMouseUp : NSEventTypeLeftMouseUp)
                        : in.kind == 3 ? (right ? NSEventTypeRightMouseDragged : NSEventTypeLeftMouseDragged)
                                       : NSEventTypeMouseMoved;
        return [NSEvent mouseEventWithType:t
                                  location:p
                             modifierFlags:mf
                                 timestamp:ts
                              windowNumber:wn
                                   context:nil
                               eventNumber:0
                                clickCount:in.click_count > 0 ? in.click_count : 1
                                  pressure:in.kind == 1 || in.kind == 3 ? 1.0f : 0.0f];
    }
    case 6:
    case 7: {
        NSString *chars = [NSString stringWithUTF8String:in.chars] ?: @"";
        return [NSEvent keyEventWithType:in.kind == 6 ? NSEventTypeKeyDown : NSEventTypeKeyUp
                                location:p
                           modifierFlags:mf
                               timestamp:ts
                            windowNumber:wn
                                 context:nil
                              characters:chars
             charactersIgnoringModifiers:chars
                               isARepeat:NO
                                keyCode:(unsigned short)in.key_code];
    }
    }
    return nil;
}

// main thread, normal run loop mode: deliver the input events we posted (nobody else dequeues: the helper runs
// CFRunLoopRun, not -[NSApp run]). Inside an AppKit tracking loop (another mode) the loop takes them itself.
static void drain(HelperEditor *e)
{
    NSString *mode = [[NSRunLoop currentRunLoop] currentMode];
    if (!mode || [mode isEqualToString:NSDefaultRunLoopMode]) {
        // only the input kinds we post: system events stay untouched (dequeuing everything ended the helper, E106)
        const NSEventMask ours = NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskLeftMouseDragged |
                                 NSEventMaskRightMouseDown | NSEventMaskRightMouseUp | NSEventMaskRightMouseDragged |
                                 NSEventMaskMouseMoved | NSEventMaskKeyDown | NSEventMaskKeyUp;
        NSEvent *q;
        while ((q = [NSApp nextEventMatchingMask:ours untilDate:nil inMode:NSDefaultRunLoopMode dequeue:YES])) {
            HelperEditor *owner = editor_for_window([q window]);
            if (owner) // our windows are never key (inactive helper): deliver to the window itself
                [owner->win sendEvent:q];
            else
                [NSApp sendEvent:q];
        }
    }
    if (std::find(g_editors.begin(), g_editors.end(), e) != g_editors.end())
        e->check_focus();
}

// Control thread. Mouse and key events go straight into the application's event queue (-postEvent:atStart: may be
// called from any thread): an AppKit tracking loop already running on the main thread (NSTextView mouse tracking
// after a click into a VSTGUI text field waits in -nextEventMatchingMask: for drag/up) receives them there; a main-
// queue block could not, because the main queue does not run while its current block (the mouse-down) is inside
// that loop (E106). Then a drain on the main thread delivers them in the normal case.
void editor_input_post(HelperEditor *e, const tirb_input &in)
{
    if (!e)
        return;
    if (in.kind == 5) { // wheel: main thread
        tirb_input copy = in;
        dispatch_async(dispatch_get_main_queue(), ^{
          if (std::find(g_editors.begin(), g_editors.end(), e) != g_editors.end())
              editor_input(e, copy);
        });
        return;
    }
    @autoreleasepool {
        NSEvent *ev = make_event(e, in);
        if (ev)
            [NSApp postEvent:ev atStart:NO];
    }
    dispatch_async(dispatch_get_main_queue(), ^{
      drain(e);
    });
}

void editor_input(HelperEditor *e, const tirb_input &in) // main thread
{
    if (!e)
        return;
    if (in.kind != 5) {
        NSEvent *ev = make_event(e, in);
        if (ev)
            [NSApp postEvent:ev atStart:NO];
        drain(e);
        return;
    }
    // wheel: a CGEvent placed on our window, handed to the view under the pointer
    NSPoint p = NSMakePoint(in.x, e->h - in.y);
    NSInteger wn = [e->win windowNumber];
    CGEventRef cg = CGEventCreateScrollWheelEvent2(nullptr, kCGScrollEventUnitPixel, 2, (int32_t)in.dy,
                                                   (int32_t)in.dx, 0);
    NSRect wr = [e->win frame];
    NSPoint sp = NSMakePoint(wr.origin.x + p.x, wr.origin.y + p.y);
    CGFloat main_h = NSMaxY([[[NSScreen screens] firstObject] frame]);
    CGEventSetLocation(cg, CGPointMake(sp.x, main_h - sp.y));
    CGEventSetIntegerValueField(cg, kCGMouseEventWindowUnderMousePointer, wn);
    CGEventSetIntegerValueField(cg, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, wn);
    CGEventSetFlags(cg, (CGEventFlags)mods(in.modifiers));
    NSEvent *ev = [NSEvent eventWithCGEvent:cg];
    CFRelease(cg);
    NSView *target = [e->container hitTest:p];
    if (target && ev)
        [target scrollWheel:ev];
}

bool editor_dump_png(HelperEditor *e, const char *path)
{
    if (!e || !e->px)
        return false;
    g_dirty = true;
    e->render();
    tirb_pixels *px = e->px;
    size_t bytes = (size_t)px->pw * px->ph * 4;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGDataProviderRef dp = CGDataProviderCreateWithData(nullptr, px->data + (px->front & 1) * bytes, bytes, nullptr);
    CGImageRef img = CGImageCreate(px->pw, px->ph, 8, 32, px->pw * 4, cs,
                                   kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, dp, nullptr, false,
                                   kCGRenderingIntentDefault);
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:img];
    NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    CGImageRelease(img);
    CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);
    return [png writeToFile:[NSString stringWithUTF8String:path] atomically:YES];
}

static HelperEditor *editor_for_window(NSWindow *w)
{
    for (HelperEditor *e : g_editors)
        if (e->win == w)
            return e;
    return nullptr;
}

static void serialize_menu(NSMenu *m, int depth, std::vector<tirb_menu_item> &out)
{
    for (NSMenuItem *it in [m itemArray]) {
        if ([it isHidden])
            continue;
        tirb_menu_item mi{};
        mi.depth = depth;
        mi.flags = ([it isEnabled] ? 1 : 0) | ([it state] == NSControlStateValueOn ? 2 : 0) |
                   ([it isSeparatorItem] ? 4 : 0) | ([it hasSubmenu] ? 8 : 0) |
                   ([it state] == NSControlStateValueMixed ? 16 : 0);
        const char *t = [[it title] UTF8String];
        strncpy(mi.title, t ? t : "", sizeof mi.title - 1);
        out.push_back(mi);
        if ([it hasSubmenu])
            serialize_menu([it submenu], depth + 1, out);
    }
}

static NSMenuItem *visible_item(NSMenu *m, int idx) // index among the non-hidden items (as serialized)
{
    int k = 0;
    for (NSMenuItem *it in [m itemArray]) {
        if ([it isHidden])
            continue;
        if (k++ == idx)
            return it;
    }
    return nil;
}

static void swz_perform_click(id self, SEL sel, NSRect frame, NSView *view)
{
    HelperEditor *e = editor_for_window([view window]);
    if (!e || !e->notify) { // not one of our mirrored editors: behave as usual
        ((void (*)(id, SEL, NSRect, NSView *))g_orig_perform_click)(self, sel, frame, view);
        return;
    }
    NSPopUpButtonCell *cell = (NSPopUpButtonCell *)self;
    NSMenu *menu = [cell menu];
    std::vector<tirb_menu_item> items;
    serialize_menu(menu, 0, items);
    NSRect r = [view convertRect:frame toView:nil]; // window coordinates (bottom-left origin) = mirror view coordinates
    tirb_menu_hdr h{};
    h.x = (float)r.origin.x;
    h.y = (float)(e->h - (r.origin.y + r.size.height));
    h.w = (float)r.size.width, h.h = (float)r.size.height;
    h.count = (int32_t)items.size();
    NSMenuItem *sel_item = [cell selectedItem];
    h.selected = -1;
    if (sel_item) {
        int k = 0;
        for (NSMenuItem *it in [menu itemArray]) {
            if ([it isHidden])
                continue;
            if (it == sel_item)
                h.selected = k;
            k++;
        }
    }
    static uint32_t next_id;
    h.menu_id = ++next_id;
    std::vector<uint8_t> msg(sizeof h + items.size() * sizeof(tirb_menu_item));
    memcpy(msg.data(), &h, sizeof h);
    if (!items.empty())
        memcpy(msg.data() + sizeof h, items.data(), items.size() * sizeof(tirb_menu_item));
    e->menu_done = false;
    e->menu_id = h.menu_id;
    e->notify(e->ctx, TIRB_N_MENU, msg.data(), (uint32_t)msg.size());
    // wait for the bridge's answer; the main queue (other calls into the plug-in) is served meanwhile
    CFAbsoluteTime until = CFAbsoluteTimeGetCurrent() + 300;
    while (!e->menu_done && CFAbsoluteTimeGetCurrent() < until)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    tirb_menu_result res = e->menu_res;
    e->menu_id = 0;
    if (!e->menu_done || res.n <= 0)
        return; // cancelled
    NSMenu *m = menu;
    NSMenuItem *it = nil;
    for (int level = 0; level < res.n && level < 8 && m; level++) {
        it = visible_item(m, res.idx[level]);
        if (!it)
            return;
        if (level < res.n - 1)
            m = [it submenu];
    }
    if (!it || ![it isEnabled])
        return;
    NSMenu *owner = [it menu];
    if (owner == menu)
        [cell selectItem:it];
    [owner performActionForItemAtIndex:[owner indexOfItem:it]];
    g_dirty = true;
}

void editor_menu_result(HelperEditor *e, const tirb_menu_result &r)
{
    if (!e || r.menu_id != e->menu_id)
        return;
    e->menu_res = r;
    e->menu_done = true;
    CFRunLoopWakeUp(CFRunLoopGetMain());
}

const char *editor_first_responder(HelperEditor *e) // diagnostics (VTI_SCRIPT "fr")
{
    static char buf[256];
    NSResponder *r = e ? [e->win firstResponder] : nil;
    snprintf(buf, sizeof buf, "%s%s", r ? [NSStringFromClass([r class]) UTF8String] : "(none)",
             [r isKindOfClass:[NSText class]] ? [[NSString stringWithFormat:@" text=\"%@\"", [(NSText *)r string]]
                                                    UTF8String]
                                              : "");
    return buf;
}

const char *editor_end_editing(HelperEditor *e) // diagnostics (VTI_SCRIPT "endedit"): what a click elsewhere does
{
    BOOL ok = [e->win makeFirstResponder:nil];
    return ok ? "resigned" : "refused";
}

const char *editor_text_command(HelperEditor *e, const char *sel) // diagnostics (VTI_SCRIPT "cmd insertNewline:")
{
    NSResponder *r = [e->win firstResponder];
    if (![r isKindOfClass:[NSTextView class]])
        return "no text view";
    NSTextView *tv = (NSTextView *)r;
    SEL s = NSSelectorFromString([NSString stringWithUTF8String:sel]);
    id tf = [tv delegate];
    if ([tf respondsToSelector:@selector(control:textView:doCommandBySelector:)]) {
        Ivar iv = class_getInstanceVariable([tf class], "_textEdit");
        void *pte = iv ? *(void **)((char *)(__bridge void *)tf + ivar_getOffset(iv)) : nullptr; // C++ pointer ivar
        if (pte) {
            void **cb = *(void ***)((char *)pte + 0x10); // object the handler calls (vtable +0x40 = loose focus)
            void **vt = *(void ***)cb;
            for (int k = 0; k < 12; k++) {
                Dl_info di{};
                dladdr(vt[k], &di);
                printf("  callback vtable[%d] +0x%x = %s + 0x%lx\n", k, k * 8,
                       di.dli_fname ? strrchr(di.dli_fname, '/') + 1 : "?",
                       (unsigned long)((char *)vt[k] - (char *)di.dli_fbase));
            }
            void *frame = *(void **)((char *)cb - 0xf8); // CTextEdit (cb - 0x148) + 0x50 = CFrame*
            void **fvt = *(void ***)frame;
            for (int k : {97, 98, 99}) {
                Dl_info fi{};
                dladdr(fvt[k], &fi);
                printf("  frame vtable[%d] +0x%x = %s + 0x%lx\n", k, k * 8, fi.dli_fname ? strrchr(fi.dli_fname, '/') + 1 : "?",
                       (unsigned long)((char *)fvt[k] - (char *)fi.dli_fbase));
            }
            Dl_info di{};
            dladdr((void *)vt, &di);
            printf("  callback vtable at %s + 0x%lx\n", di.dli_fname ? strrchr(di.dli_fname, '/') + 1 : "?",
                   (unsigned long)((char *)vt - (char *)di.dli_fbase));
        }
        BOOL ok = [tf control:tf textView:tv doCommandBySelector:s];
        printf("direct control:textView:doCommandBySelector: -> %d\n", ok);
        return "sent to delegate";
    }
    [tv doCommandBySelector:s];
    return "sent";
}

static void dump_methods(Class c, const char *what)
{
    for (; c && c != [NSObject class]; c = class_getSuperclass(c)) {
        unsigned n = 0;
        Method *m = class_copyMethodList(c, &n);
        printf("  %s %s: %u methods:", what, class_getName(c), n);
        if (strncmp(class_getName(c), "NS", 2)) // only the plug-in's own classes in full
            for (unsigned i = 0; i < n; i++)
                printf(" %s", sel_getName(method_getName(m[i])));
        printf("\n");
        free(m);
    }
}

void editor_inspect_text(HelperEditor *e) // diagnostics (VTI_SCRIPT "inspect")
{
    NSResponder *r = [e->win firstResponder];
    printf("first responder %s\n", r ? class_getName([r class]) : "-");
    if (![r isKindOfClass:[NSTextView class]])
        return;
    NSTextView *tv = (NSTextView *)r;
    id d = [tv delegate];
    for (const char *sn : {"control:textView:doCommandBySelector:", "removeFromSuperview", "initWithTextEdit:"}) {
        IMP imp = class_getMethodImplementation(objc_getClass("VSTGUI_NSTextField"), sel_getUid(sn));
        Dl_info di{};
        dladdr((void *)imp, &di);
        printf("IMP %s = %s + 0x%lx\n", sn, di.dli_fname ? strrchr(di.dli_fname, '/') + 1 : "?",
               (unsigned long)((char *)imp - (char *)di.dli_fbase));
    }
    printf("field editor delegate %s %p, fieldEditor %d\n", d ? class_getName([d class]) : "-", (__bridge void *)d,
           [tv isFieldEditor]);
    if (d) {
        dump_methods([d class], "delegate");
        if ([d respondsToSelector:@selector(delegate)]) {
            id dd = [(NSTextField *)d delegate];
            printf("text field delegate %s %p\n", dd ? class_getName([dd class]) : "-", (__bridge void *)dd);
            if (dd)
                dump_methods([dd class], "tf-delegate");
        }
    }
    fflush(stdout);
}
