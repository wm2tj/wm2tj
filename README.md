# wm2tj

A minimal X11 window manager based on [wm2](https://github.com/cannam/wm2) by Chris Cannam.

**Status: beta (0.1.0).** Used daily on a few Xubuntu and Ubuntu laptops. It
builds warning-free on Debian, Ubuntu, Fedora, Arch and Alpine, but has only
been run on Xubuntu and Ubuntu so far (see "Known limitations").

Website (English / Japanese): <https://wm2tj.kaie-net.com/>

![wm2tj desktop: the settings window, a file manager and a terminal, each with a blue vertical tab on its left, under the wm2tj-bar top bar](screenshots/wm2tj.webp)

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
- Three-finger touchpad swipe between desktops (`wm2-swipe`, XInput 2.4; no root or `input` group needed)

Everything is done with the mouse, as in wm2 (there are no key bindings; the
same list is in `man wm2tj`):

| Action | Result |
|---|---|
| Left-drag the tab | Move the window (a click raises and focuses it) |
| Double-click the tab | Maximise / restore |
| Middle-click the tab (three-finger tap on a touchpad) | Minimise; bring it back from the bar's task list or the root menu |
| The square at the end of the tab | Close |
| Left-drag the bottom-right corner of the frame | Resize |
| Left-click the desktop | Cycle through the windows |
| Right-click the desktop | Root menu: favourites, application folders, open windows, session commands; type to search |

It comes with **wm2tj-bar**, a top bar drawn as a wm2tj tab laid on its side:
app menu, task list, virtual desktops, system tray, CPU / memory / Wi-Fi /
Bluetooth / volume / battery with their own menus, brightness and volume keys,
and a clock. Plain C and Xlib; English UI with a Japanese translation.

The two are separate programs and you can use either without the other: the
window manager alone (the session then starts `xfce4-panel` if it is
installed, or runs with no bar), or the bar alone under another EWMH window
manager.

![wm2tj-bar](bar/screenshot.png)

## Layout

| Directory | Contents |
|---|---|
| `wm/` | wm2tj, the window manager (C++). `wm/README` is the original wm2 README; `wm/README-ja.txt` describes the changes (Japanese) |
| `wm/extras/` | menu generator, settings GUIs, `session-logout`, `power-dialog`, wallpaper, `wm2-swipe`, systemd units. See `wm/extras/README-ja.txt` |
| `bar/` | wm2tj-bar, the top bar (C). `bar/README.md` (English) / `bar/README.ja.md` (Japanese) |

## Building

Debian / Ubuntu:

```
sudo apt install g++ gcc make pkg-config libx11-dev libxext-dev libxft-dev libxi-dev picom
make -C wm && sudo make -C wm install
make -C bar && sudo make -C bar install
make -C wm/extras install PREFIX=$HOME/.local   # optional extras
```

Package names for Fedora, Arch and Alpine are listed in `wm/README-ja.txt`
and `bar/README.md` (Alpine also needs `gettext-dev` for the bar). `make -C wm install-session` adds a "wm2tj" entry to the
display manager's session list. The top-level `make` / `sudo make install`
does all of the above (including the man pages) in one go.

### Debian / Ubuntu packages

Prebuilt `.deb` files for the latest tag are on the
[Releases](https://github.com/wm2tj/wm2tj/releases) page: `wm2tj` (window
manager, session, helpers) and `wm2tj-bar` (the bar, which also works with
other EWMH window managers). Install both, or just the one you want
(`wm2tj` only recommends the bar, it does not depend on it):

```
sudo apt install ./wm2tj_*.deb ./wm2tj-bar_*.deb
```

Install from a terminal as above. Double-clicking the `.deb` opens GDebi on
Xubuntu and other Ubuntu flavours, and its install step silently does
nothing after the password prompt (a GDebi/pkexec problem, not specific to
these packages). Upgrading works the same way: download the new `.deb` files
and run the same command.

If you built and installed from source before, that copy is in
`/usr/local/bin`, which comes first in `PATH`, so it keeps running after the
package is installed. Check with `which -a wm2tj wm2tj-bar` and remove the
`/usr/local` copies (`wm2tj`, `wm2tj-bar`, `wm2tj-bar-settings` and the
other files `make install` put there) before or after installing the
package.

To build them yourself:

```
sudo apt install build-essential debhelper pkgconf libx11-dev libxext-dev libxft-dev libxi-dev gettext
dpkg-buildpackage -us -uc -b
sudo apt install ../wm2tj_*.deb ../wm2tj-bar_*.deb
```

## First login

Pick **wm2tj** in the session menu of the display manager (GDM, LightDM,
SDDM). The session starts the compositor, the bar, the input method and the
other helpers that are installed, and creates `~/.wm2rc` on the first login
from `/usr/share/wm2tj/wm2rc.sample` (the settings) plus a root menu
generated from the installed applications. Right-click the desktop: the
**wm2tj** folder of the menu has the settings window, the favourites editor
and the wallpaper chooser; everything else is in `~/.wm2rc` (see `man wm2tj`)
and `~/.config/wm2tj-bar/config` (see `bar/README.md`). The menu is kept up
to date as applications are installed and removed.

## Known limitations (beta)

- Builds warning-free (`-Wall -Wextra`) on Debian stable, Ubuntu 24.04,
  Fedora, Arch and Alpine (musl, gcc and clang), but has only been *run* on
  Xubuntu and Ubuntu (GDM and LightDM). NixOS, Void and Gentoo are untested
- Packages exist for Debian / Ubuntu only; Arch, Fedora and Alpine build from
  source with `make` (native packages are planned for the next beta)
- X11 only (it is a window manager for Xorg; there is no Wayland version)
- Volume control needs `wpctl` or `pactl` (plain ALSA is not supported)
- No WPA-Enterprise in the Wi-Fi menu, no StatusNotifierItem tray, single
  screen only (screen 0), and no Latin-1 `WM_NAME` from very old applications
- Touchpad swipes need Xorg 21 or later (XInput 2.4 gestures)

## Licence

wm2tj is an independent fork. It is not affiliated with or endorsed by the
original author. Licensed under the MIT (X11) licence, as wm2 is; see
`LICENSE`, `COPYING`, `wm/COPYING` and `bar/COPYING`. The wm2 notice in
`wm/COPYING` adds that the names of the authors shall not be used in
advertising without permission; that applies to `wm/`.
