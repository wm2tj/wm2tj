/* wm2-swipe: three-finger horizontal touchpad swipe -> switch virtual desktop.
 *
 * Reads the X server's own XInput 2.4 gesture events (Xorg 21+, libinput),
 * so it needs neither root nor the "input" group, and asks the window
 * manager to switch through _NET_CURRENT_DESKTOP, so it works with wm2tj
 * and any other EWMH window manager.  Started by wm2-session when installed.
 *
 *   wm2-swipe [-f fingers] [-t threshold] [-r] [-w]
 *     -f N   fingers of the swipe (default 3)
 *     -t N   minimum horizontal travel, in unaccelerated motion units (default 60)
 *     -r     reverse: swipe left = next desktop (GNOME/macOS "natural" direction).
 *            By default swipe right = next, swipe left = previous
 *     -w     wrap around at the first and last desktop (default: stop)
 *
 * Copyright (c) 2026 tetsu.  MIT licence, see COPYING.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static long rootCard(Display *d, Window r, Atom a, long dflt)
{
    Atom t; int f; unsigned long n, after; unsigned char *p = NULL; long v = dflt;
    if (XGetWindowProperty(d, r, a, 0, 1, False, XA_CARDINAL, &t, &f, &n, &after, &p) == Success
        && p && n == 1) v = *(long *)p;
    if (p) XFree(p);
    return v;
}

static void usage(int status)
{
    fputs("usage: wm2-swipe [-f fingers] [-t threshold] [-r] [-w]\n"
          "  -f N  fingers (default 3)   -t N  minimum travel (default 60)\n"
          "  -r    swipe left = next desktop (default: swipe right = next)\n"
          "  -w    wrap around at the ends\n", status ? stderr : stdout);
    exit(status);
}

int main(int argc, char **argv)
{
    int fingers_wanted = 3, reverse = 0, wrap = 0, opt;
    double threshold = 60.0;
    while ((opt = getopt(argc, argv, "f:t:rwh")) != -1) {
        switch (opt) {
        case 'f': fingers_wanted = atoi(optarg); break;
        case 't': threshold = atof(optarg); break;
        case 'r': reverse = 1; break;
        case 'w': wrap = 1; break;
        case 'h': usage(0); break;
        default: usage(2);
        }
    }
    if (fingers_wanted < 1 || threshold <= 0) usage(2);

    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "wm2-swipe: cannot open display\n"); return 1; }
    int xi, ev, er, major = 2, minor = 4;
    if (!XQueryExtension(d, "XInputExtension", &xi, &ev, &er) ||
        XIQueryVersion(d, &major, &minor) != Success || major * 10 + minor < 24) {
        fprintf(stderr, "wm2-swipe: the X server has no XInput 2.4 gestures (Xorg 21 or later)\n");
        return 1;
    }
    Window root = DefaultRootWindow(d);
    Atom cur = XInternAtom(d, "_NET_CURRENT_DESKTOP", False);
    Atom num = XInternAtom(d, "_NET_NUMBER_OF_DESKTOPS", False);

    unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {0};
    XISetMask(bits, XI_GestureSwipeBegin);
    XISetMask(bits, XI_GestureSwipeUpdate);
    XISetMask(bits, XI_GestureSwipeEnd);
    XIEventMask mask = { XIAllMasterDevices, sizeof bits, bits };
    /* Passive grab on root: otherwise a client that selects gestures itself
     * (GTK applications, for instance) swallows the swipe and root never sees it. */
    XIGrabModifiers any = { XIAnyModifier, 0 };
    if (XIGrabSwipeGestureBegin(d, XIAllMasterDevices, root, XIGrabModeAsync, XIGrabModeAsync,
                                False, &mask, 1, &any) != 0) {
        fprintf(stderr, "wm2-swipe: gesture grab failed (another gesture program running?), "
                        "listening on the root window instead\n");
        XISelectEvents(d, root, &mask, 1);
    }
    XSync(d, False);

    double dx = 0, dy = 0;
    int fingers = 0;
    for (;;) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.type != GenericEvent || e.xcookie.extension != xi || !XGetEventData(d, &e.xcookie))
            continue;
        XIGestureSwipeEvent *g = e.xcookie.data;
        switch (e.xcookie.evtype) {
        case XI_GestureSwipeBegin:
            dx = dy = 0; fingers = g->detail; break;
        case XI_GestureSwipeUpdate:
            dx += g->delta_unaccel_x; dy += g->delta_unaccel_y; break;
        case XI_GestureSwipeEnd:
            if (fingers == fingers_wanted && !(g->flags & XIGestureSwipeEventCancelled)
                && fabs(dx) >= threshold && fabs(dx) > fabs(dy)) {
                long n = rootCard(d, root, num, 1), c = rootCard(d, root, cur, 0);
                long step = (dx > 0) ? 1 : -1;
                if (reverse) step = -step;
                long t = c + step;
                if (wrap && n > 0) t = (t + n) % n;
                if (t >= 0 && t < n && t != c) {
                    XEvent m = {0};
                    m.xclient.type = ClientMessage;
                    m.xclient.window = root;
                    m.xclient.message_type = cur;
                    m.xclient.format = 32;
                    m.xclient.data.l[0] = t;
                    m.xclient.data.l[1] = g->time;
                    XSendEvent(d, root, False,
                               SubstructureRedirectMask | SubstructureNotifyMask, &m);
                    XFlush(d);
                }
            }
            fingers = 0; break;
        }
        XFreeEventData(d, &e.xcookie);
    }
}
