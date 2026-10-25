/* dock <x> <y> <w> <h> [l r t b ly0 ly1 ry0 ry1 tx0 tx1 bx0 bx1]: a dock window, with a partial strut if given; stays until killed */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    Display *d = XOpenDisplay(0);
    if (!d || argc < 5) return 1;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), atoi(argv[1]), atoi(argv[2]),
				   atoi(argv[3]), atoi(argv[4]), 0, 0, 0x808080);
    Atom type = XInternAtom(d, "_NET_WM_WINDOW_TYPE", False), dock = XInternAtom(d, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(d, w, type, XA_ATOM, 32, PropModeReplace, (unsigned char *)&dock, 1);
    if (argc >= 17) {
	long s[12];
	for (int i = 0; i < 12; ++i) s[i] = atol(argv[5 + i]);
	XChangeProperty(d, w, XInternAtom(d, "_NET_WM_STRUT_PARTIAL", False), XA_CARDINAL, 32,
			PropModeReplace, (unsigned char *)s, 12);
    }
    XMapWindow(d, w);
    XFlush(d);
    for (;;) pause();
}
