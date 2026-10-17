// popupwindow_mac.mm - popup menu windows on macOS: a borderless non-activating NSPanel at the menu window level,
// so the host's window stays key and keeps the keyboard. Compiled with ARC. See popupwindow.h.
#include "popupwindow.h"
#import <Cocoa/Cocoa.h>
#include <cmath>

using namespace VSTGUI;

namespace vti {

namespace {

// native coordinates are points with y down from the top of the main screen (the first one), like VSTGUI's
double main_height() { return NSScreen.screens.count ? NSScreen.screens[0].frame.size.height : 0; }

NSRect to_cocoa(const NativeRect &r)
{
    return NSMakeRect(r.x, main_height() - (r.y + r.h), r.w, r.h);
}

NSView *view_of(CFrame *frame)
{
    auto *pf = frame ? frame->getPlatformFrame() : nullptr;
    return pf ? (__bridge NSView *)pf->getPlatformRepresentation() : nil;
}

class MacPopupWindow : public PopupWindow {
public:
    ~MacPopupWindow() override
    {
        if (frame_)
            frame_->forget(); // removes VSTGUI's view from the panel
        [panel_ orderOut:nil];
        [panel_ close];
        panel_ = nil;
    }
    void move(const NativeRect &r) override { [panel_ setFrame:to_cocoa(r) display:YES]; }
    void show() override { [panel_ orderFront:nil]; }
    void hide() override { [panel_ orderOut:nil]; }

    NSPanel *panel_ = nil;
    CFrame *frame_ = nullptr;
};

} // namespace

std::unique_ptr<PopupWindow> PopupWindow::create(CFrame *editor, const NativeRect &r, int w, int h, double scale,
                                                 CView *content, IPlatformFrameConfig *config)
{
    if (!view_of(editor)) {
        content->forget();
        return nullptr;
    }
    auto win = std::make_unique<MacPopupWindow>();
    win->panel_ = [[NSPanel alloc] initWithContentRect:to_cocoa(r)
                                             styleMask:NSWindowStyleMaskBorderless |
                                                       NSWindowStyleMaskNonactivatingPanel
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
    NSPanel *p = win->panel_;
    p.releasedWhenClosed = NO;
    p.level = NSPopUpMenuWindowLevel;
    p.hasShadow = YES;
    p.opaque = YES;
    p.hidesOnDeactivate = NO; // the editor closes the menu itself (popup_focus_token)
    p.becomesKeyOnlyIfNeeded = YES;
    p.collectionBehavior = NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorIgnoresCycle;

    auto *frame = new CFrame(CRect(0, 0, w, h), nullptr);
    if (!frame->open((__bridge void *)p.contentView, PlatformType::kNSView, config)) {
        frame->forget();
        content->forget();
        return nullptr;
    }
    win->frame_ = frame;
    if (scale != 1.0)
        frame->setZoom(scale);
    frame->addView(content);
    return win;
}

bool popup_screen(CFrame *editor, CPoint at, double, PopupScreen &out)
{
    CCoord gx = 0, gy = 0;
    if (!view_of(editor) || !editor->getPosition(gx, gy))
        return false;
    const double zoom = editor->getZoom(); // points per editor pixel (the user's size; Cocoa does the backing scale)
    out.anchor_x = (int)std::lround(gx + at.x * zoom);
    out.anchor_y = (int)std::lround(gy + at.y * zoom);
    out.scale = 1.0;
    const NSPoint p = NSMakePoint(out.anchor_x, main_height() - out.anchor_y);
    NSScreen *screen = nil;
    for (NSScreen *s in NSScreen.screens)
        if (NSPointInRect(p, s.frame)) {
            screen = s;
            break;
        }
    if (!screen)
        screen = view_of(editor).window.screen ?: NSScreen.mainScreen;
    if (!screen)
        return false;
    const NSRect v = screen.visibleFrame; // without the menu bar and the Dock
    out.work = {(int)std::ceil(v.origin.x), (int)std::ceil(main_height() - (v.origin.y + v.size.height)),
                (int)std::floor(v.size.width), (int)std::floor(v.size.height)};
    return true;
}

bool popup_pointer(CFrame *, int &x, int &y, bool &down)
{
    const NSPoint p = NSEvent.mouseLocation;
    x = (int)std::floor(p.x);
    y = (int)std::floor(main_height() - p.y);
    down = NSEvent.pressedMouseButtons != 0;
    return true;
}

// the active application: switching to another one (Cmd-Tab) closes the menu
uintptr_t popup_focus_token(CFrame *) { return NSApp.isActive ? 1 : 2; }

int popup_submenu_delay() { return 400; }

} // namespace vti
