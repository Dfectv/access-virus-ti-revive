// popupwindow.h - top-level windows for the editor's popup menus: one borderless window per open menu (root and each
// submenu), like the original's Windows menus, which are windows of their own and may use the whole screen (round
// 0.8.22 test L4: menus drawn inside the plug-in window were cut off by it and pushed over the editor). Each window
// hosts a VSTGUI frame with the menu's view; the windows never take the keyboard focus or activate, so the keys stay
// with the editor. Windows: a WS_POPUP window, macOS: a borderless non-activating NSPanel, Linux: an override-redirect
// X11 window. The menu logic stays in editor/popupmenu.h.
#pragma once
#include "vstgui/lib/cframe.h"
#include <cstdint>
#include <memory>

namespace vti {

// native screen coordinates: Windows pixels (as the host's thread sees them), macOS points with y down from the top
// of the main screen, X11 root window pixels
struct NativeRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

class PopupWindow {
public:
    // a hidden window at `r` above the editor's window with a frame of w x h menu pixels drawn at `scale` (native
    // units per menu pixel) showing `content` (the frame owns it); `config`: the platform frame config (X11: the run
    // loop). nullptr when the platform cannot make one (the menu is drawn inside the editor then).
    static std::unique_ptr<PopupWindow> create(VSTGUI::CFrame *editor, const NativeRect &r, int w, int h, double scale,
                                               VSTGUI::CView *content, VSTGUI::IPlatformFrameConfig *config);
    virtual ~PopupWindow() = default; // closes the frame and the window
    virtual void move(const NativeRect &r) = 0;
    virtual void show() = 0;
    virtual void hide() = 0;
};

struct PopupScreen {
    NativeRect work;                // the work area of the monitor at the anchor (without task bar / menu bar)
    double scale = 1;               // native units per menu pixel
    int anchor_x = 0, anchor_y = 0; // the anchor
};
// the screen around the editor frame's point `at` (frame coordinates before its zoom); `content_scale`: the host's
// display scale (used where the platform has no better source); false when unknown
bool popup_screen(VSTGUI::CFrame *editor, VSTGUI::CPoint at, double content_scale, PopupScreen &out);

// the pointer's position and whether a mouse button is down; false when unknown
bool popup_pointer(VSTGUI::CFrame *editor, int &x, int &y, bool &down);

// the window or application that has the user's focus (0: the platform cannot tell); a menu closes when it changes,
// like a native menu when the user switches to another window with the keyboard
uintptr_t popup_focus_token(VSTGUI::CFrame *editor);

// Windows' menu show delay (ms) before a submenu opens or switches; the same default elsewhere
int popup_submenu_delay();

} // namespace vti
