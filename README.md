# wm2tj

A minimal X11 window manager based on [wm2](https://github.com/cannam/wm2) by Chris Cannam.

**Status: beta (0.1.0).** Used daily on a few Xubuntu laptops; other
distributions and compilers are untested (see "Known limitations").

wm2 (1997) gives every window a vertical tab on its left side and does almost
nothing else. I have used it for years whenever I put Linux on an underpowered
machine, and I still think it is one of the coolest-looking window managers
around. wm2tj keeps that look and that restraint, and adds what you need to
use it every day on a modern laptop:

- Window titles rendered with Xft and UTF-8 (Japanese and other non-Latin titles work)
- Maximise / restore (double-click the tab) and minimise (middle-click the tab)
- EWMH support: panels, taskbars, pagers, virtual desktops, struts, fullscreen
- Configurable look in `~/.wm2rc`: tab width and length, gloss colour, borders, fonts
- A root menu with cascading folders and a search box (finds apps and open windows; works with an input method)
- Reload the configuration in place with `pkill -HUP -x wm2tj`, keeping the current desktop
- No polling while moving windows

It comes with **wm2tj-bar**, a top bar drawn as a wm2tj tab laid on its side:
app menu, task list, virtual desktops, system tray, CPU / memory / Wi-Fi /
Bluetooth / volume / battery with their own menus, brightness and volume keys,
and a clock. Plain C and Xlib; English UI with a Japanese translation.

![wm2tj-bar](bar/screenshot.png)

## Layout

| Directory | Contents |
|---|---|
| `wm/` | wm2tj, the window manager (C++). `wm/README` is the original wm2 README; `wm/README-ja.txt` describes the changes (Japanese) |
| `wm/extras/` | menu generator, settings GUIs, `session-logout`, `power-dialog`, wallpaper, systemd units. See `wm/extras/README-ja.txt` |
| `bar/` | wm2tj-bar, the top bar (C). `bar/README.md` (English) / `bar/README.ja.md` (Japanese) |

## Building

Debian / Ubuntu:

```
sudo apt install g++ gcc make pkg-config libx11-dev libxext-dev libxft-dev picom
make -C wm && sudo make -C wm install
make -C bar && sudo make -C bar install
make -C wm/extras install PREFIX=$HOME/.local   # optional extras
```

Package names for Fedora, Arch and Alpine are listed in `wm/README-ja.txt`
and `bar/README.md`. `make -C wm install-session` adds a "wm2tj" entry to the
display manager's session list.

## Known limitations (beta)

- Tested on Xubuntu-based systems only. clang++ and musl (Alpine) compile in
  theory but have not been built; NixOS, Void and Gentoo are untested
- Volume control needs `wpctl` or `pactl` (plain ALSA is not supported)
- No WPA-Enterprise in the Wi-Fi menu, no StatusNotifierItem tray, single
  screen only (screen 0), and no Latin-1 `WM_NAME` from very old applications

## Licence

wm2tj is an independent fork. It is not affiliated with or endorsed by the
original author. Licensed under the MIT (X11) licence, as wm2 is; see
`COPYING`, `wm/COPYING` and `bar/COPYING`.
