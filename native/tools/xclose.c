// xclose - what a window manager does when the user clicks a window's close button, for tests in a virtual display
// without one (native/tests/gui_test.sh): sends WM_DELETE_WINDOW to the largest window with that title (GTK has
// small helper windows with the same name). Waits up to 10 s for the window.
//   xclose TITLE [--size]   --size: only prints "window ID WxH"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static Window best;
static int best_area;

static void find(Display *d, Window w, const char *name)
{
    char *n = NULL;
    if (XFetchName(d, w, &n) && n) {
        int hit = !strcmp(n, name);
        XFree(n);
        XWindowAttributes a;
        if (hit && XGetWindowAttributes(d, w, &a) && a.width * a.height > best_area)
            best = w, best_area = a.width * a.height;
    }
    Window root, parent, *kids = NULL;
    unsigned nk = 0;
    if (!XQueryTree(d, w, &root, &parent, &kids, &nk))
        return;
    for (unsigned i = 0; i < nk; i++)
        find(d, kids[i], name);
    if (kids)
        XFree(kids);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: xclose TITLE [--size]\n");
        return 2;
    }
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "xclose: no display\n");
        return 2;
    }
    for (int i = 0; i < 100 && !best; i++) {
        find(d, DefaultRootWindow(d), argv[1]);
        if (!best)
            usleep(100000);
    }
    if (!best) {
        fprintf(stderr, "xclose: no window \"%s\"\n", argv[1]);
        return 1;
    }
    XWindowAttributes a;
    XGetWindowAttributes(d, best, &a);
    printf("window %lx %dx%d\n", best, a.width, a.height);
    fflush(stdout);
    if (argc > 2 && !strcmp(argv[2], "--size"))
        return 0;
    XEvent e;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = best;
    e.xclient.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = (long)XInternAtom(d, "WM_DELETE_WINDOW", False);
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(d, best, False, NoEventMask, &e);
    XFlush(d);
    XCloseDisplay(d);
    return 0;
}
