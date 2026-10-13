/* xtype - type text (+ Return) via XTest (test helper for driving the original editor on Xvfb).
   usage: xtype "text" [noreturn]      build: cc -o xtype xtype.c -lX11 -lXtst */
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void key(Display *d, KeySym ks, int shift)
{
    KeyCode kc = XKeysymToKeycode(d, ks), sh = XKeysymToKeycode(d, XK_Shift_L);
    if (shift)
        XTestFakeKeyEvent(d, sh, True, 0);
    XTestFakeKeyEvent(d, kc, True, 0);
    XTestFakeKeyEvent(d, kc, False, 0);
    if (shift)
        XTestFakeKeyEvent(d, sh, False, 0);
    XFlush(d);
    usleep(40000);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    Display *d = XOpenDisplay(NULL);
    if (!d)
        return 1;
    /* no window manager on Xvfb: give the top-level window under the pointer the keyboard focus */
    Window root = DefaultRootWindow(d), r, child = 0;
    int rx, ry, wx, wy;
    unsigned mask;
    if (XQueryPointer(d, root, &r, &child, &rx, &ry, &wx, &wy, &mask) && child) {
        XSetInputFocus(d, child, RevertToParent, CurrentTime);
        XFlush(d);
        usleep(100000);
    }
    for (const char *p = argv[1]; *p; p++) {
        char nm[2] = {*p, 0};
        key(d, *p == ' ' ? XK_space : XStringToKeysym(nm), *p >= 'A' && *p <= 'Z');
    }
    if (argc < 3)
        key(d, XK_Return, 0);
    XCloseDisplay(d);
    return 0;
}
