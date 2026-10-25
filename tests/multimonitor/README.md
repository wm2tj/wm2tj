# Multi-monitor test

Runs wm2tj and wm2tj-bar on a second, invisible X server (the Xorg dummy
driver) and checks, for several monitor layouts (stacked, side by side,
L-shaped, 2x2, portrait, mirrored, a monitor added while running, side
panels): where the bars go (`monitors primary` / `monitors all`), that a new
window opens on the pointer's monitor, and that maximizing stays within the
monitor and clear of its bar and panels.

Needs `xserver-xorg-video-dummy xdotool x11-apps libxrandr-dev`.

```sh
cc -O2 -o netstate netstate.c -lX11      # sends _NET_WM_STATE (xdotool 3.2016 cannot)
cc -O2 -o dock dock.c -lX11              # a dock panel, optionally with a strut
/usr/lib/xorg/Xorg :8 -config dummy.conf -configdir . -logfile xorg8.log -noreset -nolisten tcp &
python3 run.py ../../wm/wm2tj ../../bar/wm2tj-bar
```

Run it from this directory (Xorg takes a relative `-config` only). The
monitors are virtual ones made with `xrandr --setmonitor`; the dummy driver
has one usable output. wm2tj and the bar get a `HOME` of their own under
this directory (copy a `.wm2rc` to `home/` to test with your settings).
