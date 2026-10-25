/* netstate <window> <add|remove> <STATE> [STATE]: send _NET_WM_STATE like a pager */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(0);
    XEvent e;
    char name[128];
    if (!d || argc < 4) return 1;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = strtoul(argv[1], 0, 0);
    e.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = !strcmp(argv[2], "add") ? 1 : !strcmp(argv[2], "remove") ? 0 : 2;
    snprintf(name, sizeof name, "_NET_WM_STATE_%s", argv[3]);
    e.xclient.data.l[1] = XInternAtom(d, name, False);
    if (argc > 4) {
	snprintf(name, sizeof name, "_NET_WM_STATE_%s", argv[4]);
	e.xclient.data.l[2] = XInternAtom(d, name, False);
    }
    e.xclient.data.l[3] = 2;
    XSendEvent(d, DefaultRootWindow(d), False,
	       SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XCloseDisplay(d);
    return 0;
}
