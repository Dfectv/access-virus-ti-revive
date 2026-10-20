// popupwindow_win.cpp - popup menu windows on Windows: a WS_POPUP tool window owned by the editor's top-level window,
// topmost and never activated, with the menu shadow (CS_DROPSHADOW) - the same kind of window as the original's menus.
// See popupwindow.h.
#include "popupwindow.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/lib/platform/iplatformframecallback.h"
#include <windows.h>
#include <windowsx.h>
#include <cmath>

using namespace VSTGUI;

namespace vti {

namespace {

HINSTANCE module_instance()
{
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&module_instance), &m);
    return m;
}

const wchar_t *kClass = L"TIRevivePopupMenu", *kProp = L"TIRevivePopupMenu";

LRESULT CALLBACK popup_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE; // a click must not take the activation from the host's window
    case WM_ERASEBKGND: return 1;                // the frame inside paints everything
    default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// A DLL's window classes stay registered after it is unloaded; the class goes when the plug-in DLL detaches (a static
// object's destructor), so a reload at another address does not meet a class whose window procedure is gone.
struct ClassUnregister {
    bool registered = false;
    ~ClassUnregister()
    {
        if (registered)
            UnregisterClassW(kClass, module_instance());
    }
};
static ClassUnregister class_unregister;

bool register_class()
{
    static bool done = false;
    if (done)
        return true;
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = popup_proc;
    wc.hInstance = module_instance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    const bool ours = RegisterClassExW(&wc) != 0;
    class_unregister.registered = ours;
    done = ours || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    return done;
}

HWND hwnd_of(CFrame *frame)
{
    auto *pf = frame ? frame->getPlatformFrame() : nullptr;
    return pf ? static_cast<HWND>(pf->getPlatformRepresentation()) : nullptr;
}

class WinPopupWindow : public PopupWindow {
public:
    ~WinPopupWindow() override
    {
        if (child_ && old_proc_) {
            SetWindowLongPtrW(child_, GWLP_WNDPROC, (LONG_PTR)old_proc_);
            RemovePropW(child_, kProp);
        }
        if (frame_)
            frame_->forget(); // destroys VSTGUI's child window
        if (hwnd_)
            DestroyWindow(hwnd_);
    }
    void move(const NativeRect &r) override
    {
        SetWindowPos(hwnd_, HWND_TOPMOST, r.x, r.y, r.w, r.h, SWP_NOACTIVATE);
    }
    CFrame *frame() const override { return frame_; }
    void show() override
    {
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    void hide() override { ShowWindow(hwnd_, SW_HIDE); }

    // VSTGUI's frame window: a button press goes to the frame without VSTGUI's SetFocus, which would activate the
    // popup (and deactivate the host's window) - a menu never takes the focus
    static LRESULT CALLBACK child_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto *self = static_cast<WinPopupWindow *>(GetPropW(hwnd, kProp));
        if (!self)
            return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDBLCLK: {
            MouseDownEvent ev;
            ev.mousePosition(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            ev.clickCount = 1;
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
                ev.buttonState.add(MouseButton::Left);
            else if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK)
                ev.buttonState.add(MouseButton::Right);
            else
                ev.buttonState.add(MouseButton::Middle);
            static_cast<IPlatformFrameCallback *>(self->frame_)->platformOnEvent(ev);
            return 0;
        }
        default: return CallWindowProcW(self->old_proc_, hwnd, msg, wp, lp);
        }
    }

    HWND hwnd_ = nullptr, child_ = nullptr;
    WNDPROC old_proc_ = nullptr;
    CFrame *frame_ = nullptr;
};

} // namespace

std::unique_ptr<PopupWindow> PopupWindow::create(CFrame *editor, const NativeRect &r, int w, int h, double scale,
                                                 CView *content, IPlatformFrameConfig *config)
{
    HWND owner = hwnd_of(editor);
    if (!owner || !register_class()) {
        content->forget();
        return nullptr;
    }
    owner = GetAncestor(owner, GA_ROOT); // above the host's window, and gone with it
    auto win = std::make_unique<WinPopupWindow>();
    win->hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClass, L"", WS_POPUP, r.x, r.y,
                                 r.w, r.h, owner, nullptr, module_instance(), nullptr);
    if (!win->hwnd_) {
        content->forget();
        return nullptr;
    }
    auto *frame = new CFrame(CRect(0, 0, w, h), nullptr);
    if (!frame->open(win->hwnd_, PlatformType::kHWND, config)) {
        frame->forget();
        content->forget();
        return nullptr; // the destructor closes the window
    }
    win->frame_ = frame;
    if (scale != 1.0)
        frame->setZoom(scale);
    frame->addView(content);
    if ((win->child_ = hwnd_of(frame))) {
        SetPropW(win->child_, kProp, win.get());
        win->old_proc_ = (WNDPROC)SetWindowLongPtrW(win->child_, GWLP_WNDPROC, (LONG_PTR)&WinPopupWindow::child_proc);
    }
    return win;
}

bool popup_screen(CFrame *editor, CPoint at, double, PopupScreen &out)
{
    HWND hwnd = hwnd_of(editor);
    RECT wr;
    if (!hwnd || !GetWindowRect(hwnd, &wr))
        return false;
    const double zoom = editor->getZoom(); // the frame's pixels per editor pixel (display scale x user size)
    out.anchor_x = wr.left + (int)std::lround(at.x * zoom);
    out.anchor_y = wr.top + (int)std::lround(at.y * zoom);
    // the menus at the display scale of the editor's monitor, like the system's menus (not the user's editor size)
    const UINT dpi = GetDpiForWindow(hwnd);
    out.scale = dpi ? dpi / 96.0 : 1.0;
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{out.anchor_x, out.anchor_y}, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    out.work = {mi.rcWork.left, mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top};
    return true;
}

bool popup_pointer(CFrame *, int &x, int &y, bool &down)
{
    POINT p;
    if (!GetCursorPos(&p))
        return false;
    x = p.x;
    y = p.y;
    down = ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000) != 0;
    return true;
}

uintptr_t popup_focus_token(CFrame *) { return (uintptr_t)GetForegroundWindow(); }

int popup_submenu_delay()
{
    UINT ms = 400; // Windows' default
    SystemParametersInfoW(SPI_GETMENUSHOWDELAY, 0, &ms, 0);
    return (int)ms;
}

} // namespace vti
