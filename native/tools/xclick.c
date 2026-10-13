/* xclick - click at absolute screen coordinates via XTest (test helper for editor comparisons on Xvfb).
   usage: xclick x y [button]          build: cc -o xclick xclick.c -lX11 -lXtst */
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: xclick x y [button]\n");
        return 2;
    }
    Display *d = XOpenDisplay(NULL);
    if (!d)
        return 1;
    int b = argc > 3 ? atoi(argv[3]) : 1;
    XTestFakeMotionEvent(d, -1, atoi(argv[1]), atoi(argv[2]), 0);
    XFlush(d);
    usleep(50000);
    XTestFakeButtonEvent(d, b, True, 0);
    XFlush(d);
    usleep(80000);
    XTestFakeButtonEvent(d, b, False, 0);
    XFlush(d);
    XCloseDisplay(d);
    return 0;
}
