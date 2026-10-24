# wm2tj-bar — a top bar for wm2tj

English | [日本語](README.ja.md)

A standalone top bar for wm2tj, a window manager based on wm2. Written in C with Xlib (Xft, Shape). It runs as a separate process from the window manager and talks to it only through EWMH properties, so wm2tj needs no special support beyond the usual hints. It replaces xfce4-panel.

```
[■ mylaptop ◤] [Terminal] [Firefox] [Files]   [1][2][3]  ▣▮  ▤▮  ▂▄▆█  [あ]  ᛒ ◁) [bat]85%  12:34
 └  tab  ┘    └ running applications ┘                CPU  mem  Wi-Fi  IME  BT vol  battery  clock
```

![screenshot](screenshot.png)

- **Left end**: the tab that wm2tj draws beside each window, laid on its side with the same width (`tab_width`), colour, border, button and slanted end. The button (■) opens the menu. The label is the host name (`tab_label`), and the tab is just long enough for it
- **Next to it (on black)**: the applications running on the current desktop (`_NET_CLIENT_LIST`). The front one is plain grey outlined with a thin line of the tab colour (`task_gloss`, `task_style`; in the tab's own blue it would run into the tab at the left end), minimised ones are grey
  - Click to bring one to the front. Clicking the front one minimises it, clicking again restores it
  - With many windows the buttons get narrower (200px at most). Hover to see the full title
  - Windows with `_NET_WM_STATE_SKIP_TASKBAR` are left out
- **Right side**: virtual desktops (numbered boxes, click or scroll to switch) → CPU → memory → [system tray](#system-tray) → Wi-Fi → input method → Bluetooth → volume → battery → clock
  - CPU and memory: an icon and a vertical bar (usage from the bottom, red at 90% or more)
  - Input method: the label of the current one (`あ` for Mozc, `拼` for Pinyin, `jp` / `us` for a keyboard), lit while an input method is on. Works with fcitx5, IBus, or plain XKB layouts. See [Input method](#input-method)
  - Wi-Fi: four signal bars (lit by strength). A network icon when wired, grey bars and a red × when offline. Click for the [Wi-Fi menu](#wi-fi-menu)
  - Bluetooth: the rune, grey when off, blue while a device is connected. Click for the [Bluetooth menu](#bluetooth-menu). Hidden on machines without an adapter
  - Volume: a speaker with up to three waves, a red × when muted. Scroll over it to change the volume, click for the [sound menu](#sound). Hidden without a sound server
  - Battery: a battery icon (filled to the level, red at 15% or less, a bolt while charging) and the percentage. Hidden on machines without a battery. Click for the battery menu (see Power)
  - Hover over an icon for the numbers (e.g. "Memory 3.2 / 15.5 GB (41%)", "Wi-Fi 78% (wlp2s0)"). Hover over the clock for the date
- CPU, memory, Wi-Fi, Bluetooth and battery are read straight from `/proc`, `/sys` and the kernel, without running any commands (clock, CPU and memory every second, the rest every 5 seconds). The volume and the input method have no such place to read, so they are asked of `wpctl` / `pactl` every 5 seconds and of `fcitx5-remote` or `ibus` every second (5–20 ms each); XKB layout changes arrive as events
- Power handling (brightness keys, power profile, screen off): see [Power](#power)

## ■ Menu

- **Favourites** at the top. Right-click an application → "Add to favourites" / "Remove from favourites". Saved in `~/.config/wm2tj-bar/favorites` (one .desktop file name per line)
  - `sync_wm2tj yes` ("Sync the menu with wm2tj" in the settings window) reads and writes `~/.config/wm2tj/favorites`, the favourites of the wm2tj right-click menu, so a favourite added or removed on either side shows in both
    - until that file exists the bar shows its own favourites (`~/.config/wm2tj-bar/favorites`) and the first change creates the shared file (wm2tj's `wm2-menu-gen` also starts it from the bar's list when missing)
    - after each change the bar runs `wm2tj_menu_command` so the wm2tj menu follows; the default runs `wm2-menu-gen --hup` (from PATH, `~/.local/bin` or `/usr/local/bin`; wm2tj takes the new menu when it next opens, an older wm2tj gets SIGHUP), unless wm2tj's `wm2-menu-sync.path` (systemd --user) is watching the favourites file and does the same
  - `"Name" command` lines in that file show as favourites too and run the command. The bar keeps them, and the comments at the top, when it writes the file
- **Applications** ▶ by category (Accessories / Internet / Office / Graphics / Multimedia / Development / Education / Science / Games / System / Settings / Other)
  - .desktop files from `~/.local/share/applications`, then `$XDG_DATA_DIRS/applications` (default `/usr/local/share` `/usr/share`). For files with the same name the first one wins, so a local `Hidden=true` hides a system entry
  - Left out: `NoDisplay`, `Hidden`, a missing `TryExec`, and `OnlyShowIn` / `NotShowIn` that do not match `$XDG_CURRENT_DESKTOP`
  - Names: `Name[ll_CC]` → `Name[ll]` for the current language → the translation in a language pack (`X-GNOME-Gettext-Domain` / `X-Ubuntu-Gettext-Domain`) → `Name`
  - Field codes such as `%f %u` are removed from `Exec`. `Terminal=true` applications open in the `terminal` from the configuration
  - A category too long for the screen is laid out in several columns
- **Session** ▶ Lock / Log out / Suspend / Restart… / Shut down…. Restart and shut down ask first (Cancel is the default)
- **Bar settings…**: the settings window, below. Not shown when `wm2tj-bar-settings` is not installed
- Keyboard: ↑↓ to choose, → / Enter to open a submenu, ← to go back, Enter to run, Esc to close

## Wi-Fi menu

Click the Wi-Fi icon for a list of nearby networks (NetworkManager, driven with `nmcli`).

- One entry per network name, strongest first. A ✓ marks the one in use, with signal bars on the right and a padlock for secured networks
- Choose one to connect. Saved and open networks connect straight away. **For a new secured network the bar asks for the password itself** (no nm-applet needed)
  - The password goes to `nmcli --ask` on standard input, never on the command line, so it does not show in `ps`. It is wiped after use
  - In the password box: Backspace or Ctrl+U to delete, Enter to connect, Esc to cancel. "Show password" shows the characters
  - If the connection fails, the bar asks for the password again ("Could not connect" for open networks)
- Below the list: "Disconnect", "Turn Wi-Fi off" / "Turn Wi-Fi on", and "Network settings…" (`wifi_settings_command`, default `nm-connection-editor`; empty hides the item)
- Opening the menu starts a rescan in the background and the list is refreshed 4 seconds later
- Without `nmcli` the menu just says "nmcli (NetworkManager) not found"

## Bluetooth menu

Click the Bluetooth icon (BlueZ, driven with `bluetoothctl`).

- The paired devices, with a ✓ on the connected ones. Choose one to connect or disconnect it
- "Turn Bluetooth on" / "Turn Bluetooth off". Turning on also lifts an rfkill soft block (through `/dev/rfkill`, which the session user may write on systemd systems)
- "Bluetooth settings…" (`bluetooth_settings_command`, default `blueman-manager`; empty hides the item). Pairing a new device is left to it
- The state itself (blocked, adapter up, connections) comes from rfkill, an HCI socket and `/sys/class/bluetooth`, without commands

## Sound

The volume keys and the speaker icon drive the default output through `wpctl` (PipeWire) or, without it, `pactl` (PulseAudio).

- **Keys**: `XF86AudioRaiseVolume` / `LowerVolume` change the volume by `volume_step` % (default 5, up to 100%; raising also unmutes). `XF86AudioMute` mutes the output, `XF86AudioMicMute` the microphone. A tip under the icon shows the result for a moment
- **Scroll** over the icon to change the volume
- **Click** for the sound menu: a **volume** slider (click or drag it, scroll over it, or select it and press ← →, `volume_step` % a step; greyed while muted; raising it unmutes), the outputs (✓ on the default one; choose another to switch to it), "Mute" / "Unmute", and "Sound settings…" (`sound_settings_command`, default `pavucontrol`; empty hides the item)
- `volume_step 0` leaves the keys to something else

## System tray

The bar is a freedesktop.org system tray (XEmbed), so applications that put an icon in the tray show it between the memory gauge and Wi-Fi. The icons are the applications' own; clicking them goes to the applications.

- Most GTK 3, Qt and Electron applications try a StatusNotifierItem host first and fall back to XEmbed when there is none, so they appear here
- Applications that only speak StatusNotifierItem (D-Bus) do not appear. A bridge such as [snixembed](https://git.sr.ht/~steef/snixembed) shows them in an XEmbed tray. GTK 4 has no tray icons at all
- Icons are 16–24 px, depending on the bar height. Icons that ask to be hidden (`_XEMBED_INFO`) are left out
- When the bar exits or reloads, it gives the icons back, and the applications dock them again in the next tray
- `tray no` turns it off (for using another tray such as stalonetray). If another tray is already running, the bar leaves it alone
- `tray_hide nm-applet` (WM_CLASS names as `xprop WM_CLASS` shows them, either half, separated by spaces) docks those icons but does not show them; the applications keep running
- The input method's own icon (the IBus panel, `ibus-ui-gtk`; fcitx5, `fcitx`) is hidden automatically while the bar shows that input method. With `input_method none` it shows in the tray
- Icons of other applets that the bar already covers (nm-applet, blueman-applet) appear next to the bar's own. Leave the applet out of the session, or hide its icon with `tray_hide`

## Notifications

The bar does not show desktop notifications (`org.freedesktop.Notifications`), and neither does wm2tj. Run a notification daemon for them.

- [dunst](https://dunst-project.org/) (`apt install dunst`) is light (a few MB). xfce4-notifyd, mate-notification-daemon, notification-daemon and lxqt-notificationd work too
- wm2tj's `wm2-session` starts the first of these that is installed, in that order, unless a daemon is already running (`WM2TJ_NOTIFYD=<command>` picks one). With your own session script, add `dunst &` or similar
- Where a notification daemon is installed (Xubuntu, MATE, KDE and so on), D-Bus often starts it on demand. Stock Ubuntu (GNOME) shows notifications inside gnome-shell, so in a wm2tj session nothing appears until one is installed
- Without a daemon, `notify-send`, update notices and mail or chat applications show nothing. Chrome and Firefox fall back to drawing their own notifications

## Input method

The bar shows the current input method and switches it, whichever framework runs. `input_method auto` (the default) looks for fcitx5, then IBus, then XKB; `fcitx5`, `ibus` or `xkb` picks one, `none` hides the indicator. Whatever the language, the label comes from the engine itself, so `auto` suits a distribution as is. While the bar shows an input method, that input method's own tray icon is hidden, leaving one indicator (`none` leaves it to the tray icon instead).

An input method is drawn lit, a plain keyboard layout (and a direct-input mode such as Mozc's `A`) in an outline.

- **fcitx5**: the label is the one fcitx5 uses, `Label=` from `fcitx5/inputmethod/<name>.conf` (`あ` for Mozc), or the layout for a keyboard (`jp`, `us`). Click to switch on or off (`fcitx5-remote -t`, the same as the trigger key)
- **IBus**: the label is the engine's `<symbol>` from its component file in `ibus/component/` (`あ` Mozc, `拼` Pinyin, `한` Hangul; components that list their engines with `<engines exec=…>` are asked once), or the layout for an `xkb:` engine. Click for a menu of the engines chosen in the IBus preferences (`preload-engines`), the current one ticked; choosing one runs `ibus engine <name>`. The last item opens `ime_settings_command` (default `ibus-setup`)
- **XKB**: without an input method framework but with several layouts (`setxkbmap -layout us,ru`), the label is the current layout and the menu lists the layouts by name (English (US), Russian). Changes made by a hotkey show at once. With a single layout nothing is shown

## Power

So that no separate power manager (such as xfce4-power-manager) is needed, the bar does the following. Each part can be turned off in the configuration (see [`config.sample`](config.sample)).

- **Brightness keys** (`XF86MonBrightnessUp` / `Down`): `brightness_step` % per press (default 10). The bar calls logind's `SetBrightness` with `busctl`, so no root is needed. It never goes fully dark (1% minimum)
- **Power profile**: switches to `profile_on_ac` when the power is plugged in and to `profile_on_battery` when it is unplugged, with `powerprofilesctl` (power-profiles-daemon). Nothing is switched unless these are set
- **Screen off**: likewise sets the X DPMS off time to `screen_off_on_ac` / `screen_off_on_battery` minutes. DPMS is left alone unless these are set
- The power source is checked every 5 seconds in `/sys/class/power_supply` (`online` of the `Mains` supply)
- **Battery menu** (click the battery): the level and whether it is charging, a **brightness** slider, and the **power mode** (power saver / balanced / performance, a tick on the one in use)
  - Brightness: click or drag the slider, scroll over it, or select it and press ← → (`brightness_step` % a step, 5% when that is 0). Goes through logind like the keys. Not shown without `busctl` or a backlight
  - Power mode: the profiles `powerprofilesctl list` shows; choosing one runs `powerprofilesctl set`. Not shown without power-profiles-daemon. The choice lasts until the power is next plugged in or out (when `profile_on_*` switches it, if set)

The power button and the lid are left to systemd-logind. By default the power button shuts down. To suspend instead:

```
# /etc/systemd/logind.conf.d/wm2tj-power.conf
[Login]
HandlePowerKey=suspend
HandleLidSwitch=suspend
HandleLidSwitchExternalPower=suspend
```

Apply it with `sudo systemctl kill -s HUP systemd-logind`. Stop xfce4-power-manager if it is running, because it takes over the power button and the brightness keys. Locking the screen before suspend is the screen saver's job (xfce4-screensaver or similar). What happens when the battery is nearly empty is up to UPower (`CriticalPowerAction` in `/etc/UPower/UPower.conf`).

## Building and installing

```
sudo apt install build-essential pkg-config libx11-dev libxext-dev libxft-dev gettext   # Debian/Ubuntu
# Fedora: gcc make pkgconf libX11-devel libXext-devel libXft-devel gettext
# Arch:   base-devel libx11 libxext libxft gettext      Alpine: build-base pkgconf libx11-dev libxext-dev libxft-dev gettext gettext-dev
# Void:   base-devel pkg-config libX11-devel libXext-devel libXft-devel gettext
# the settings window (optional): python3 + PyGObject + GTK 3 = python3-gi / python3-gobject / python-gobject / py3-gobject3
# make works without gettext's msgfmt too (the bundled .mo is used as it is)
make
sudo make install          # /usr/local/bin/wm2tj-bar, wm2tj-bar-settings and its .desktop, the translations (/usr/local/share/locale)
```

Start it from the session script (`wm2-session` or similar) before wm2tj:

```
wm2tj-bar &
exec wm2tj
```

It overlaps xfce4-panel at the top, so remove the xfce4-panel line when switching.

### Dependencies

To build: a C compiler, pkg-config, the Xlib, Xext and Xft development files, and gettext (`msgfmt`, for the translations). No libraries are bundled.

At run time only X is required. The following are **used when present**. If one is missing, only that feature stops, and the reason is printed once on standard error:

| Command | Package (Debian / Ubuntu) | Used for |
|---|---|---|
| `nmcli` | network-manager | the Wi-Fi menu |
| `nm-connection-editor` | network-manager-gnome | "Network settings…" (change with `wifi_settings_command`) |
| `bluetoothctl`, `timeout` | bluez, coreutils | the Bluetooth menu |
| `blueman-manager` | blueman | "Bluetooth settings…" (change with `bluetooth_settings_command`) |
| `wpctl` or `pactl` | wireplumber, or pulseaudio-utils | the volume keys and the sound menu |
| `pavucontrol` | pavucontrol | "Sound settings…" (change with `sound_settings_command`) |
| `fcitx5-remote` | fcitx5 | the input method indicator with fcitx5 |
| `ibus`, `gsettings` | ibus, libglib2.0-bin | the input method indicator and menu with IBus |
| `ibus-setup` | ibus | "Input method settings…" (change with `ime_settings_command`) |
| `busctl` | systemd | the brightness keys (logind's `SetBrightness`) |
| `powerprofilesctl` | power-profiles-daemon | switching power profiles (only when `profile_on_*` is set) |
| `systemctl` / `loginctl` | systemd / elogind | suspend, restart and shut down in the Session menu (defaults; elogind's loginctl when there is no systemctl) |
| `dbus-send` | dbus | brightness changes where busctl is missing (elogind systems) |
| `python3`, PyGObject, GTK 3 | python3-gi, gir1.2-gtk-3.0 | the settings window (`wm2tj-bar-settings`); the bar itself does not need them |

### Languages

The messages are written in English and translated with gettext. Japanese is included (`po/ja.po`). With a Japanese `LANG` / `LANGUAGE` the bar is in Japanese, otherwise in English.

- After adding or changing messages: `make pot` → `msgmerge -U po/ja.po po/wm2tj-bar.pot` → translate `po/ja.po`
- To add a language, create `po/<lang>.po` and add it to `LANGS` in the Makefile

## Configuration

The bar reads `tab_width` `tab_length` `tab_gloss` `tab_foreground` `borders` `button_background` `font` from `~/.wm2rc` so that it looks like the wm2tj tabs. To change something for the bar only, use `~/.config/wm2tj-bar/config` (same format as `~/.wm2rc`). The keys are listed in [`config.sample`](config.sample).

- Reload: `pkill -HUP -x wm2tj-bar` (reads the configuration again and restarts)
- A second instance exits with "already running" (one per screen)

### Settings window

`wm2tj-bar-settings` ("Bar settings…" in the ■ menu, "Top bar settings" among the Settings applications). It stands on its own: it does not need the wm2tj settings window.

- Tabs: Appearance (the tab at the left end, the bar, the clock) / Menu (sync the menu with wm2tj, the session commands, the terminal) / Power / Network and sound / Tray
- Ticking "Default" next to a setting ("Same as wm2tj" for the looks, "Leave alone" for power) takes the key out of the config (the line becomes a comment). "Same as wm2tj" is the value in `~/.wm2rc`
- "Apply" rewrites only the values that changed (comments and the other lines stay, missing keys go at the end) and sends the bar SIGHUP. Before the first change the file is copied to `config.bak-wm2tj-bar-settings`
- "Revert" drops the changes not applied yet and reads the file again

### Default session commands

| Item | Default | Notes |
|---|---|---|
| Lock | `xsecurelock` | needs installing (`apt install xsecurelock`); i3lock and others work too |
| Log out | `pkill -TERM -x wm2tj` | assumes the session ends when wm2tj exits. Use your own logout command if it does not |
| Suspend | `systemctl suspend` | `loginctl suspend` (elogind) when there is no systemctl |
| Restart | `systemctl reboot` | likewise `loginctl reboot` |
| Shut down | `systemctl poweroff` | likewise `loginctl poweroff` |
| Terminal (`terminal`) | `$TERMINAL`, else xfce4-terminal → gnome-terminal → konsole → … → xterm | for `Terminal=true` entries, opened with `-e` |

## What the window manager needs to provide (wm2tj already does)

- No frame for `_NET_WM_WINDOW_TYPE_DOCK` windows
- Keep clear of the bar by `_NET_WM_STRUT_PARTIAL` / `_NET_WM_STRUT` when maximising and so on
- Set `_NET_NUMBER_OF_DESKTOPS` / `_NET_CURRENT_DESKTOP`, and switch desktops on a `_NET_CURRENT_DESKTOP` client message
- Set `_NET_ACTIVE_WINDOW`, and bring a window to the front on a client message of the same name (switching desktop or restoring it as needed)
- Set `_NET_CLIENT_LIST` and each window's `_NET_WM_DESKTOP`. Minimise on `WM_CHANGE_STATE` (IconicState) and reflect it in `WM_STATE`

## To do

- Icons in the menus (none in v0.1)
- A menu for fcitx5 (it switches on a click for now)
- Date and weekday (possible with `clock_format`; the default is HH:MM)

## Licence

MIT ([`COPYING`](COPYING)). The code is newly written and contains no wm2 code (the formula for the tab's gloss colour is the same one written for wm2tj).
