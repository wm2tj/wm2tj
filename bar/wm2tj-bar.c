/*
 * wm2tj-bar — a top bar for the wm2tj window manager
 *
 * Looks like a wm2 tab turned 90 degrees clockwise: the same glossy
 * colour, font and height, laid across the top of the screen.
 *
 *   [■][1][2][3]        active window title        CPU MEM Wi-Fi BAT 12:34
 *
 * A separate process from the window manager. It talks to it only through
 * EWMH properties (_NET_WM_WINDOW_TYPE_DOCK, _NET_WM_STRUT_PARTIAL,
 * _NET_CURRENT_DESKTOP, _NET_NUMBER_OF_DESKTOPS, _NET_ACTIVE_WINDOW).
 * System information is read from /proc and /sys; no external commands.
 * Power: the brightness keys go through logind (busctl), and the power
 * profile and the screen-off time follow the power source (powerprofilesctl,
 * DPMS). The power button and the lid are left to logind. The Wi-Fi menu
 * drives NetworkManager with nmcli, the Bluetooth menu BlueZ with
 * bluetoothctl. The volume keys and the speaker go through wpctl (PipeWire)
 * or pactl (PulseAudio). The input method indicator follows fcitx5
 * (fcitx5-remote), IBus (ibus) or, without either, the XKB layout group.
 * Each of these commands is optional. Applications put their icons in the
 * bar through the freedesktop.org system tray (XEmbed).
 * Messages are English, translated with gettext (po/).
 *
 * Copyright (c) 2026 tetsu. MIT licence, see COPYING.
 */

#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/dpms.h>
#include <X11/XF86keysym.h>
#include <X11/XKBlib.h>

#include <ctype.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libintl.h>
#include <locale.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PROGNAME "wm2tj-bar"
#define SETTINGS_PROG PROGNAME "-settings"
#define VERSION  "0.1.0"
#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif

#define _(s)  gettext(s)
#define N_(s) (s)

static Display *dpy;
static int scr;
static Window root;
static Visual *vis;
static Colormap cmap;
static int depth;
static GC gc;
static int scrW, scrH;

static volatile sig_atomic_t gotHup, gotTerm;


/* ------------------------------------------------------------------ */
/* configuration                                                      */

static struct {
    int  height;		/* 0: tab_width + 2 from ~/.wm2rc */
    int  tabWidth;		/* from ~/.wm2rc */
    int  tabLength;		/* 0: just long enough for the label */
    char tabLabel[128];		/* "": the host name */
    char borders[64];		/* tab border, from ~/.wm2rc */
    char buttonBackground[64];	/* from ~/.wm2rc */
    char barBackground[64];	/* right of the tab */
    char textColour[64];	/* status text on the bar */
    char taskGloss[64];	/* the active window's button: a colour, or "theme" (the tab's) */
    char taskStyle[16];	/* flat (with a theme-colour line), gloss */
    char font[256];
    char menuFont[256];
    char gloss[64];		/* "none": flat background */
    char background[64];	/* used when gloss is none */
    char foreground[64];
    char clockFormat[64];
    char lockCmd[512];
    char logoutCmd[512];
    char suspendCmd[256];
    char rebootCmd[256];
    char poweroffCmd[256];
    char terminal[256];
    int  brightnessStep;	/* % per key press, 0: leave the keys alone */
    char backlight[64];		/* "": the first in /sys/class/backlight */
    char profileAc[32];		/* power-profiles-daemon, "": leave it alone */
    char profileBattery[32];
    int  screenOffAc;		/* minutes, 0: never, -1: leave DPMS alone */
    int  screenOffBattery;
    char wifiSettingsCmd[256];	/* "": no settings item in the Wi-Fi menu */
    char btSettingsCmd[256];	/* "": no settings item in the Bluetooth menu */
    int  volumeStep;		/* % per key press, 0: leave the keys alone */
    char soundSettingsCmd[256];	/* "": no settings item in the sound menu */
    char inputMethod[16];	/* auto, fcitx5, ibus, xkb, none */
    char imeSettingsCmd[256];	/* "": no settings item in the input method menu */
    int  tray;			/* be the system tray */
    char trayHide[256];		/* WM_CLASS names whose tray icons stay hidden */
    int  syncWm2tj;		/* share the favourites with the wm2tj menu */
    char syncCmd[512];		/* ... and rebuild that menu after a change */
} cfg;

/* with sync_wm2tj: regenerate ~/.wm2rc and make wm2tj reread it. wm2-menu-gen
   is wm2tj's (extras/), installed to ~/.local/bin or /usr/local/bin; wm2 is
   its name before 2026-10-21 */
#define DEFAULT_SYNC_CMD \
    "g=$(command -v wm2-menu-gen) || for d in \"$HOME/.local/bin\" /usr/local/bin; do " \
    "[ -x \"$d/wm2-menu-gen\" ] && g=\"$d/wm2-menu-gen\" && break; done; " \
    "[ -n \"$g\" ] && \"$g\" >/dev/null 2>&1 && pkill -HUP -x 'wm2tj|wm2'"

static char configDir[512];
static int inPath(const char *prog);

static void setStr(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src);
}
#define SET(f, v) setStr(cfg.f, sizeof(cfg.f), v)

/* "key value  # comment" or 'key "value with #"', the ~/.wm2rc syntax */
static int splitLine(char *line, char **key, char **val)
{
    char *p = line, *q;

    while (isspace((unsigned char)*p)) ++p;
    if (!*p || *p == '#') return 0;
    *key = p;
    while (*p && !isspace((unsigned char)*p)) ++p;
    if (*p) *p++ = '\0';
    while (isspace((unsigned char)*p)) ++p;

    if (*p == '"') {
	++p;
	if ((q = strchr(p, '"'))) *q = '\0';
    } else {
	if ((q = strchr(p, '#'))) *q = '\0';
	q = p + strlen(p);
	while (q > p && isspace((unsigned char)q[-1])) *--q = '\0';
    }
    *val = p;
    return 1;
}

static void readWm2rc(void)
{
    char path[512], line[1024], *k, *v;
    const char *home = getenv("HOME");
    FILE *f;

    snprintf(path, sizeof path, "%s/.wm2rc", home ? home : ".");
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
	if (!splitLine(line, &k, &v)) continue;
	if      (!strcmp(k, "tab_width"))      cfg.tabWidth = atoi(v);
	else if (!strcmp(k, "font"))           SET(font, v);
	else if (!strcmp(k, "tab_gloss"))      SET(gloss, v);
	else if (!strcmp(k, "tab_background")) SET(background, v);
	else if (!strcmp(k, "tab_foreground")) SET(foreground, v);
	else if (!strcmp(k, "borders"))        SET(borders, v);
	else if (!strcmp(k, "button_background")) SET(buttonBackground, v);
    }
    fclose(f);
}

static void readConfig(void)
{
    char path[600], line[1024], *k, *v;
    FILE *f;

    snprintf(path, sizeof path, "%s/config", configDir);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
	if (!splitLine(line, &k, &v)) continue;
	if      (!strcmp(k, "height"))           cfg.height = atoi(v);
	else if (!strcmp(k, "font"))             SET(font, v);
	else if (!strcmp(k, "menu_font"))        SET(menuFont, v);
	else if (!strcmp(k, "gloss"))            SET(gloss, v);
	else if (!strcmp(k, "background"))       SET(background, v);
	else if (!strcmp(k, "foreground"))       SET(foreground, v);
	else if (!strcmp(k, "tab_length"))       cfg.tabLength = atoi(v);
	else if (!strcmp(k, "borders"))          SET(borders, v);
	else if (!strcmp(k, "button_background")) SET(buttonBackground, v);
	else if (!strcmp(k, "bar_background"))   SET(barBackground, v);
	else if (!strcmp(k, "text"))             SET(textColour, v);
	else if (!strcmp(k, "task_gloss"))       SET(taskGloss, v);
	else if (!strcmp(k, "task_style"))       SET(taskStyle, v);
	else if (!strcmp(k, "clock_format"))     SET(clockFormat, v);
	else if (!strcmp(k, "tab_label"))        SET(tabLabel, strcmp(v, "none") ? v : " ");
	else if (!strcmp(k, "lock_command"))     SET(lockCmd, v);
	else if (!strcmp(k, "logout_command"))   SET(logoutCmd, v);
	else if (!strcmp(k, "suspend_command"))  SET(suspendCmd, v);
	else if (!strcmp(k, "reboot_command"))   SET(rebootCmd, v);
	else if (!strcmp(k, "poweroff_command")) SET(poweroffCmd, v);
	else if (!strcmp(k, "terminal"))         SET(terminal, v);
	else if (!strcmp(k, "brightness_step"))  cfg.brightnessStep = atoi(v);
	else if (!strcmp(k, "backlight"))        SET(backlight, v);
	else if (!strcmp(k, "profile_on_ac"))    SET(profileAc, v);
	else if (!strcmp(k, "profile_on_battery")) SET(profileBattery, v);
	else if (!strcmp(k, "screen_off_on_ac"))   cfg.screenOffAc = atoi(v);
	else if (!strcmp(k, "screen_off_on_battery")) cfg.screenOffBattery = atoi(v);
	else if (!strcmp(k, "wifi_settings_command")) SET(wifiSettingsCmd, v);
	else if (!strcmp(k, "bluetooth_settings_command")) SET(btSettingsCmd, v);
	else if (!strcmp(k, "volume_step"))      cfg.volumeStep = atoi(v);
	else if (!strcmp(k, "sound_settings_command")) SET(soundSettingsCmd, v);
	else if (!strcmp(k, "input_method"))     SET(inputMethod, v);
	else if (!strcmp(k, "ime_settings_command")) SET(imeSettingsCmd, v);
	else if (!strcmp(k, "tray"))
	    cfg.tray = !strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "1");
	else if (!strcmp(k, "tray_hide"))        SET(trayHide, v);
	else if (!strcmp(k, "sync_wm2tj"))
	    cfg.syncWm2tj = !strcmp(v, "yes") || !strcmp(v, "true") || !strcmp(v, "1");
	else if (!strcmp(k, "wm2tj_menu_command")) SET(syncCmd, v);
	else fprintf(stderr, PROGNAME ": %s: unknown key \"%s\"\n", path, k);
    }
    fclose(f);
}

static void loadConfig(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");

    if (xdg && *xdg) snprintf(configDir, sizeof configDir, "%s/" PROGNAME, xdg);
    else snprintf(configDir, sizeof configDir, "%s/.config/" PROGNAME,
		  home ? home : ".");

    /* the wm2tj defaults (the same as the wm2 tab) */
    cfg.height = 0;
    cfg.tabWidth = 32;
    cfg.tabLength = 0;
    SET(borders, "white");
    SET(buttonBackground, "#1a4f9c");
    SET(barBackground, "black");
    SET(textColour, "white");
    SET(taskGloss, "#53575f");	/* grey: the blue tab at the left end stands out */
    SET(taskStyle, "flat");
    SET(font, "Noto Sans CJK JP:style=Bold:pixelsize=13");
    SET(menuFont, "Noto Sans CJK JP:pixelsize=14");
    SET(gloss, "#2a7de1");
    SET(background, "gray80");
    SET(foreground, "white");
    SET(clockFormat, "%H:%M");
    SET(lockCmd, "xsecurelock");
    SET(logoutCmd, "pkill -TERM -x wm2tj");
    /* systemd, else elogind (Void, Alpine, Gentoo/OpenRC...): its loginctl has these verbs */
    if (inPath("systemctl")) {
	SET(suspendCmd, "systemctl suspend");
	SET(rebootCmd, "systemctl reboot");
	SET(poweroffCmd, "systemctl poweroff");
    } else {
	SET(suspendCmd, "loginctl suspend");
	SET(rebootCmd, "loginctl reboot");
	SET(poweroffCmd, "loginctl poweroff");
    }
    SET(terminal, "");		/* "": $TERMINAL or the first terminal found (checkCommands) */
    cfg.brightnessStep = 10;
    cfg.backlight[0] = '\0';
    cfg.profileAc[0] = cfg.profileBattery[0] = '\0';
    cfg.screenOffAc = cfg.screenOffBattery = -1;
    SET(wifiSettingsCmd, "nm-connection-editor");
    SET(btSettingsCmd, "blueman-manager");
    cfg.volumeStep = 5;
    SET(soundSettingsCmd, "pavucontrol");
    SET(inputMethod, "auto");
    SET(imeSettingsCmd, "ibus-setup");
    cfg.tray = 1;
    cfg.trayHide[0] = '\0';
    cfg.syncWm2tj = 0;
    SET(syncCmd, DEFAULT_SYNC_CMD);

    readWm2rc();
    readConfig();
}


/* ------------------------------------------------------------------ */
/* colours and drawing helpers                                        */

static XftFont *barFont, *menuFont;
static int barH;
static int glossOn;
static double glossBase[3], taskBase[3];

static XftColor cFg, cShadow, cHover, cDeskCur, cDeskLine, cWarn;
static XftColor cMenuBg, cMenuFg, cMenuDim, cMenuLine, cBorder, cMenuBorder, cFlatBg,
    cEntryBg;
static XftColor cButton, cButtonOn, cBarBg, cText, cDim, cWell, cWellLine, cMeter, cHot, cOff, cBatFill,
    cTask, cTaskHover;

static void allocColor(XftColor *c, const char *name, unsigned short alpha)
{
    XColor x;
    XRenderColor rc = { 0, 0, 0, alpha };

    if (XParseColor(dpy, cmap, name, &x)) {
	/* premultiplied, as XRender expects */
	rc.red   = (unsigned short)((unsigned long)x.red   * alpha / 0xffff);
	rc.green = (unsigned short)((unsigned long)x.green * alpha / 0xffff);
	rc.blue  = (unsigned short)((unsigned long)x.blue  * alpha / 0xffff);
    } else {
	fprintf(stderr, PROGNAME ": unknown colour \"%s\"\n", name);
    }
    XftColorAllocValue(dpy, vis, cmap, &rc, c);
}

static void mix(double *c, double r, double g, double b, double t)
{
    if (t < 0) t = 0; else if (t > 1) t = 1;
    c[0] += (r - c[0]) * t;
    c[1] += (g - c[1]) * t;
    c[2] += (b - c[2]) * t;
}

/*
 * The wm2tj tab colour, from Border::glossColour(). s runs across the
 * tab (0 = its left edge, deep; 1 = its right edge, light), t along it
 * (0 = its top). Turned clockwise into a bar, the tab's left edge is
 * the bar's top and the tab's top is the bar's right end.
 */
static void glossColour(double *c, const double *base, double s, double t)
{
    double g;

    c[0] = base[0]; c[1] = base[1]; c[2] = base[2];
    mix(c, 0, 0.02, 0.15, 0.50 * pow(1 - s, 1.4));
    mix(c, 1, 1, 1, 0.45 * s * s);
    g = (s - 0.72) / 0.10;
    mix(c, 1, 1, 1, 0.45 * exp(-g * g));
    if (t < 0.3) mix(c, 1, 1, 1, 0.25 * (1 - t / 0.3));
    mix(c, 0, 0, 0.1, 0.20 * t);
}

static int maskShift(unsigned long m, int *bits)
{
    int sh = 0, n = 0;
    while (m && !(m & 1)) { m >>= 1; ++sh; }
    while (m & 1) { m >>= 1; ++n; }
    *bits = n;
    return sh;
}

static unsigned long packPixel(const double *c)
{
    unsigned long masks[3] = { vis->red_mask, vis->green_mask, vis->blue_mask };
    unsigned long p = 0;
    int i, bits, sh;

    for (i = 0; i < 3; ++i) {
	unsigned long max;
	sh = maskShift(masks[i], &bits);
	max = (1UL << bits) - 1;
	p |= ((unsigned long)(c[i] * max + 0.5) & max) << sh;
    }
    return p;
}

/* a w x h pixmap of the tab colour (on base), turned clockwise */
static Pixmap renderGloss(int w, int h, const double *base)
{
    Pixmap p = XCreatePixmap(dpy, root, w, h, depth);
    XImage *img;
    int x, y;

    if (!glossOn) {
	XSetForeground(dpy, gc, cFlatBg.pixel);
	XFillRectangle(dpy, p, gc, 0, 0, w, h);
	return p;
    }
    img = XCreateImage(dpy, vis, depth, ZPixmap, 0, 0, w, h, 32, 0);
    img->data = malloc((size_t)img->bytes_per_line * h);
    for (y = 0; y < h; ++y) {
	double s = h > 1 ? (double)y / (h - 1) : 0;
	for (x = 0; x < w; ++x) {
	    double c[3];
	    glossColour(c, base, s, w > 1 ? (double)(w - 1 - x) / (w - 1) : 0);
	    XPutPixel(img, x, y, packPixel(c));
	}
    }
    XPutImage(dpy, p, gc, img, 0, 0, 0, 0, w, h);
    XDestroyImage(img);
    return p;
}

static int textWidth(XftFont *f, const char *s)
{
    XGlyphInfo gi;
    XftTextExtentsUtf8(dpy, f, (const FcChar8 *)s, strlen(s), &gi);
    return gi.xOff;
}

/* label text with the wm2 tab's drop shadow (down-left once turned) */
static void drawLabel(XftDraw *d, XftFont *f, XftColor *fg, int x, int y,
		      const char *s, int shadow)
{
    int n = strlen(s);
    if (shadow)
	XftDrawStringUtf8(d, &cShadow, f, x - 1, y + 1, (const FcChar8 *)s, n);
    XftDrawStringUtf8(d, fg, f, x, y, (const FcChar8 *)s, n);
}

static void drawBox(XftDraw *d, XftColor *c, int x, int y, int w, int h)
{
    XftDrawRect(d, c, x, y, w, 1);
    XftDrawRect(d, c, x, y + h - 1, w, 1);
    XftDrawRect(d, c, x, y + 1, 1, h - 2);
    XftDrawRect(d, c, x + w - 1, y + 1, 1, h - 2);
}

/* cut a UTF-8 string so that it fits in w pixels, adding an ellipsis */
static void fitText(XftFont *f, char *s, size_t size, int w)
{
    size_t n = strlen(s);

    if (textWidth(f, s) <= w) return;
    while (n > 0) {
	do --n; while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80);
	if (n + 4 > size) continue;
	memcpy(s + n, "\xe2\x80\xa6", 4);	/* … */
	if (textWidth(f, s) <= w) return;
    }
    s[0] = '\0';
}


/* ------------------------------------------------------------------ */
/* X properties                                                       */

static Atom aType, aDock, aStrut, aStrutPartial, aWmDesktop, aWmState,
    aSticky, aAbove, aSkipTaskbar, aSkipPager, aNumDesktops, aCurDesktop,
    aActive, aNetWmName, aUtf8, aSelection, aClientList, aIcccmState,
    aChangeState;

static void internAtoms(void)
{
    char sel[64];

    aType         = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    aDock         = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
    aStrut        = XInternAtom(dpy, "_NET_WM_STRUT", False);
    aStrutPartial = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL", False);
    aWmDesktop    = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    aWmState      = XInternAtom(dpy, "_NET_WM_STATE", False);
    aSticky       = XInternAtom(dpy, "_NET_WM_STATE_STICKY", False);
    aAbove        = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    aSkipTaskbar  = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    aSkipPager    = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    aNumDesktops  = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS", False);
    aCurDesktop   = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    aActive       = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    aNetWmName    = XInternAtom(dpy, "_NET_WM_NAME", False);
    aUtf8         = XInternAtom(dpy, "UTF8_STRING", False);
    aClientList   = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
    aIcccmState   = XInternAtom(dpy, "WM_STATE", False);
    aChangeState  = XInternAtom(dpy, "WM_CHANGE_STATE", False);
    snprintf(sel, sizeof sel, "_WM2TJ_BAR_S%d", scr);
    aSelection    = XInternAtom(dpy, sel, False);
}

static long getCardinal(Window w, Atom prop, long dflt)
{
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char *data = 0;
    long v = dflt;

    if (XGetWindowProperty(dpy, w, prop, 0, 1, False, AnyPropertyType, &type,
			   &format, &n, &after, &data) == Success && data) {
	if (n >= 1 && format == 32) v = *(long *)data;
	XFree(data);
    }
    return v;
}

static void getWindowTitle(Window w, char *buf, size_t size)
{
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char *data = 0;
    XTextProperty tp;

    buf[0] = '\0';
    if (XGetWindowProperty(dpy, w, aNetWmName, 0, 1024, False, aUtf8, &type,
			   &format, &n, &after, &data) == Success && data) {
	if (n > 0) snprintf(buf, size, "%s", (char *)data);
	XFree(data);
	if (buf[0]) return;
    }
    if (XGetWMName(dpy, w, &tp) && tp.value) {
	char **list = 0;
	int count = 0;
	if (Xutf8TextPropertyToTextList(dpy, &tp, &list, &count) >= Success &&
	    count > 0 && list) {
	    snprintf(buf, size, "%s", list[0]);
	    XFreeStringList(list);
	}
	XFree(tp.value);
    }
}

static int ignoreXError(Display *d, XErrorEvent *e)
{
    (void)d; (void)e;		/* windows we watch can vanish at any time */
    return 0;
}


/* ------------------------------------------------------------------ */
/* system information                                                 */

static int cpuPct = -1;
static unsigned long long cpuPrevTotal, cpuPrevIdle;
static long memTotalKb, memAvailKb;
enum { NET_OFF, NET_WIFI, NET_LAN };
static int netKind, netQ = -1;
static char netIf[32];
static int batPresent, batCap, batCharging, batFull;
static int acOnline = -1;	/* -1: not read yet */

static int readFile(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ssize_t n;

    buf[0] = '\0';
    if (fd < 0) return -1;
    n = read(fd, buf, size - 1);
    close(fd);
    if (n < 0) return -1;
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = '\0';
    return (int)n;
}

static void readCpu(void)
{
    char buf[512];
    unsigned long long v[8] = { 0 }, total = 0, idle;
    int i;

    if (readFile("/proc/stat", buf, sizeof buf) <= 0) return;
    if (sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
	       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) < 4)
	return;
    for (i = 0; i < 8; ++i) total += v[i];
    idle = v[3] + v[4];		/* idle + iowait */
    if (cpuPrevTotal && total > cpuPrevTotal) {
	unsigned long long dt = total - cpuPrevTotal, di = idle - cpuPrevIdle;
	cpuPct = (int)((dt - di) * 100 / dt);
    }
    cpuPrevTotal = total;
    cpuPrevIdle = idle;
}

static void readMem(void)
{
    char buf[4096], *p;

    if (readFile("/proc/meminfo", buf, sizeof buf) <= 0) return;
    if ((p = strstr(buf, "MemTotal:")))     memTotalKb = atol(p + 9);
    if ((p = strstr(buf, "MemAvailable:"))) memAvailKb = atol(p + 13);
}

/*
 * Wi-Fi: a wireless interface that is up, with its link quality from
 * /proc/net/wireless. Otherwise a physical wired interface that is up
 * (virtual ones such as tailscale0 have no device link).
 */
static void readNet(void)
{
    DIR *d = opendir("/sys/class/net");
    struct dirent *e;
    char path[512], buf[4096], wiredIf[32] = "";

    netKind = NET_OFF;
    netQ = -1;
    netIf[0] = '\0';
    if (!d) return;
    while ((e = readdir(d))) {
	struct stat st;

	if (e->d_name[0] == '.' || !strcmp(e->d_name, "lo")) continue;
	snprintf(path, sizeof path, "/sys/class/net/%s/operstate", e->d_name);
	if (readFile(path, buf, sizeof buf) <= 0 || strcmp(buf, "up")) continue;

	snprintf(path, sizeof path, "/sys/class/net/%s/wireless", e->d_name);
	if (stat(path, &st) == 0) {
	    netKind = NET_WIFI;
	    snprintf(netIf, sizeof netIf, "%.31s", e->d_name);
	    if (readFile("/proc/net/wireless", buf, sizeof buf) > 0) {
		char key[300], *p;
		snprintf(key, sizeof key, "%s:", e->d_name);
		if ((p = strstr(buf, key))) {
		    int status;
		    double link;
		    if (sscanf(p + strlen(key), "%x %lf", &status, &link) == 2) {
			netQ = (int)(link * 100 / 70 + 0.5);
			if (netQ > 100) netQ = 100;
		    }
		}
	    }
	} else {
	    snprintf(path, sizeof path, "/sys/class/net/%s/device", e->d_name);
	    if (stat(path, &st) == 0 && !wiredIf[0])
		snprintf(wiredIf, sizeof wiredIf, "%.31s", e->d_name);
	}
    }
    closedir(d);
    if (netKind == NET_OFF && wiredIf[0]) {
	netKind = NET_LAN;
	snprintf(netIf, sizeof netIf, "%s", wiredIf);
    }
}

/* a number from /sys/class/power_supply/<dev>/<what>, -1 if missing */
static long readSupply(const char *dev, const char *what)
{
    char path[512], buf[32];

    snprintf(path, sizeof path, "/sys/class/power_supply/%s/%s", dev, what);
    return readFile(path, buf, sizeof buf) > 0 ? atol(buf) : -1;
}

/* the system batteries (scope "Device" is a mouse or keyboard), added up
   when there are several (ThinkPads with BAT0 + BAT1) */
static void readBattery(void)
{
    DIR *d = opendir("/sys/class/power_supply");
    struct dirent *e;
    char path[512], buf[64];
    long now, full, sumNow = 0, sumFull = 0;
    int mains = -1, capSum = 0, nCap = 0, charging = 0, isFull = 0;

    batPresent = 0;
    if (!d) return;
    while ((e = readdir(d))) {
	if (e->d_name[0] == '.') continue;
	snprintf(path, sizeof path, "/sys/class/power_supply/%s/type", e->d_name);
	if (readFile(path, buf, sizeof buf) <= 0) continue;
	/* USB: USB-PD adapters (Chromebooks, Pinebook, many thin laptops) */
	if (!strcmp(buf, "Mains") || !strcmp(buf, "USB")) {
	    snprintf(path, sizeof path, "/sys/class/power_supply/%s/online", e->d_name);
	    if (readFile(path, buf, sizeof buf) > 0 && mains != 1) mains = atoi(buf) != 0;
	    continue;
	}
	if (strcmp(buf, "Battery")) continue;
	snprintf(path, sizeof path, "/sys/class/power_supply/%s/scope", e->d_name);
	if (readFile(path, buf, sizeof buf) > 0 && !strcmp(buf, "Device")) continue;
	snprintf(path, sizeof path, "/sys/class/power_supply/%s/present", e->d_name);
	if (readFile(path, buf, sizeof buf) > 0 && atoi(buf) == 0) continue;	/* empty bay */
	/* capacity is against the design size on some batteries (Apple's
	   bq20z451): a worn one never gets near 100, so prefer now/full */
	if ((now = readSupply(e->d_name, "charge_now")) >= 0 &&
	    (full = readSupply(e->d_name, "charge_full")) > 0)
	    ;
	else if ((now = readSupply(e->d_name, "energy_now")) >= 0)
	    full = readSupply(e->d_name, "energy_full");
	if (now >= 0 && full > 0) {
	    sumNow += now;
	    sumFull += full;
	} else if ((now = readSupply(e->d_name, "capacity")) >= 0) {
	    capSum += (int)now;
	    ++nCap;
	} else continue;
	snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", e->d_name);
	if (readFile(path, buf, sizeof buf) > 0) {
	    charging |= !strcmp(buf, "Charging");
	    isFull |= !strcmp(buf, "Full") || !strcmp(buf, "Not charging");
	}
	batPresent = 1;
    }
    closedir(d);
    if (batPresent) {
	if (sumFull > 0)
	    batCap = sumNow >= sumFull ? 100 : (int)((sumNow * 100 + sumFull / 2) / sumFull);
	else batCap = capSum / nCap;
	batCharging = charging;
	batFull = isFull && !charging;
    }
    /* some batteries report Discharging while topped up on the adapter */
    if (mains == 1 && batPresent && !batCharging) batFull = 1;
    /* no adapter listed: on AC unless the battery is running down */
    acOnline = mains >= 0 ? mains : !batPresent || batCharging || batFull;
}

/*
 * Bluetooth, from the kernel: blocked in rfkill, or the adapter up
 * (HCIGETDEVINFO on an HCI socket, which needs no privileges), and the
 * connections, which show up as /sys/class/bluetooth/hci0:<handle>.
 */
#define BTPROTO_HCI	1
#define HCIGETDEVINFO	_IOR('H', 211, int)
#define HCI_UP		0x1

static int btState = -1;	/* -1: no adapter, 0: off, 1: on */
static int btConns;
static char btDev[16];		/* "hci0" */

static int btBlocked(void)
{
    DIR *d = opendir("/sys/class/rfkill");
    struct dirent *e;
    char path[512], buf[32];
    int blocked = 0;

    if (!d) return 0;
    while ((e = readdir(d))) {
	if (e->d_name[0] == '.') continue;
	snprintf(path, sizeof path, "/sys/class/rfkill/%s/type", e->d_name);
	if (readFile(path, buf, sizeof buf) <= 0 || strcmp(buf, "bluetooth")) continue;
	snprintf(path, sizeof path, "/sys/class/rfkill/%s/soft", e->d_name);
	if (readFile(path, buf, sizeof buf) > 0 && atoi(buf)) blocked = 1;
	snprintf(path, sizeof path, "/sys/class/rfkill/%s/hard", e->d_name);
	if (readFile(path, buf, sizeof buf) > 0 && atoi(buf)) blocked = 1;
    }
    closedir(d);
    return blocked;
}

static int btUp(int devId)
{
    static int sock = -2;
    union { uint16_t devId; unsigned char raw[512]; } info;
    uint32_t flags;

    if (sock == -2) sock = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
    if (sock < 0) return 1;		/* cannot tell: trust rfkill */
    memset(&info, 0, sizeof info);
    info.devId = (uint16_t)devId;
    if (ioctl(sock, HCIGETDEVINFO, &info) < 0) return 0;
    memcpy(&flags, info.raw + 16, sizeof flags);	/* after dev_id, name, bdaddr */
    return (flags & HCI_UP) != 0;
}

static void readBluetooth(void)
{
    DIR *d = opendir("/sys/class/bluetooth");
    struct dirent *e;
    size_t n;

    btState = -1;
    btConns = 0;
    btDev[0] = '\0';
    if (!d) return;
    while ((e = readdir(d)))		/* the first adapter */
	if (!strncmp(e->d_name, "hci", 3) && !strchr(e->d_name, ':') &&
	    (!btDev[0] || strcmp(e->d_name, btDev) < 0))
	    snprintf(btDev, sizeof btDev, "%.15s", e->d_name);
    if (!btDev[0]) { closedir(d); return; }
    n = strlen(btDev);
    rewinddir(d);
    while ((e = readdir(d)))
	if (!strncmp(e->d_name, btDev, n) && e->d_name[n] == ':') ++btConns;
    closedir(d);
    btState = !btBlocked() && btUp(atoi(btDev + 3));
}

/* unblock Bluetooth in rfkill (the session user may write /dev/rfkill) */
static void btUnblock(void)
{
    struct { uint32_t idx; uint8_t type, op, soft, hard; } ev = { 0, 2, 3, 0, 0 };
    int fd = open("/dev/rfkill", O_WRONLY | O_CLOEXEC);	/* type 2: bluetooth, */
							/* op 3: change all */
    if (fd < 0) return;
    if (write(fd, &ev, sizeof ev) < 0)
	fprintf(stderr, PROGNAME ": /dev/rfkill: %s\n", strerror(errno));
    close(fd);
}


/* ------------------------------------------------------------------ */
/* applications (.desktop files)                                      */

typedef struct {
    char *id;			/* e.g. "firefox.desktop" */
    char *name;
    char *exec;
    int   terminal;
    int   cat;
} App;

static App *apps;
static int nApps;

/* main categories, in menu order; the last one catches everything else */
static const struct { const char *keys; const char *label; } cats[] = {
    { "Utility;",                  N_("Accessories") },
    { "Network;",                  N_("Internet") },
    { "Office;",                   N_("Office") },
    { "Graphics;",                 N_("Graphics") },
    { "AudioVideo;Audio;Video;",   N_("Multimedia") },
    { "Development;",              N_("Development") },
    { "Education;",                N_("Education") },
    { "Science;",                  N_("Science") },
    { "Game;",                     N_("Games") },
    { "System;",                   N_("System") },
    { "Settings;",                 N_("Settings") },
    { "",                          N_("Other") },
};
#define NCATS ((int)(sizeof cats / sizeof cats[0]))

static int categoryOf(const char *list)
{
    char tok[128];
    const char *p = list;

    while (*p) {
	size_t n = strcspn(p, ";");
	int i;
	if (n > 0 && n + 2 < sizeof tok) {
	    memcpy(tok, p, n);
	    tok[n] = ';'; tok[n + 1] = '\0';
	    for (i = 0; i < NCATS - 1; ++i) {
		const char *k = cats[i].keys;
		while ((k = strstr(k, tok))) {
		    if (k == cats[i].keys || k[-1] == ';') return i;
		    ++k;
		}
	    }
	}
	p += n;
	if (*p) ++p;
    }
    return NCATS - 1;
}

/* is any of the ';' separated names in list in XDG_CURRENT_DESKTOP? */
static int inCurrentDesktop(const char *list)
{
    const char *cur = getenv("XDG_CURRENT_DESKTOP");
    char buf[256], *tok, *save;

    if (!cur || !*cur) return 0;
    snprintf(buf, sizeof buf, "%s", cur);
    for (tok = strtok_r(buf, ":", &save); tok; tok = strtok_r(0, ":", &save)) {
	const char *p = list;
	size_t n = strlen(tok);
	while ((p = strstr(p, tok))) {
	    if ((p == list || p[-1] == ';') && (p[n] == ';' || p[n] == '\0'))
		return 1;
	    p += n;
	}
    }
    return 0;
}

static int inPath(const char *prog)
{
    const char *path = getenv("PATH");
    char *buf, *dir, *save, full[PATH_MAX];
    int found = 0;

    if (!prog || !*prog) return 0;
    if (strchr(prog, '/')) return access(prog, X_OK) == 0;
    buf = strdup(path && *path ? path : "/usr/local/bin:/usr/bin:/bin");	/* NixOS: PATH > 1 KB */
    for (dir = strtok_r(buf, ":", &save); dir && !found; dir = strtok_r(0, ":", &save)) {
	snprintf(full, sizeof full, "%s/%s", dir, prog);
	found = access(full, X_OK) == 0;
    }
    free(buf);
    return found;
}

/* drop the %f %u ... field codes; "%%" is a literal % */
static char *cleanExec(const char *e)
{
    char *out = malloc(strlen(e) + 1), *o = out;

    while (*e) {
	if (*e == '%') {
	    if (e[1] == '%') *o++ = '%';
	    e += e[1] ? 2 : 1;
	} else {
	    *o++ = *e++;
	}
    }
    while (o > out && isspace((unsigned char)o[-1])) --o;
    *o = '\0';
    return out;
}

static int isTrue(const char *v)
{
    return !strcmp(v, "true") || !strcmp(v, "1");
}

typedef struct { char **ids; int n, cap; } StrList;

static int strListHas(StrList *l, const char *s)
{
    int i;
    for (i = 0; i < l->n; ++i) if (!strcmp(l->ids[i], s)) return 1;
    return 0;
}

static void strListAdd(StrList *l, const char *s)
{
    if (l->n == l->cap) {
	l->cap = l->cap ? l->cap * 2 : 64;
	l->ids = realloc(l->ids, l->cap * sizeof *l->ids);
    }
    l->ids[l->n++] = strdup(s);
}

static void strListFree(StrList *l)
{
    int i;
    for (i = 0; i < l->n; ++i) free(l->ids[i]);
    free(l->ids);
    l->ids = 0; l->n = l->cap = 0;
}

/* "Name[ja_JP]" and "Name[ja]" for LC_MESSAGES=ja_JP.UTF-8 */
static char locKey[40], langKey[40];

static void setLocaleKeys(void)
{
    const char *l = setlocale(LC_MESSAGES, 0);
    char ll[32];
    size_t n;

    locKey[0] = langKey[0] = '\0';
    if (!l || !strcmp(l, "C") || !strcmp(l, "POSIX") || !strncmp(l, "C.", 2)) return;
    n = strcspn(l, ".@");
    if (n >= sizeof ll) return;
    memcpy(ll, l, n);
    ll[n] = '\0';
    snprintf(locKey, sizeof locKey, "Name[%s]", ll);
    ll[strcspn(ll, "_")] = '\0';
    snprintf(langKey, sizeof langKey, "Name[%s]", ll);
    if (!strcmp(langKey, locKey)) langKey[0] = '\0';
}

static void loadDesktopFile(const char *path, const char *id)
{
    char line[2048], name[512] = "", nameLoc[512] = "", nameLang[512] = "",
	exec[1024] = "",
	catList[512] = "", type[64] = "", tryExec[512] = "",
	onlyShow[256] = "", notShow[256] = "", domain[128] = "";
    int inEntry = 0, noDisplay = 0, terminal = 0;
    FILE *f = fopen(path, "r");
    App *a;

    if (!f) return;
    while (fgets(line, sizeof line, f)) {
	char *k = line, *v, *p;

	line[strcspn(line, "\r\n")] = '\0';
	if (line[0] == '[') {
	    inEntry = !strcmp(line, "[Desktop Entry]");
	    continue;
	}
	if (!inEntry || line[0] == '#' || !(v = strchr(line, '='))) continue;
	p = v;
	while (p > k && isspace((unsigned char)p[-1])) --p;
	*p = '\0';
	++v;
	while (isspace((unsigned char)*v)) ++v;

	if      (!strcmp(k, "Name"))          setStr(name, sizeof name, v);
	else if (locKey[0] && !strcmp(k, locKey))  setStr(nameLoc, sizeof nameLoc, v);
	else if (langKey[0] && !strcmp(k, langKey)) setStr(nameLang, sizeof nameLang, v);
	else if (!strcmp(k, "Exec"))          setStr(exec, sizeof exec, v);
	else if (!strcmp(k, "Categories"))    setStr(catList, sizeof catList, v);
	else if (!strcmp(k, "Type"))          setStr(type, sizeof type, v);
	else if (!strcmp(k, "TryExec"))       setStr(tryExec, sizeof tryExec, v);
	else if (!strcmp(k, "OnlyShowIn"))    setStr(onlyShow, sizeof onlyShow, v);
	else if (!strcmp(k, "NotShowIn"))     setStr(notShow, sizeof notShow, v);
	else if (!strcmp(k, "NoDisplay"))     noDisplay |= isTrue(v);
	else if (!strcmp(k, "Hidden"))        noDisplay |= isTrue(v);
	else if (!strcmp(k, "Terminal"))      terminal = isTrue(v);
	else if (!strcmp(k, "X-GNOME-Gettext-Domain") ||
		 (!domain[0] && !strcmp(k, "X-Ubuntu-Gettext-Domain")))
					      setStr(domain, sizeof domain, v);
    }
    fclose(f);

    if (strcmp(type, "Application") || noDisplay || !name[0] || !exec[0])
	return;
    if (tryExec[0] && !inPath(tryExec)) return;
    if (onlyShow[0] && !inCurrentDesktop(onlyShow)) return;
    if (notShow[0] && inCurrentDesktop(notShow)) return;

    apps = realloc(apps, (nApps + 1) * sizeof *apps);
    a = &apps[nApps++];
    a->id = strdup(id);
    /* Ubuntu keeps many translations in language packs, not in the file */
    if (!nameLoc[0]) setStr(nameLoc, sizeof nameLoc, nameLang);
    if (!nameLoc[0] && domain[0]) setStr(nameLoc, sizeof nameLoc, dgettext(domain, name));
    a->name = strdup(nameLoc[0] ? nameLoc : name);
    a->exec = cleanExec(exec);
    a->terminal = terminal;
    a->cat = categoryOf(catList);
}

static int appCmp(const void *x, const void *y)
{
    return strcoll(((const App *)x)->name, ((const App *)y)->name);
}

static void freeApps(void)
{
    int i;
    for (i = 0; i < nApps; ++i) {
	free(apps[i].id); free(apps[i].name); free(apps[i].exec);
    }
    free(apps);
    apps = 0; nApps = 0;
}

/*
 * $XDG_DATA_HOME/applications first, then each of $XDG_DATA_DIRS. The
 * first file with a given id wins, so a Hidden=true copy in
 * ~/.local/share/applications hides the system one.
 */
static void loadApps(void)
{
    const char *home = getenv("HOME"), *dh = getenv("XDG_DATA_HOME"),
	*dd = getenv("XDG_DATA_DIRS");
    char *dirs, *dir, *save;
    StrList seen = { 0 };
    size_t n;

    freeApps();
    if (!home) home = ".";
    if (!(dd && *dd)) dd = "/usr/local/share:/usr/share";
    /* XDG_DATA_HOME, XDG_DATA_DIRS (any length: NixOS), then the Flatpak
       exports in case the session did not add them; missing dirs are skipped */
    n = strlen(home) * 2 + strlen(dd) + (dh ? strlen(dh) : 0) + 128;
    dirs = malloc(n);
    snprintf(dirs, n, "%s%s%s:%s:%s/.local/share/flatpak/exports/share:/var/lib/flatpak/exports/share",
	     dh && *dh ? dh : home, dh && *dh ? "" : "/.local/share", "", dd, home);

    for (dir = strtok_r(dirs, ":", &save); dir; dir = strtok_r(0, ":", &save)) {
	char path[1024], file[1400];
	DIR *d;
	struct dirent *e;

	snprintf(path, sizeof path, "%s/applications", dir);
	if (!(d = opendir(path))) continue;
	while ((e = readdir(d))) {
	    size_t n = strlen(e->d_name);
	    if (n < 9 || strcmp(e->d_name + n - 8, ".desktop")) continue;
	    if (strListHas(&seen, e->d_name)) continue;
	    strListAdd(&seen, e->d_name);
	    snprintf(file, sizeof file, "%s/%s", path, e->d_name);
	    loadDesktopFile(file, e->d_name);
	}
	closedir(d);
    }
    strListFree(&seen);
    qsort(apps, nApps, sizeof *apps, appCmp);
}


/* ------------------------------------------------------------------ */
/* favourites: one .desktop file name per line. With sync_wm2tj they are
   the wm2tj menu's (~/.config/wm2tj/favorites), which may also hold
   "Name" command lines; those are kept as they are. Until that file
   exists (wm2tj may still take its favourites from elsewhere) our own
   list is shown; the first change writes the shared file. Each change
   then runs wm2tj_menu_command so wm2tj's menu follows. */

#define FAVS_HEADER "# wm2tj favourites (edited with wm2tj-favorites; shown top to bottom)\n" \
		    "# format: foo.desktop  or  \"name\" command\n"

static StrList favs;
static void spawn(const char *cmd);

static void favsDir(char *buf, size_t n)
{
    size_t len = strlen(configDir) - strlen("/" PROGNAME);

    if (cfg.syncWm2tj) snprintf(buf, n, "%.*s/wm2tj", (int)len, configDir);
    else snprintf(buf, n, "%s", configDir);
}

static void loadFavs(void)
{
    char dir[560], path[600], line[512];
    FILE *f;

    strListFree(&favs);
    favsDir(dir, sizeof dir);
    snprintf(path, sizeof path, "%s/favorites", dir);
    if (!(f = fopen(path, "r")) && cfg.syncWm2tj) {
	snprintf(path, sizeof path, "%s/favorites", configDir);
	f = fopen(path, "r");
    }
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
	line[strcspn(line, "\r\n")] = '\0';
	if (line[0] && line[0] != '#' && !strListHas(&favs, line))
	    strListAdd(&favs, line);
    }
    fclose(f);
}

/* a "Name" command line: the name into name, returns the command */
static const char *favCommand(const char *line, char *name, size_t n)
{
    const char *q;

    if (line[0] != '"' || !(q = strchr(line + 1, '"'))) return 0;
    snprintf(name, n, "%.*s", (int)(q - line - 1), line + 1);
    while (isspace((unsigned char)*++q)) ;
    return *q ? q : 0;
}

static void saveFavs(void)
{
    char dir[560], path[600], tmp[620], line[512];
    FILE *f, *old;
    int i;

    favsDir(dir, sizeof dir);
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/favorites", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(f = fopen(tmp, "w"))) {
	fprintf(stderr, PROGNAME ": cannot write %s: %s\n", tmp, strerror(errno));
	return;
    }
    if ((old = fopen(path, "r"))) {	/* keep the comments at the top */
	while (fgets(line, sizeof line, old) && line[0] == '#') fputs(line, f);
	fclose(old);
    } else if (cfg.syncWm2tj) {
	fputs(FAVS_HEADER, f);
    }
    for (i = 0; i < favs.n; ++i) fprintf(f, "%s\n", favs.ids[i]);
    if (fclose(f) == 0 && rename(tmp, path) == 0 && cfg.syncWm2tj)
	spawn(cfg.syncCmd);
}

static void toggleFav(const char *id)
{
    int i;

    for (i = 0; i < favs.n; ++i) {
	if (!strcmp(favs.ids[i], id)) {
	    free(favs.ids[i]);
	    memmove(&favs.ids[i], &favs.ids[i + 1], (favs.n - i - 1) * sizeof *favs.ids);
	    --favs.n;
	    saveFavs();
	    return;
	}
    }
    strListAdd(&favs, id);
    saveFavs();
}


/* ------------------------------------------------------------------ */
/* launching                                                          */

static void spawn(const char *cmd)
{
    pid_t pid;

    if (!cmd || !*cmd) return;
    pid = fork();
    if (pid == 0) {
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa, 0);
	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
	setsid();
	execl("/bin/sh", "sh", "-c", cmd, (char *)0);
	_exit(127);
    }
    if (pid < 0) fprintf(stderr, PROGNAME ": fork: %s\n", strerror(errno));
}

static void launchApp(const App *a)
{
    if (a->terminal) {
	char *cmd = malloc(strlen(cfg.terminal) + strlen(a->exec) + 8);
	sprintf(cmd, "%s -e %s", cfg.terminal, a->exec);
	spawn(cmd);
	free(cmd);
    } else {
	spawn(a->exec);
    }
}

/* copy text that may be cut short without leaving half a UTF-8 character */
static void copyText(char *dst, size_t n, const char *src)
{
    size_t len = strlen(src);

    if (len >= n) {
	len = n - 1;
	while (len > 0 && ((unsigned char)src[len] & 0xc0) == 0x80) --len;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* run a command and wait for it; the sound and input method ones are quick */
static void runWait(char *const argv[])
{
    pid_t pid = fork();

    if (pid == 0) {
	struct sigaction sa;
	int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa, 0);
	sigaction(SIGPIPE, &sa, 0);
	if (fd >= 0) { dup2(fd, 0); dup2(fd, 1); }
	execvp(argv[0], argv);
	_exit(127);
    }
    if (pid > 0) waitpid(pid, 0, 0);	/* SA_NOCLDWAIT: returns once it has gone */
}


/* ------------------------------------------------------------------ */
/* sound (PipeWire through wpctl, or PulseAudio through pactl)        */

enum { SND_NONE, SND_WPCTL, SND_PACTL };
static int sndTool;
static int volPct = -1, volMuted, micMuted;	/* volPct -1: no sound server */
static KeyCode kcVolUp, kcVolDown, kcMute, kcMicMute;

typedef struct { char id[128]; char name[96]; int isDefault; } Sink;
#define MAXSINKS 16
static Sink sinks[MAXSINKS];
static int nSinks;

static const char *sndDevice(int source)
{
    if (sndTool == SND_WPCTL) return source ? "@DEFAULT_AUDIO_SOURCE@" : "@DEFAULT_AUDIO_SINK@";
    return source ? "@DEFAULT_SOURCE@" : "@DEFAULT_SINK@";
}

/*
 * The level of the default output (or input) in %, -1 without a server.
 * wpctl: "Volume: 0.40 [MUTED]"; pactl: "Volume: front-left: 26214 /  40% /
 * -23.88 dB, ..." and "Mute: yes".
 */
static int readLevel(int source, int *muted)
{
    const char *kind = source ? "source" : "sink", *dev = sndDevice(source);
    char cmd[256], line[512], *s;
    int pct = -1;
    FILE *p;

    *muted = 0;
    if (sndTool == SND_WPCTL)
	snprintf(cmd, sizeof cmd, "LC_ALL=C wpctl get-volume %s 2>/dev/null", dev);
    else if (sndTool == SND_PACTL)
	snprintf(cmd, sizeof cmd, "LC_ALL=C pactl get-%s-volume %s 2>/dev/null; "
		 "LC_ALL=C pactl get-%s-mute %s 2>/dev/null", kind, dev, kind, dev);
    else return -1;
    if (!(p = popen(cmd, "r"))) return -1;
    while (fgets(line, sizeof line, p)) {
	if (!strncmp(line, "Volume:", 7)) {
	    if (sndTool == SND_WPCTL) {
		pct = (int)(strtod(line + 7, 0) * 100 + 0.5);
		*muted = strstr(line, "[MUTED]") != 0;
	    } else if ((s = strchr(line, '%'))) {
		while (s > line && isdigit((unsigned char)s[-1])) --s;
		pct = atoi(s);
	    }
	} else if (!strncmp(line, "Mute:", 5)) {
	    *muted = strstr(line, "yes") != 0;
	}
    }
    pclose(p);
    return pct;
}

static void readVolume(void)
{
    volPct = readLevel(0, &volMuted);
}

/* the output to v%, and out of mute if asked */
static void setVolumePct(int v, int unmute)
{
    char pct[16], *dev = (char *)sndDevice(0);

    if (!sndTool) return;
    snprintf(pct, sizeof pct, "%d%%", v);
    if (sndTool == SND_WPCTL) {
	char *um[] = { "wpctl", "set-mute", dev, "0", 0 };
	char *set[] = { "wpctl", "set-volume", dev, pct, 0 };
	if (volMuted && unmute) runWait(um);
	runWait(set);
    } else {
	char *um[] = { "pactl", "set-sink-mute", dev, "0", 0 };
	char *set[] = { "pactl", "set-sink-volume", dev, pct, 0 };
	if (volMuted && unmute) runWait(um);
	runWait(set);
    }
    volPct = v;
    if (unmute) volMuted = 0;
}

/* the volume keys and the scroll wheel: up to 100%, or what it already is */
static void changeVolume(int steps)
{
    int v, top;

    if (!sndTool || cfg.volumeStep <= 0) return;
    if (volPct < 0) readVolume();
    if (volPct < 0) return;
    top = volPct > 100 ? volPct : 100;
    v = volPct + steps * cfg.volumeStep;
    if (v > top) v = top;
    if (v < 0) v = 0;
    setVolumePct(v, steps > 0);
    readVolume();
}

static void toggleMute(int source)
{
    char *dev = (char *)sndDevice(source);
    char *wp[] = { "wpctl", "set-mute", dev, "toggle", 0 };
    char *pa[] = { "pactl", source ? "set-source-mute" : "set-sink-mute", dev, "toggle", 0 };

    if (!sndTool) return;
    runWait(sndTool == SND_WPCTL ? wp : pa);
    if (source) readLevel(1, &micMuted);
    else readVolume();
}

static void addSink(const char *id, const char *name, int isDefault)
{
    Sink *s;
    if (nSinks >= MAXSINKS) return;
    s = &sinks[nSinks++];
    copyText(s->id, sizeof s->id, id);
    copyText(s->name, sizeof s->name, name);
    s->isDefault = isDefault;
}

/*
 * The outputs. wpctl status lists them under Audio, as
 * " │  *   55. Built-in Audio Analog Stereo [vol: 0.40]" (* for the default);
 * pactl prints the default's name, then "Name:" and "Description:" per sink.
 */
static void loadSinks(void)
{
    char line[512], def[128] = "", id[128] = "", *s, *e;
    FILE *p;

    nSinks = 0;
    if (sndTool == SND_WPCTL) {
	int audio = 0, in = 0, star;
	if (!(p = popen("LC_ALL=C wpctl status 2>/dev/null", "r"))) return;
	while (fgets(line, sizeof line, p)) {
	    line[strcspn(line, "\n")] = '\0';
	    if (isalpha((unsigned char)line[0])) { audio = !strncmp(line, "Audio", 5); continue; }
	    if (!audio) continue;
	    if (strstr(line, "─ ")) { in = strstr(line, "Sinks:") != 0; continue; }
	    if (!in) continue;
	    for (s = line, star = 0; *s && !isdigit((unsigned char)*s); ++s)
		if (*s == '*') star = 1;
	    snprintf(id, sizeof id, "%ld", strtol(s, &e, 10));
	    if (!*s || *e != '.') continue;
	    for (++e; *e == ' '; ++e) ;
	    if ((s = strstr(e, " [vol:"))) *s = '\0';
	    for (s = e + strlen(e); s > e && s[-1] == ' '; ) *--s = '\0';
	    if (*e) addSink(id, e, star);
	}
	pclose(p);
    } else if (sndTool == SND_PACTL) {
	if (!(p = popen("LC_ALL=C pactl get-default-sink 2>/dev/null; "
			"LC_ALL=C pactl list sinks 2>/dev/null", "r"))) return;
	while (fgets(line, sizeof line, p)) {
	    line[strcspn(line, "\n")] = '\0';
	    for (s = line; *s == ' ' || *s == '\t'; ++s) ;
	    if (!def[0] && s == line && strncmp(line, "Sink #", 6))
		copyText(def, sizeof def, line);
	    else if (!strncmp(s, "Name: ", 6))
		copyText(id, sizeof id, s + 6);
	    else if (!strncmp(s, "Description: ", 13) && id[0]) {
		addSink(id, s + 13, !strcmp(id, def));
		id[0] = '\0';
	    }
	}
	pclose(p);
    }
}

static void setDefaultSink(const Sink *s)
{
    char *wp[] = { "wpctl", "set-default", (char *)s->id, 0 };
    char *pa[] = { "pactl", "set-default-sink", (char *)s->id, 0 };

    runWait(sndTool == SND_WPCTL ? wp : pa);
    readVolume();
}

static void grabVolumeKeys(void)
{
    KeyCode *kc[] = { &kcVolUp, &kcVolDown, &kcMute, &kcMicMute };
    KeySym ks[] = { XF86XK_AudioRaiseVolume, XF86XK_AudioLowerVolume,
		    XF86XK_AudioMute, XF86XK_AudioMicMute };
    int i;

    if (cfg.volumeStep <= 0 || !sndTool) return;
    for (i = 0; i < 4; ++i)
	if ((*kc[i] = XKeysymToKeycode(dpy, ks[i])))
	    XGrabKey(dpy, *kc[i], AnyModifier, root, False, GrabModeAsync, GrabModeAsync);
}


/* ------------------------------------------------------------------ */
/* input method: fcitx5 (fcitx5-remote), IBus (ibus), or the XKB group */

enum { IM_NONE, IM_FCITX5, IM_IBUS, IM_XKB };
static int haveFcitx, haveIbus, xkbBase = -1;
static int imKind;		/* which one the indicator shows */
static char imName[64];		/* "keyboard-jp", "mozc-on", "xkb:us::eng", "1:ru"; "": none */
static char imTitle[96], imLabel[32];
static int imActive;		/* an input method, not just a keyboard layout */

static int imNameOk(const char *s)
{
    if (!*s) return 0;
    for (; *s; ++s)
	if (!isalnum((unsigned char)*s) && !strchr("-_.:+@", *s)) return 0;
    return 1;
}

/* input_method auto (the first one running) or one of them */
static int imWanted(int kind)
{
    static const char *names[] = { "none", "fcitx5", "ibus", "xkb" };
    return !strcmp(cfg.inputMethod, "auto") || !strcmp(cfg.inputMethod, names[kind]);
}

/* the first line a command prints, if it is a plausible name */
static int readName(const char *cmd, char *buf, size_t n)
{
    FILE *p;

    buf[0] = '\0';
    if ((p = popen(cmd, "r"))) {
	if (!fgets(buf, n, p)) buf[0] = '\0';
	pclose(p);
    }
    buf[strcspn(buf, "\r\n")] = '\0';
    if (!imNameOk(buf)) buf[0] = '\0';	/* not running: an error message */
    return buf[0] != '\0';
}

/* fcitx5: the layout of a keyboard, or Label= and Name= from the input
   method's fcitx5/inputmethod/<name>.conf, what fcitx5 itself shows */
static void fcitxDescribe(void)
{
    const char *dirs = getenv("XDG_DATA_DIRS");
    char *list, *d, *save, path[PATH_MAX], line[256];
    FILE *f;

    snprintf(imTitle, sizeof imTitle, "%s", imName);
    imActive = strncmp(imName, "keyboard-", 9) != 0;
    if (!imActive) {			/* keyboard-us-dvorak: "us" */
	snprintf(imLabel, sizeof imLabel, "%.*s", (int)strcspn(imName + 9, "-"), imName + 9);
	snprintf(imTitle, sizeof imTitle, "%s", imLabel);
	return;
    }
    snprintf(imLabel, sizeof imLabel, "%.2s", imName);
    list = strdup(dirs && *dirs ? dirs : "/usr/local/share:/usr/share");
    for (d = strtok_r(list, ":", &save); d; d = strtok_r(0, ":", &save)) {
	snprintf(path, sizeof path, "%s/fcitx5/inputmethod/%s.conf", d, imName);
	if (!(f = fopen(path, "re"))) continue;
	while (fgets(line, sizeof line, f)) {
	    line[strcspn(line, "\r\n")] = '\0';
	    if (!strncmp(line, "Label=", 6) && line[6])
		copyText(imLabel, sizeof imLabel, line + 6);
	    else if (!strncmp(line, "Name=", 5) && line[5])
		copyText(imTitle, sizeof imTitle, line + 5);
	}
	fclose(f);
	break;
    }
}

/*
 * IBus. An engine is described in a component file (ibus/component/NAME.xml)
 * by an <engine> block with <name>, <symbol> (what IBus's own panel shows:
 * "あ", "拼", "한"), <longname> and <layout>. Some components list their
 * engines by running a program instead: <engines exec="… --xml"/>.
 */
typedef struct { char name[64], symbol[32], longname[96], layout[32]; } IbusEngine;
#define IBUS_CACHE 32
static IbusEngine ibusCache[IBUS_CACHE];
static int nIbusCache;

/* the text of <tag> inside [blk, end), with the usual entities */
static void xmlTag(const char *blk, const char *end, const char *tag, char *out, size_t n)
{
    static const char *ent[][2] = { { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" },
				    { "&quot;", "\"" }, { "&apos;", "'" } };
    char open[40], close[40], tmp[256];
    const char *s, *e;
    size_t k = 0, i;

    out[0] = '\0';
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    if (!(s = strstr(blk, open)) || s >= end) return;
    s += strlen(open);
    if (!(e = strstr(s, close)) || e > end) return;
    while (s < e && k + 1 < sizeof tmp) {
	for (i = 0; i < 5; ++i)
	    if (!strncmp(s, ent[i][0], strlen(ent[i][0]))) break;
	if (i < 5) { tmp[k++] = ent[i][1][0]; s += strlen(ent[i][0]); }
	else tmp[k++] = *s++;
    }
    tmp[k] = '\0';
    copyText(out, n, tmp);
}

static int ibusFindIn(const char *xml, const char *name, IbusEngine *out)
{
    const char *b = xml, *e;
    char nm[128];

    while ((b = strstr(b, "<engine>")) && (e = strstr(b, "</engine>"))) {
	xmlTag(b, e, "name", nm, sizeof nm);
	if (!strcmp(nm, name)) {
	    snprintf(out->name, sizeof out->name, "%s", name);
	    xmlTag(b, e, "symbol", out->symbol, sizeof out->symbol);
	    xmlTag(b, e, "longname", out->longname, sizeof out->longname);
	    xmlTag(b, e, "layout", out->layout, sizeof out->layout);
	    return 1;
	}
	b = e + 9;
    }
    return 0;
}

static char *slurp(FILE *f)
{
    size_t len = 0, cap = 0, r;
    char *buf = 0;

    do {
	if (cap - len < 4097) buf = realloc(buf, cap += 65536);
	r = fread(buf + len, 1, cap - len - 1, f);
	len += r;
    } while (r > 0);
    buf[len] = '\0';
    return buf;
}

/* look the engine up in every component file; the result is kept */
static const IbusEngine *ibusEngine(const char *name)
{
    const char *env = getenv("IBUS_COMPONENT_PATH"), *dd = getenv("XDG_DATA_DIRS");
    const char *sub = "";
    char *list, *d, *save, path[PATH_MAX + 300], compdir[PATH_MAX];
    IbusEngine e;
    int i, found = 0;

    for (i = 0; i < nIbusCache; ++i)
	if (!strcmp(ibusCache[i].name, name)) return &ibusCache[i];
    memset(&e, 0, sizeof e);
    /* IBUS_COMPONENT_PATH, else <each XDG data dir>/ibus/component (NixOS has
       no /usr/share; its dirs are in XDG_DATA_DIRS) */
    if (env && *env) list = strdup(env);
    else {
	list = strdup(dd && *dd ? dd : "/usr/local/share:/usr/share");
	sub = "/ibus/component";
    }
    for (d = strtok_r(list, ":", &save); d && !found; d = strtok_r(0, ":", &save)) {
	DIR *dir;
	snprintf(compdir, sizeof compdir, "%s%s", d, sub);
	d = compdir;
	dir = opendir(d);
	struct dirent *de;
	if (!dir) continue;
	while (!found && (de = readdir(dir))) {
	    char *xml, *x, *q;
	    FILE *f;
	    size_t l = strlen(de->d_name);
	    if (l < 5 || strcmp(de->d_name + l - 4, ".xml")) continue;
	    snprintf(path, sizeof path, "%s/%s", d, de->d_name);
	    if (!(f = fopen(path, "re"))) continue;
	    xml = slurp(f);
	    fclose(f);
	    found = ibusFindIn(xml, name, &e);
	    if (!found && (x = strstr(xml, "<engines exec=\"")) &&
		(q = strchr(x += 15, '"'))) {
		*q = '\0';
		if ((f = popen(x, "r"))) {
		    char *out = slurp(f);
		    pclose(f);
		    found = ibusFindIn(out, name, &e);
		    free(out);
		}
	    }
	    free(xml);
	}
	closedir(dir);
    }
    if (!found) snprintf(e.name, sizeof e.name, "%s", name);
    i = nIbusCache < IBUS_CACHE ? nIbusCache++ : IBUS_CACHE - 1;
    ibusCache[i] = e;
    return &ibusCache[i];
}

/* xkb:us::eng is a plain layout; another engine is an input method, shown
   lit unless its symbol is plain ASCII (a direct-input mode: Mozc's "A") */
static void ibusDescribe(void)
{
    const IbusEngine *e = ibusEngine(imName);
    const char *s;

    snprintf(imTitle, sizeof imTitle, "%s", e->longname[0] ? e->longname : imName);
    if (!strncmp(imName, "xkb:", 4)) {
	imActive = 0;
	snprintf(imLabel, sizeof imLabel, "%.*s", (int)strcspn(imName + 4, ":"), imName + 4);
	return;
    }
    if (e->symbol[0]) {
	copyText(imLabel, sizeof imLabel, e->symbol);
	for (imActive = 0, s = e->symbol; *s; ++s)
	    if ((unsigned char)*s >= 0x80) imActive = 1;
    } else {
	snprintf(imLabel, sizeof imLabel, "%.2s", imName);
	imActive = 1;
    }
}

/*
 * XKB: several layouts without an input method framework (setxkbmap -layout
 * us,ru). The layouts are in the root window's _XKB_RULES_NAMES, the
 * current one is the keyboard's group; a change arrives as an XKB event.
 */
static int xkbLayouts(char lay[][16], int max)
{
    Atom type;
    int format, n = 0, i;
    unsigned long count, after;
    unsigned char *data = 0;
    const char *s, *end;

    if (XGetWindowProperty(dpy, root, XInternAtom(dpy, "_XKB_RULES_NAMES", False),
			   0, 1024, False, XA_STRING, &type, &format, &count, &after,
			   &data) != Success || !data) return 0;
    end = (char *)data + count;
    for (s = (char *)data, i = 0; s < end && i < 2; ++i) s += strlen(s) + 1;
    while (s < end && *s && n < max) {	/* rules, model, then "us,ru" */
	size_t l = strcspn(s, ",");
	snprintf(lay[n++], 16, "%.*s", (int)l, s);
	s += l + (s[l] == ',');
    }
    XFree(data);
    return n;
}

static int xkbGroup(void)
{
    XkbStateRec st;
    return XkbGetState(dpy, XkbUseCoreKbd, &st) == Success ? st.group : 0;
}

/* the long name of a group, "English (US)" */
static void xkbGroupName(int g, char *out, size_t n)
{
    XkbDescPtr d = XkbAllocKeyboard();
    char *s;

    out[0] = '\0';
    if (!d) return;
    d->device_spec = XkbUseCoreKbd;
    if (XkbGetNames(dpy, XkbGroupNamesMask, d) == Success && d->names &&
	g < XkbNumKbdGroups && d->names->groups[g] &&
	(s = XGetAtomName(dpy, d->names->groups[g]))) {
	copyText(out, n, s);
	XFree(s);
    }
    XkbFreeKeyboard(d, 0, True);
}

static int readXkb(char *buf, size_t n)
{
    char lay[XkbNumKbdGroups][16];
    int nl = xkbLayouts(lay, XkbNumKbdGroups), g = xkbGroup();

    buf[0] = '\0';
    if (nl < 2) return 0;		/* one layout: nothing to show */
    if (g >= nl) g = 0;
    snprintf(buf, n, "%d:%.15s", g, lay[g]);
    return 1;
}

static void xkbDescribe(void)
{
    const char *lay = strchr(imName, ':') + 1;

    imActive = 0;
    snprintf(imLabel, sizeof imLabel, "%s", lay);
    xkbGroupName(atoi(imName), imTitle, sizeof imTitle);
    if (!imTitle[0]) snprintf(imTitle, sizeof imTitle, "%s", lay);
}

static int readOne(int kind, char *buf, size_t n)
{
    switch (kind) {
    case IM_FCITX5:
	return haveFcitx && imWanted(kind) && readName("fcitx5-remote -n 2>/dev/null", buf, n);
    case IM_IBUS:
	return haveIbus && imWanted(kind) && readName("ibus engine 2>/dev/null", buf, n);
    case IM_XKB:
	return xkbBase >= 0 && imWanted(kind) && readXkb(buf, n);
    }
    return 0;
}

/* the current input method; every second while one is shown (fcitx5
   about 5 ms, ibus about 20 ms), every five seconds otherwise */
static void trayRefilter(void);

static void readIme(void)
{
    char buf[64] = "";
    int kind = IM_NONE, k, was = imKind;

    if (imKind != IM_NONE && readOne(imKind, buf, sizeof buf)) kind = imKind;
    for (k = IM_FCITX5; !kind && k <= IM_XKB; ++k)
	if (k != imKind && readOne(k, buf, sizeof buf)) kind = k;
    if (kind == imKind && !strcmp(buf, imName)) return;
    imKind = kind;
    snprintf(imName, sizeof imName, "%s", buf);
    if (kind == IM_FCITX5) fcitxDescribe();
    else if (kind == IM_IBUS) ibusDescribe();
    else if (kind == IM_XKB) xkbDescribe();
    if (kind != was) trayRefilter();	/* the input method's own icon comes and goes with ours */
}

static void xkbStart(void)
{
    int op, ev, err, major = XkbMajorVersion, minor = XkbMinorVersion;

    if (!XkbQueryExtension(dpy, &op, &ev, &err, &major, &minor)) return;
    xkbBase = ev;
    XkbSelectEventDetails(dpy, XkbUseCoreKbd, XkbStateNotify,
			  XkbGroupStateMask, XkbGroupStateMask);
}

/* the menu of input methods (IBus) or layouts (XKB) */
typedef struct { char id[64]; char label[128]; int current; } Ime;
#define MAXIMES 24
static Ime imes[MAXIMES];
static int nImes;

static void addIme(const char *id, const char *label, int current)
{
    Ime *m;
    if (nImes >= MAXIMES) return;
    m = &imes[nImes++];
    snprintf(m->id, sizeof m->id, "%s", id);
    copyText(m->label, sizeof m->label, label);
    m->current = current;
}

/* IBus: the engines chosen in its preferences (preload-engines), as
   ['xkb:us::eng', 'mozc-jp'] */
static void loadImes(void)
{
    char line[2048], *s, *e;
    FILE *p;
    int i, cur = 0;

    nImes = 0;
    if (imKind == IM_IBUS) {
	if ((p = popen("gsettings get org.freedesktop.ibus.general preload-engines 2>/dev/null", "r"))) {
	    if (!fgets(line, sizeof line, p)) line[0] = '\0';
	    pclose(p);
	    for (s = line; (s = strchr(s, '\'')) && (e = strchr(s + 1, '\'')); s = e + 1) {
		const IbusEngine *en;
		*e = '\0';
		if (!imNameOk(s + 1)) continue;
		en = ibusEngine(s + 1);
		addIme(s + 1, en->longname[0] ? en->longname : en->name, !strcmp(s + 1, imName));
		cur |= !strcmp(s + 1, imName);
	    }
	}
	if (!cur && imName[0]) {	/* chosen some other way: still listed */
	    memmove(&imes[1], &imes[0], (nImes < MAXIMES ? nImes : MAXIMES - 1) * sizeof *imes);
	    if (nImes < MAXIMES) ++nImes;
	    snprintf(imes[0].id, sizeof imes[0].id, "%s", imName);
	    copyText(imes[0].label, sizeof imes[0].label, imTitle);
	    imes[0].current = 1;
	}
    } else if (imKind == IM_XKB) {
	char lay[XkbNumKbdGroups][16], id[8], name[96];
	int nl = xkbLayouts(lay, XkbNumKbdGroups), g = xkbGroup();
	for (i = 0; i < nl; ++i) {
	    xkbGroupName(i, name, sizeof name);
	    snprintf(id, sizeof id, "%d", i);
	    addIme(id, name[0] ? name : lay[i], i == g);
	}
    }
}

static void selectIme(const Ime *m)
{
    if (imKind == IM_IBUS) {
	char *argv[] = { "ibus", "engine", (char *)m->id, 0 };
	runWait(argv);
    } else if (imKind == IM_XKB) {
	XkbLockGroup(dpy, XkbUseCoreKbd, atoi(m->id));
	XSync(dpy, False);
    }
    readIme();
}

/* fcitx5 has no menu: a click switches, like its own key */
static void toggleIme(void)
{
    char *argv[] = { "fcitx5-remote", "-t", 0 };
    runWait(argv);
    readIme();
}


/* ------------------------------------------------------------------ */
/* the bar                                                            */

static Window bar;
static Pixmap barGloss, barBack, taskGloss;
static XftDraw *barDraw;

static int nDesktops, curDesktop;
static Window activeWin;

/* running applications: _NET_CLIENT_LIST, in the order wm2tj keeps it */
typedef struct {
    Window w;
    char title[256];
    long desktop;		/* 0xFFFFFFFF: on every desktop */
    int iconic;
} Task;
#define MAXTASKS 64
static Task tasks[MAXTASKS];
static int nTasks;

enum { H_NONE, H_MENU, H_DESKTOP, H_CPU, H_MEM, H_NET, H_BT, H_BAT, H_CLOCK, H_TASK,
       H_IME, H_VOL };
typedef struct { int x0, x1, kind, arg; } Hit;
static Hit hits[64];
static int nHits;
static int hoverKind = H_NONE, hoverArg = -1;
static int menuOpen;		/* the bar's ■ is pressed in */

static void addHit(int x0, int x1, int kind, int arg)
{
    if (nHits < (int)(sizeof hits / sizeof hits[0])) {
	hits[nHits].x0 = x0; hits[nHits].x1 = x1;
	hits[nHits].kind = kind; hits[nHits].arg = arg;
	++nHits;
    }
}

static Hit *hitAt(int x)
{
    int i;
    for (i = 0; i < nHits; ++i)
	if (x >= hits[i].x0 && x < hits[i].x1) return &hits[i];
    return 0;
}

static int baseline(XftFont *f, int h)
{
    return (h - (f->ascent + f->descent)) / 2 + f->ascent;
}

/*
 * The tab at the left end, a wm2tj window tab laid on its side:
 *
 *   +-------------------------------------------+
 *   | +--+                                     /
 *   | |■ |  label                             /     (the rest is black)
 *   | +--+                                   /
 *   +--------------------------------------+
 *
 * Measured off the real tab (tab_width 26): 1px border, a 2px strip,
 * the button notch (its edge, a 1px see-through gap, the button with
 * its own 1px border), and a 45 degree cut at the far end. The tab is
 * tab_length long before the cut, as in wm2tj.
 */
static int tabLen(void)   { return cfg.tabLength + barH; }
static int taskH(void)    { return barH - 6; }
static int notchLo(void)  { return 3; }
static int notchHi(void)  { return barH - 4; }

static void renderTab(Pixmap p)
{
    int L = cfg.tabLength, TL = tabLen(), x, y;
    XImage *img = XCreateImage(dpy, vis, depth, ZPixmap, 0, 0, TL, barH, 32, 0);
    double border[3], back[3], c[3];
    XColor xc;

    XParseColor(dpy, cmap, cfg.borders, &xc);
    border[0] = xc.red / 65535.0; border[1] = xc.green / 65535.0; border[2] = xc.blue / 65535.0;
    XParseColor(dpy, cmap, cfg.barBackground, &xc);
    back[0] = xc.red / 65535.0; back[1] = xc.green / 65535.0; back[2] = xc.blue / 65535.0;

    img->data = malloc((size_t)img->bytes_per_line * barH);
    for (y = 0; y < barH; ++y) {
	for (x = 0; x < TL; ++x) {
	    int end = L + barH - 1 - y;	/* the cut: one pixel shorter per row */
	    int nx = x >= notchLo() && x <= notchHi(),
		ny = y >= notchLo() && y <= notchHi();
	    if (x >= end) {
		memcpy(c, back, sizeof c);
	    } else if (x == 0 || y == 0 || y == barH - 1 || x == end - 1 ||
		       (nx && ny && (x == notchLo() || x == notchHi() ||
				     y == notchLo() || y == notchHi()))) {
		memcpy(c, border, sizeof c);
	    } else if (nx && ny) {
		memcpy(c, back, sizeof c);	/* the gap; shaped away below */
	    } else if (glossOn) {
		glossColour(c, glossBase, (double)y / (barH - 1), (double)x / (TL - 1));
	    } else {
		XColor f;
		XParseColor(dpy, cmap, cfg.background, &f);
		c[0] = f.red / 65535.0; c[1] = f.green / 65535.0; c[2] = f.blue / 65535.0;
	    }
	    XPutPixel(img, x, y, packPixel(c));
	}
    }
    XPutImage(dpy, p, gc, img, 0, 0, 0, 0, TL, barH);
    XDestroyImage(img);
}

/* cut the 1px gap round the button out of the window, as wm2tj does */
static void shapeBar(void)
{
    XRectangle r[4];
    int lo = notchLo() + 1, hi = notchHi() - 1, n = hi - lo + 1;

    r[0].x = lo; r[0].y = lo; r[0].width = n; r[0].height = 1;
    r[1].x = lo; r[1].y = hi; r[1].width = n; r[1].height = 1;
    r[2].x = lo; r[2].y = lo; r[2].width = 1; r[2].height = n;
    r[3].x = hi; r[3].y = lo; r[3].width = 1; r[3].height = n;
    XShapeCombineMask(dpy, bar, ShapeBounding, 0, 0, None, ShapeSet);
    XShapeCombineRectangles(dpy, bar, ShapeBounding, 0, 0, r, 4,
			    ShapeSubtract, Unsorted);
}

/* the status icons: drawn with rectangles, centred on the bar */
static int iconMid(void) { return barH / 2; }

static int batLow(void)
{
    return batPresent && !batCharging && !batFull && batCap <= 15;
}

/* a small graph frame with a dark inside */
static void drawWell(int x, int y, int w, int h)
{
    XftDrawRect(barDraw, &cWell, x, y, w, h);
    drawBox(barDraw, &cWellLine, x, y, w, h);
}

/* a vertical gauge filled from the bottom; red when nearly full */
#define VBAR_W 8
static void drawVBar(int x, int pct)
{
    int gh = barH - 8, gy = 4, h;

    if (pct < 0) pct = 0; else if (pct > 100) pct = 100;
    drawWell(x, gy, VBAR_W, gh);
    h = (pct * (gh - 2) + 50) / 100;
    if (pct > 0 && h < 1) h = 1;
    XftDrawRect(barDraw, pct >= 90 ? &cHot : &cMeter, x + 1, gy + gh - 1 - h,
		VBAR_W - 2, h);
}

/* CPU: a chip and its gauge */
static void drawCpu(int x)
{
    int m = iconMid(), i;

    drawBox(barDraw, &cText, x + 2, m - 4, 9, 9);
    XftDrawRect(barDraw, &cText, x + 5, m - 1, 3, 3);
    for (i = 0; i < 3; ++i) {
	XftDrawRect(barDraw, &cText, x + 4 + i * 2, m - 6, 1, 2);
	XftDrawRect(barDraw, &cText, x + 4 + i * 2, m + 5, 1, 2);
	XftDrawRect(barDraw, &cText, x, m - 2 + i * 2, 2, 1);
	XftDrawRect(barDraw, &cText, x + 11, m - 2 + i * 2, 2, 1);
    }
    drawVBar(x + 13 + 3, cpuPct);
}

/* memory: a module and its gauge */
static void drawMem(int x)
{
    int m = iconMid(), i;

    drawBox(barDraw, &cText, x, m - 4, 16, 7);
    for (i = 0; i < 3; ++i) XftDrawRect(barDraw, &cText, x + 2 + i * 4, m - 2, 3, 3);
    for (i = 0; i < 7; ++i) XftDrawRect(barDraw, &cText, x + 1 + i * 2, m + 3, 1, 2);
    drawVBar(x + 16 + 3, (int)((memTotalKb - memAvailKb) * 100 / memTotalKb));
}

/* Wi-Fi: four rising bars lit by signal; wired: a little network */
static void drawNet(int x)
{
    int m = iconMid(), i;

    if (netKind == NET_LAN) {
	drawBox(barDraw, &cText, x + 5, m - 7, 6, 5);
	XftDrawRect(barDraw, &cText, x + 7, m - 2, 1, 3);
	XftDrawRect(barDraw, &cText, x + 2, m + 1, 12, 1);
	XftDrawRect(barDraw, &cText, x + 2, m + 1, 1, 2);
	XftDrawRect(barDraw, &cText, x + 13, m + 1, 1, 2);
	drawBox(barDraw, &cText, x, m + 3, 5, 5);
	drawBox(barDraw, &cText, x + 11, m + 3, 5, 5);
	return;
    }
    for (i = 0; i < 4; ++i) {
	int h = 4 + i * 3;
	int lit = netKind == NET_WIFI && (netQ < 0 ? 1 : netQ > i * 25);
	XftDrawRect(barDraw, lit ? &cText : &cOff, x + i * 4, m + 7 - h, 3, h);
    }
    if (netKind == NET_OFF) {	/* a small cross: no network */
	for (i = 0; i < 5; ++i) {
	    XftDrawRect(barDraw, &cHot, x + 11 + i, m - 7 + i, 1, 1);
	    XftDrawRect(barDraw, &cHot, x + 15 - i, m - 7 + i, 1, 1);
	}
    }
}

/* the Bluetooth rune: grey when off, blue while something is connected */
static void drawBt(int x)
{
    int m = iconMid(), cx = x + 5;
    XPoint p[6] = {
	{ (short)(cx - 4), (short)(m - 4) }, { (short)(cx + 4), (short)(m + 3) },
	{ (short)cx,       (short)(m + 7) }, { (short)cx,       (short)(m - 7) },
	{ (short)(cx + 4), (short)(m - 3) }, { (short)(cx - 4), (short)(m + 4) },
    };

    XSetForeground(dpy, gc, (btState <= 0 ? &cOff : btConns ? &cMeter : &cText)->pixel);
    XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
    XDrawLines(dpy, barBack, gc, p, 6, CoordModeOrigin);
    XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
}

/* battery: an outline filled to the level, a bolt when charging */
static int drawBattery(int x, int draw)
{
    int m = iconMid(), w = 22, h = 12, fill;
    XPoint bolt[6];

    if (!draw) return w + 2;
    drawBox(barDraw, &cText, x, m - h / 2, w, h);
    XftDrawRect(barDraw, &cText, x + w, m - 3, 2, 6);
    fill = (batCap * (w - 4) + 50) / 100;
    if (fill > w - 4) fill = w - 4;
    XftDrawRect(barDraw, batLow() ? &cHot : &cBatFill, x + 2, m - h / 2 + 2, fill, h - 4);
    if (batCharging) {
	int cx = x + w / 2, top = m - h / 2 - 1;
	bolt[0].x = cx + 1; bolt[0].y = top;
	bolt[1].x = cx - 4; bolt[1].y = m + 1;
	bolt[2].x = cx;     bolt[2].y = m + 1;
	bolt[3].x = cx - 1; bolt[3].y = top + h + 2;
	bolt[4].x = cx + 4; bolt[4].y = m - 1;
	bolt[5].x = cx;     bolt[5].y = m - 1;
	XSetForeground(dpy, gc, cBarBg.pixel);
	XSetLineAttributes(dpy, gc, 2, LineSolid, CapRound, JoinRound);
	XDrawLines(dpy, barBack, gc, bolt, 6, CoordModeOrigin);
	XDrawLine(dpy, barBack, gc, bolt[5].x, bolt[5].y, bolt[0].x, bolt[0].y);
	XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
	XSetForeground(dpy, gc, cWarn.pixel);
	XFillPolygon(dpy, barBack, gc, bolt, 6, Nonconvex, CoordModeOrigin);
    }
    return w + 2;
}

/* the speaker: up to three waves for the level, a red cross when muted */
#define VOL_W 18
static void drawVol(int x)
{
    int m = iconMid(), i, waves = volPct <= 0 ? 0 : volPct < 34 ? 1 : volPct < 67 ? 2 : 3;
    XPoint cone[4] = {
	{ (short)(x + 3), (short)(m - 2) }, { (short)(x + 8), (short)(m - 7) },
	{ (short)(x + 8), (short)(m + 8) }, { (short)(x + 3), (short)(m + 3) },
    };
    XftColor *c = volMuted ? &cDim : &cText;

    XftDrawRect(barDraw, c, x, m - 2, 3, 5);
    XSetForeground(dpy, gc, c->pixel);
    XFillPolygon(dpy, barBack, gc, cone, 4, Convex, CoordModeOrigin);
    if (volMuted) {
	for (i = 0; i < 6; ++i) {
	    XftDrawRect(barDraw, &cHot, x + 11 + i, m - 3 + i, 2, 1);
	    XftDrawRect(barDraw, &cHot, x + 16 - i, m - 3 + i, 2, 1);
	}
	return;
    }
    XSetLineAttributes(dpy, gc, 2, LineSolid, CapButt, JoinMiter);
    for (i = 0; i < 3; ++i) {
	int r = 3 + i * 3;
	XSetForeground(dpy, gc, (i < waves ? &cText : &cOff)->pixel);
	XDrawArc(dpy, barBack, gc, x + 8 - r, m - r, 2 * r, 2 * r, -50 * 64, 100 * 64);
    }
    XSetLineAttributes(dpy, gc, 0, LineSolid, CapButt, JoinMiter);
}

/* the input method: its label in a box, lit while it is on */
static int imeWidth(void)
{
    int w = textWidth(barFont, imLabel) + 10;
    return w < barH - 8 ? barH - 8 : w;
}

static void drawIme(int x)
{
    int w = imeWidth(), h = barH - 8, tw = textWidth(barFont, imLabel);

    if (imActive) XftDrawRect(barDraw, &cButtonOn, x, 4, w, h);
    else drawBox(barDraw, &cDeskLine, x, 4, w, h);
    drawLabel(barDraw, barFont, imActive ? &cFg : &cText, x + (w - tw) / 2,
	      baseline(barFont, barH), imLabel, imActive && glossOn);
}

/* the numbers behind an icon, shown while the pointer is over it */
static int nMenus;
static Window tipWin;
static char tipCur[300];
static Pixmap tipBack;
static XftDraw *tipDraw;

static void hideTip(void)
{
    if (!tipWin) return;
    XftDrawDestroy(tipDraw);
    XFreePixmap(dpy, tipBack);
    XDestroyWindow(dpy, tipWin);
    tipWin = None;
    tipCur[0] = '\0';
}

/* a tip shown for a moment without the pointer: after a volume key */
static int flashKind = H_NONE, flashArg;
static time_t flashUntil;

static void flashTip(int kind, int arg)
{
    flashKind = kind;
    flashArg = arg;
    flashUntil = time(0) + 2;
}

static int tipText(int kind, int arg, char *buf, size_t n)
{
    time_t now = time(0);

    switch (kind) {
    case H_CPU:
	if (cpuPct < 0) return 0;
	snprintf(buf, n, "CPU %d%%", cpuPct);
	return 1;
    case H_MEM:
	snprintf(buf, n, _("Memory %.1f / %.1f GB (%ld%%)"),
		 (memTotalKb - memAvailKb) / 1048576.0, memTotalKb / 1048576.0,
		 (memTotalKb - memAvailKb) * 100 / memTotalKb);
	return 1;
    case H_NET:
	if (netKind == NET_WIFI && netQ >= 0)
	    snprintf(buf, n, "Wi-Fi %d%% (%s)", netQ, netIf);
	else if (netKind == NET_WIFI) snprintf(buf, n, "Wi-Fi (%s)", netIf);
	else if (netKind == NET_LAN) snprintf(buf, n, _("Wired (%s)"), netIf);
	else snprintf(buf, n, "%s", _("Not connected"));
	return 1;
    case H_BT:
	if (btState <= 0) snprintf(buf, n, "%s", _("Bluetooth off"));
	else if (!btConns) snprintf(buf, n, "%s", _("Bluetooth on"));
	else snprintf(buf, n, ngettext("Bluetooth: %d device connected",
				       "Bluetooth: %d devices connected", btConns), btConns);
	return 1;
    case H_BAT:
	snprintf(buf, n, _("Battery %d%% %s"), batCap,
		 batCharging ? _("charging") : batFull ? _("plugged in") : _("on battery"));
	return 1;
    case H_CLOCK:
	/* TRANSLATORS: strftime(3) format of the date shown over the clock */
	/* xgettext:no-c-format */
	strftime(buf, n, _("%a %d %b %Y %H:%M"), localtime(&now));
	return 1;
    case H_TASK:
	if (arg < 0 || arg >= nTasks || !tasks[arg].title[0]) return 0;
	snprintf(buf, n, "%s", tasks[arg].title);
	return 1;
    case H_VOL:
	if (arg == 1) snprintf(buf, n, "%s", micMuted ? _("Microphone muted") : _("Microphone on"));
	else if (volPct < 0) return 0;
	else snprintf(buf, n, volMuted ? _("Volume %d%% (muted)") : _("Volume %d%%"), volPct);
	return 1;
    case H_IME:
	if (!imName[0]) return 0;
	snprintf(buf, n, imActive ? _("Input method: %s") : _("Keyboard: %s"), imTitle);
	return 1;
    }
    return 0;
}

static void showTip(void)
{
    char text[300];
    XSetWindowAttributes a;
    int i, w, h, x = 0, x1 = 0, flash = flashKind != H_NONE;
    int kind = flash ? flashKind : hoverKind, arg = flash ? flashArg : hoverArg;

    if (nMenus || !tipText(kind, arg, text, sizeof text)) { hideTip(); return; }
    if (tipWin && !strcmp(text, tipCur)) return;
    hideTip();
    snprintf(tipCur, sizeof tipCur, "%s", text);
    for (i = 0; i < nHits; ++i)
	if (hits[i].kind == kind && (flash || hits[i].arg == arg)) {
	    x = hits[i].x0; x1 = hits[i].x1;
	}
    w = textWidth(menuFont, text) + 16;
    h = menuFont->ascent + menuFont->descent + 8;
    x = (x + x1 - w) / 2;
    if (x + w > scrW - 2) x = scrW - 2 - w;
    if (x < 2) x = 2;

    a.override_redirect = True;
    a.save_under = True;
    a.background_pixmap = None;
    tipWin = XCreateWindow(dpy, root, x, barH + 2, w, h, 0, depth, InputOutput,
			   vis, CWOverrideRedirect | CWSaveUnder | CWBackPixmap, &a);
    tipBack = XCreatePixmap(dpy, root, w, h, depth);
    tipDraw = XftDrawCreate(dpy, tipBack, vis, cmap);
    XftDrawRect(tipDraw, &cMenuBg, 0, 0, w, h);
    drawBox(tipDraw, &cMenuBorder, 0, 0, w, h);
    drawLabel(tipDraw, menuFont, &cMenuFg, 8, baseline(menuFont, h), text, 0);
    XSetWindowBackgroundPixmap(dpy, tipWin, tipBack);
    XMapRaised(dpy, tipWin);
}

static int taskVisible(const Task *t)
{
    return t->desktop == 0xFFFFFFFFL || t->desktop == curDesktop || nDesktops <= 1;
}

/*
 * The running applications of this desktop, between the tab and the
 * desktops: the active one lit like a wm2tj tab, hidden ones dimmed.
 */
static void drawTasks(int x0, int x1)
{
    int i, n = 0, w, x = x0, y = 3, h = taskH(), by;

    for (i = 0; i < nTasks; ++i) n += taskVisible(&tasks[i]);
    if (!n || x1 - x0 < 30) return;
    w = (x1 - x0 - (n - 1) * 4) / n;
    if (w > 200) w = 200;
    if (w < 30) w = 30;
    by = y + baseline(barFont, h);

    for (i = 0; i < nTasks; ++i) {
	Task *t = &tasks[i];
	char s[256];
	int on = t->w == activeWin && !t->iconic;

	if (!taskVisible(t)) continue;
	if (x + w > x1) break;
	if (on) {
	    XCopyArea(dpy, taskGloss, barBack, gc, 0, 0, w, h, x, y);
	    if (glossOn && !strcmp(cfg.taskStyle, "flat"))
		drawBox(barDraw, &cMeter, x, y, w, h);	/* a thin line of the theme colour */
	} else if (!t->iconic) {
	    XftDrawRect(barDraw, hoverKind == H_TASK && hoverArg == i ? &cTaskHover : &cTask,
			x, y, w, h);
	    drawBox(barDraw, &cWellLine, x, y, w, h);
	} else {
	    if (hoverKind == H_TASK && hoverArg == i) XftDrawRect(barDraw, &cTask, x, y, w, h);
	    drawBox(barDraw, &cOff, x, y, w, h);
	}
	snprintf(s, sizeof s, "%s", t->title[0] ? t->title : _("(untitled)"));
	fitText(barFont, s, sizeof s, w - 12);
	drawLabel(barDraw, barFont, on ? &cFg : t->iconic ? &cDim : &cText,
		  x + 6, by, s, on && glossOn);
	addHit(x, x + w, H_TASK, i);
	x += w + 4;
    }
}


/* ------------------------------------------------------------------ */
/* the system tray (freedesktop.org System Tray Protocol, XEmbed)     */

#define MAXTRAY 32
#define SYSTEM_TRAY_REQUEST_DOCK 0
#define XEMBED_EMBEDDED_NOTIFY	 0
#define XEMBED_MAPPED		 (1 << 0)

/* each icon sits in a box of ours with the bar's colour behind it, for
   icons whose background is ParentRelative */
typedef struct { Window icon, box; int x, want, hidden, boxMapped; } TrayIcon;
static TrayIcon tray[MAXTRAY];
static int nTray;
static Window trayWin;		/* owns _NET_SYSTEM_TRAY_S<n>; None: no tray */
static Atom aTraySel, aTrayOpcode, aTrayOrient, aTrayVisual, aTrayIconSize,
    aXembed, aXembedInfo, aManager;

static int traySize(void)
{
    int s = barH - 10;
    return s > 24 ? 24 : s < 16 ? 16 : s;
}

static int trayFind(Window w)
{
    int i;
    for (i = 0; i < nTray; ++i) if (tray[i].icon == w) return i;
    return -1;
}

/* an icon may ask to be hidden through _XEMBED_INFO (version, flags) */
static int trayWanted(Window w)
{
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char *data = 0;
    int want = 1;

    if (XGetWindowProperty(dpy, w, aXembedInfo, 0, 2, False, aXembedInfo, &type,
			   &format, &n, &after, &data) == Success && data) {
	if (format == 32 && n >= 2) want = (((long *)data)[1] & XEMBED_MAPPED) != 0;
	XFree(data);
    }
    return want;
}

/* tray_hide: the icon's WM_CLASS (either half) is in the list. The icon
   of the input method the bar already shows is hidden too (IBus's panel is
   "ibus-ui-gtk", fcitx5's "fcitx"), so there is one indicator, not two */
static int trayHidden(Window w)
{
    XClassHint ch = { 0, 0 };
    char list[sizeof cfg.trayHide + 32], *tok, *save;
    int hide = 0;

    if (!XGetClassHint(dpy, w, &ch)) return 0;
    snprintf(list, sizeof list, "%s %s", cfg.trayHide,
	     imKind == IM_IBUS ? "ibus-ui-gtk" : imKind == IM_FCITX5 ? "fcitx" : "");
    for (tok = strtok_r(list, " \t,", &save); tok && !hide;
	 tok = strtok_r(0, " \t,", &save))
	hide = (ch.res_name && !strcasecmp(tok, ch.res_name)) ||
	       (ch.res_class && !strcasecmp(tok, ch.res_class));
    if (ch.res_name) XFree(ch.res_name);
    if (ch.res_class) XFree(ch.res_class);
    return hide;
}

static void xembedSend(Window w, long message, long detail, long d1, long d2)
{
    XEvent e;

    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = aXembed;
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = message;
    e.xclient.data.l[2] = detail;
    e.xclient.data.l[3] = d1;
    e.xclient.data.l[4] = d2;
    XSendEvent(dpy, w, False, NoEventMask, &e);
}

static void trayStart(void)
{
    char sel[64];
    long orient = 0, size = traySize(), visual = (long)XVisualIDFromVisual(vis);
    XEvent e;

    if (!cfg.tray) return;
    snprintf(sel, sizeof sel, "_NET_SYSTEM_TRAY_S%d", scr);
    aTraySel      = XInternAtom(dpy, sel, False);
    aTrayOpcode   = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
    aTrayOrient   = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    aTrayVisual   = XInternAtom(dpy, "_NET_SYSTEM_TRAY_VISUAL", False);
    aTrayIconSize = XInternAtom(dpy, "_NET_SYSTEM_TRAY_ICON_SIZE", False);
    aXembed       = XInternAtom(dpy, "_XEMBED", False);
    aXembedInfo   = XInternAtom(dpy, "_XEMBED_INFO", False);
    aManager      = XInternAtom(dpy, "MANAGER", False);
    if (XGetSelectionOwner(dpy, aTraySel) != None) {
	fprintf(stderr, PROGNAME ": another system tray is running, not starting one\n");
	return;
    }
    trayWin = XCreateSimpleWindow(dpy, root, -1, -1, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, trayWin, aTrayOrient, XA_CARDINAL, 32, PropModeReplace,
		    (unsigned char *)&orient, 1);
    XChangeProperty(dpy, trayWin, aTrayVisual, XA_VISUALID, 32, PropModeReplace,
		    (unsigned char *)&visual, 1);
    XChangeProperty(dpy, trayWin, aTrayIconSize, XA_CARDINAL, 32, PropModeReplace,
		    (unsigned char *)&size, 1);
    XSetSelectionOwner(dpy, aTraySel, trayWin, CurrentTime);
    if (XGetSelectionOwner(dpy, aTraySel) != trayWin) {
	XDestroyWindow(dpy, trayWin);
	trayWin = None;
	return;
    }
    /* tell the applications waiting for a tray */
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = root;
    e.xclient.message_type = aManager;
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = aTraySel;
    e.xclient.data.l[2] = trayWin;
    XSendEvent(dpy, root, False, StructureNotifyMask, &e);
}

static void trayDock(Window icon)
{
    XSetWindowAttributes a;
    XWindowAttributes wa;
    TrayIcon *t;
    int s = traySize();

    if (!icon || trayFind(icon) >= 0 || nTray >= MAXTRAY) return;
    if (!XGetWindowAttributes(dpy, icon, &wa)) return;	/* already gone */
    t = &tray[nTray++];
    memset(t, 0, sizeof *t);
    t->icon = icon;
    t->x = -1;
    a.background_pixel = cBarBg.pixel;
    t->box = XCreateWindow(dpy, bar, -s, 0, s, s, 0, depth, InputOutput, vis,
			   CWBackPixel, &a);
    XSelectInput(dpy, icon, StructureNotifyMask | PropertyChangeMask);
    XReparentWindow(dpy, icon, t->box, 0, 0);
    XMoveResizeWindow(dpy, icon, 0, 0, s, s);
    xembedSend(icon, XEMBED_EMBEDDED_NOTIFY, 0, t->box, 0);
    t->want = trayWanted(icon);
    t->hidden = trayHidden(icon);
    if (t->want) XMapWindow(dpy, icon);
}

/* tray_hide or the shown input method changed: look at every icon again */
static void trayRefilter(void)
{
    int i;

    for (i = 0; i < nTray; ++i) tray[i].hidden = trayHidden(tray[i].icon);
}

/* drop an icon; give it back to the root window if it is still ours */
static void trayForget(int i, int giveBack)
{
    TrayIcon *t = &tray[i];

    if (giveBack) {
	XSelectInput(dpy, t->icon, NoEventMask);
	XUnmapWindow(dpy, t->icon);
	XReparentWindow(dpy, t->icon, root, 0, 0);
    }
    XDestroyWindow(dpy, t->box);
    memmove(t, t + 1, (nTray - i - 1) * sizeof *t);
    --nTray;
}

/* on exit or reload, or when another tray takes over: the applications
   keep their icons and dock them again in the next tray */
static void trayStop(void)
{
    while (nTray) trayForget(nTray - 1, 1);
    if (trayWin) XDestroyWindow(dpy, trayWin);
    trayWin = None;
    XSync(dpy, False);
}

/* the icons, right to left from x = right; returns the left edge */
static int trayPlace(int right)
{
    int s = traySize(), y = (barH - s) / 2, i;

    for (i = nTray - 1; i >= 0; --i) {
	TrayIcon *t = &tray[i];
	if (!t->want || t->hidden) {
	    if (t->boxMapped) { XUnmapWindow(dpy, t->box); t->boxMapped = 0; }
	    continue;
	}
	right -= s;
	if (t->x != right) { XMoveWindow(dpy, t->box, right, y); t->x = right; }
	if (!t->boxMapped) { XMapWindow(dpy, t->box); t->boxMapped = 1; }
	right -= 6;
    }
    return right + 6;
}

static void drawBar(void);

static int trayEvent(XEvent *e)
{
    int i;

    switch (e->type) {
    case ClientMessage:
	if (!trayWin || e->xclient.window != trayWin) return 0;
	if (e->xclient.message_type == aTrayOpcode &&
	    e->xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
	    trayDock((Window)e->xclient.data.l[2]);
	    drawBar();
	}
	return 1;		/* balloon messages are not shown */
    case SelectionClear:
	if (!trayWin || e->xselectionclear.window != trayWin) return 0;
	trayStop();
	drawBar();
	return 1;
    case DestroyNotify:
	if ((i = trayFind(e->xdestroywindow.window)) < 0) return 0;
	trayForget(i, 0);
	drawBar();
	return 1;
    case ReparentNotify:
	if ((i = trayFind(e->xreparent.window)) < 0) return 0;
	if (e->xreparent.parent != tray[i].box) {	/* taken elsewhere */
	    XSelectInput(dpy, tray[i].icon, NoEventMask);
	    trayForget(i, 0);
	    drawBar();
	}
	return 1;
    case PropertyNotify:
	if ((i = trayFind(e->xproperty.window)) < 0) return 0;
	if (e->xproperty.atom == aXembedInfo) {
	    tray[i].want = trayWanted(tray[i].icon);
	    if (tray[i].want) XMapWindow(dpy, tray[i].icon);
	    else XUnmapWindow(dpy, tray[i].icon);
	    drawBar();
	}
	return 1;
    case ConfigureNotify:	/* icons keep the size they are given */
	if ((i = trayFind(e->xconfigure.window)) < 0) return 0;
	if (e->xconfigure.width != traySize() || e->xconfigure.height != traySize() ||
	    e->xconfigure.x || e->xconfigure.y)
	    XMoveResizeWindow(dpy, tray[i].icon, 0, 0, traySize(), traySize());
	return 1;
    case MapNotify:
    case UnmapNotify:
	return trayFind(e->xany.window) >= 0;
    }
    return 0;
}

static void drawBar(void)
{
    int by = baseline(barFont, barH), i, right;
    int b0 = notchLo() + 2, bs = notchHi() - notchLo() - 3;
    char clock[64];
    int gap = 14;
    time_t now = time(0);

    nHits = 0;
    XCopyArea(dpy, barGloss, barBack, gc, 0, 0, scrW, barH, 0, 0);

    /* the button: wm2tj's tab button, the ■ menu */
    XftDrawRect(barDraw, &cBorder, b0, b0, bs, bs);
    XftDrawRect(barDraw, menuOpen ? &cButtonOn : &cButton,
		b0 + 1, b0 + 1, bs - 2, bs - 2);
    addHit(0, notchHi() + 3, H_MENU, 0);

    /* the label, as on a wm2tj tab */
    if (cfg.tabLabel[0]) {
	char t[128];
	int x = notchHi() + 8;
	snprintf(t, sizeof t, "%s", cfg.tabLabel);
	fitText(barFont, t, sizeof t, cfg.tabLength - x - 4);
	drawLabel(barDraw, barFont, &cFg, x, by, t, glossOn);
    }

    /* right side, built right to left: clock, status icons, desktops */
    strftime(clock, sizeof clock, cfg.clockFormat, localtime(&now));
    right = scrW - 10 - textWidth(barFont, clock);
    drawLabel(barDraw, barFont, &cText, right, by, clock, 0);
    addHit(right - 4, scrW, H_CLOCK, 0);

    if (batPresent) {
	char pct[16];
	int w;
	snprintf(pct, sizeof pct, "%d%%", batCap);
	right -= gap + textWidth(barFont, pct);
	drawLabel(barDraw, barFont, batLow() ? &cWarn : &cText, right, by, pct, 0);
	w = drawBattery(right - 4, 0);		/* measure */
	right -= 4 + w;
	drawBattery(right, 1);
	addHit(right - 4, right + w + 4 + textWidth(barFont, pct) + 4, H_BAT, 0);
    }
    if (volPct >= 0) {
	right -= gap + VOL_W;
	drawVol(right);
	addHit(right - 4, right + VOL_W + 4, H_VOL, 0);
    }
    if (btState >= 0) {
	right -= gap + 10;
	drawBt(right);
	addHit(right - 4, right + 14, H_BT, 0);
    }
    if (imName[0]) {
	int w = imeWidth();
	right -= gap + w;
	drawIme(right);
	addHit(right - 4, right + w + 4, H_IME, 0);
    }
    right -= gap + 16;
    drawNet(right);
    addHit(right - 4, right + 20, H_NET, 0);
    if (nTray) {
	int left = trayPlace(right - gap);
	if (left < right - gap) right = left;
    }
    if (memTotalKb > 0) {
	right -= gap + 16 + 3 + VBAR_W;
	drawMem(right);
	addHit(right - 4, right + 16 + 3 + VBAR_W + 4, H_MEM, 0);
    }
    right -= gap + 13 + 3 + VBAR_W;
    drawCpu(right);
    addHit(right - 4, right + 13 + 3 + VBAR_W + 4, H_CPU, 0);

    /* virtual desktops, numbered boxes; the current one is lit */
    right -= gap + 4;
    for (i = nDesktops - 1; i >= 0; --i) {
	char num[16];
	int tw, w, h = barH - 8, x;
	snprintf(num, sizeof num, "%d", i + 1);
	tw = textWidth(barFont, num);
	w = tw + 10 > h ? tw + 10 : h;
	x = right - w;
	if (x < tabLen() + 4) break;
	if (i == curDesktop) {
	    XftDrawRect(barDraw, &cButtonOn, x, 4, w, h);
	    drawLabel(barDraw, barFont, &cFg, x + (w - tw) / 2, by, num, glossOn);
	} else {
	    if (hoverKind == H_DESKTOP && hoverArg == i)
		XftDrawRect(barDraw, &cHover, x, 4, w, h);
	    drawBox(barDraw, &cDeskLine, x, 4, w, h);
	    drawLabel(barDraw, barFont, &cDim, x + (w - tw) / 2, by, num, 0);
	}
	addHit(x - 2, x + w + 2, H_DESKTOP, i);
	right = x - 4;
    }

    drawTasks(tabLen() + 6, right - gap);

    XCopyArea(dpy, barBack, bar, gc, 0, 0, scrW, barH, 0, 0);
    if (flashKind != H_NONE || hoverKind >= H_CPU) showTip(); else hideTip();
}

static void setStruts(void)
{
    long strut[4] = { 0, 0, barH, 0 };
    long partial[12] = { 0, 0, barH, 0, 0, 0, 0, 0, 0, scrW - 1, 0, 0 };

    XChangeProperty(dpy, bar, aStrutPartial, XA_CARDINAL, 32, PropModeReplace,
		    (unsigned char *)partial, 12);
    XChangeProperty(dpy, bar, aStrut, XA_CARDINAL, 32, PropModeReplace,
		    (unsigned char *)strut, 4);
}

static void makeBarBuffers(void)
{
    if (barDraw) XftDrawDestroy(barDraw);
    if (barBack) XFreePixmap(dpy, barBack);
    if (barGloss) XFreePixmap(dpy, barGloss);
    barGloss = XCreatePixmap(dpy, root, scrW, barH, depth);
    XSetForeground(dpy, gc, cBarBg.pixel);
    XFillRectangle(dpy, barGloss, gc, 0, 0, scrW, barH);
    renderTab(barGloss);
    barBack = XCreatePixmap(dpy, root, scrW, barH, depth);
    barDraw = XftDrawCreate(dpy, barBack, vis, cmap);
    if (!taskGloss) {
	taskGloss = renderGloss(200, taskH(), taskBase);
	if (glossOn && !strcmp(cfg.taskStyle, "flat")) {	/* one colour, no gloss */
	    XSetForeground(dpy, gc, packPixel(taskBase));
	    XFillRectangle(dpy, taskGloss, gc, 0, 0, 200, taskH());
	}
    }
}

static void createBar(void)
{
    XSetWindowAttributes a;
    XClassHint ch = { PROGNAME, PROGNAME };
    XWMHints wmh;
    XSizeHints sh;
    long desk = 0xFFFFFFFF;
    Atom states[4] = { aSticky, aAbove, aSkipTaskbar, aSkipPager };

    a.background_pixmap = None;
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
	PointerMotionMask | LeaveWindowMask;
    bar = XCreateWindow(dpy, root, 0, 0, scrW, barH, 0, depth, InputOutput,
			vis, CWBackPixmap | CWEventMask, &a);

    XChangeProperty(dpy, bar, aType, XA_ATOM, 32, PropModeReplace,
		    (unsigned char *)&aDock, 1);
    XChangeProperty(dpy, bar, aWmState, XA_ATOM, 32, PropModeReplace,
		    (unsigned char *)states, 4);
    XChangeProperty(dpy, bar, aWmDesktop, XA_CARDINAL, 32, PropModeReplace,
		    (unsigned char *)&desk, 1);
    XChangeProperty(dpy, bar, aNetWmName, aUtf8, 8, PropModeReplace,
		    (unsigned char *)PROGNAME, strlen(PROGNAME));
    XStoreName(dpy, bar, PROGNAME);
    XSetClassHint(dpy, bar, &ch);
    wmh.flags = InputHint;
    wmh.input = False;
    XSetWMHints(dpy, bar, &wmh);
    sh.flags = PPosition | PSize | PMinSize | PMaxSize;
    sh.x = sh.y = 0;
    sh.width = sh.min_width = sh.max_width = scrW;
    sh.height = sh.min_height = sh.max_height = barH;
    XSetWMNormalHints(dpy, bar, &sh);
    setStruts();

    makeBarBuffers();
    shapeBar();
    XMapRaised(dpy, bar);
}

static void readDesktops(void)
{
    nDesktops = (int)getCardinal(root, aNumDesktops, 0);
    curDesktop = (int)getCardinal(root, aCurDesktop, 0);
    if (nDesktops < 0 || nDesktops > 32) nDesktops = 0;
}

static void readActive(void)
{
    activeWin = (Window)getCardinal(root, aActive, 0);
}

static int skipTaskbar(Window w)
{
    Atom type;
    int format, skip = 0;
    unsigned long n, after, i;
    unsigned char *data = 0;

    if (XGetWindowProperty(dpy, w, aWmState, 0, 32, False, XA_ATOM, &type,
			   &format, &n, &after, &data) == Success && data) {
	for (i = 0; i < n; ++i) if (((Atom *)data)[i] == aSkipTaskbar) skip = 1;
	XFree(data);
    }
    return skip;
}

static void readTask(Task *t)
{
    getWindowTitle(t->w, t->title, sizeof t->title);
    t->desktop = getCardinal(t->w, aWmDesktop, 0) & 0xFFFFFFFFL;
    t->iconic = getCardinal(t->w, aIcccmState, 1) == IconicState;
}

static void readTasks(void)
{
    Atom type;
    int format;
    unsigned long n = 0, after, i;
    unsigned char *data = 0;
    Task old[MAXTASKS];
    int nOld = nTasks, j;

    memcpy(old, tasks, sizeof old);
    nTasks = 0;
    if (XGetWindowProperty(dpy, root, aClientList, 0, MAXTASKS, False, XA_WINDOW,
			   &type, &format, &n, &after, &data) != Success || !data)
	return;
    for (i = 0; i < n && nTasks < MAXTASKS; ++i) {
	Window w = ((Window *)data)[i];
	int known = 0;
	if (w == bar || skipTaskbar(w)) continue;
	for (j = 0; j < nOld; ++j) if (old[j].w == w) known = 1;
	if (!known) XSelectInput(dpy, w, PropertyChangeMask);
	tasks[nTasks].w = w;
	readTask(&tasks[nTasks]);
	++nTasks;
    }
    XFree(data);
}

static Task *taskOf(Window w)
{
    int i;
    for (i = 0; i < nTasks; ++i) if (tasks[i].w == w) return &tasks[i];
    return 0;
}

static void sendToRoot(Window w, Atom type, long d0, long d1)
{
    XEvent e;

    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = type;
    e.xclient.format = 32;
    e.xclient.data.l[0] = d0;
    e.xclient.data.l[1] = d1;
    XSendEvent(dpy, root, False,
	       SubstructureRedirectMask | SubstructureNotifyMask, &e);
}

/* click on a task: bring it up, or hide it if it is already in front */
static void clickTask(int i)
{
    Task *t;

    if (i < 0 || i >= nTasks) return;
    t = &tasks[i];
    if (t->w == activeWin && !t->iconic)
	sendToRoot(t->w, aChangeState, IconicState, 0);
    else
	sendToRoot(t->w, aActive, 2, CurrentTime);	/* 2: from a pager */
}

static void switchDesktop(int d)
{
    XEvent e;

    if (d < 0 || d >= nDesktops || d == curDesktop) return;
    memset(&e, 0, sizeof e);
    e.xclient.type = ClientMessage;
    e.xclient.window = root;
    e.xclient.message_type = aCurDesktop;
    e.xclient.format = 32;
    e.xclient.data.l[0] = d;
    e.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, root, False,
	       SubstructureRedirectMask | SubstructureNotifyMask, &e);
}


/* ------------------------------------------------------------------ */
/* Wi-Fi networks (NetworkManager, through nmcli)                     */

typedef struct { char ssid[64]; int signal, secure, inUse, known; } Ap;
#define MAXAPS 24
static Ap aps[MAXAPS];
static int nAps;
static int wifiRadio = -1;	/* -1: no nmcli, -2: NetworkManager not running, 0: off, 1: on */
static time_t wifiRefreshAt;	/* reload the open menu after a rescan */

/* split a line of "nmcli -t" output; ':' and '\' inside a field are escaped */
static int nmFields(char *line, char **f, int max)
{
    char *r = line, *w = line;
    int n = 0;

    f[n++] = w;
    for (; *r && *r != '\n'; ++r) {
	if (*r == '\\' && r[1]) *w++ = *++r;
	else if (*r == ':' && n < max) { *w++ = '\0'; f[n++] = w; }
	else *w++ = *r;
    }
    *w = '\0';
    return n;
}

/* the SSIDs of the saved Wi-Fi connections */
static StrList knownSsids;

static void loadKnownSsids(void)
{
    FILE *p;
    char line[512], cmd[4096], *f[2];
    size_t len;
    int n = 0;

    strListFree(&knownSsids);
    len = (size_t)snprintf(cmd, sizeof cmd, "nmcli -g 802-11-wireless.ssid con show");
    if (!(p = popen("nmcli -t -f UUID,TYPE con show 2>/dev/null", "r"))) return;
    while (fgets(line, sizeof line, p)) {
	if (nmFields(line, f, 2) < 2 || strcmp(f[1], "802-11-wireless")) continue;
	if (len + strlen(f[0]) + 24 < sizeof cmd) {	/* UUIDs need no quoting */
	    len += (size_t)snprintf(cmd + len, sizeof cmd - len, " uuid %s", f[0]);
	    ++n;
	}
    }
    pclose(p);
    if (!n) return;					/* no saved networks */
    strcat(cmd, " 2>/dev/null");
    if (!(p = popen(cmd, "r"))) return;
    while (fgets(line, sizeof line, p))
	if (nmFields(line, f, 1) && f[0][0]) strListAdd(&knownSsids, f[0]);
    pclose(p);
}

static int apCmp(const void *x, const void *y)
{
    const Ap *a = x, *b = y;
    if (a->inUse != b->inUse) return b->inUse - a->inUse;
    return b->signal - a->signal;
}

/* the networks NetworkManager last saw, one per name, the strongest */
static void loadAps(void)
{
    FILE *p;
    char line[512], *f[4];
    int i;

    nAps = 0;
    wifiRadio = -1;
    if (!inPath("nmcli")) return;
    if ((p = popen("nmcli -t radio wifi 2>/dev/null", "r"))) {
	/* nmcli without a running NetworkManager (iwd/ConnMan/dhcpcd setups that
	   still have the package) prints an error instead */
	if (fgets(line, sizeof line, p))
	    wifiRadio = !strncmp(line, "enabled", 7) ? 1 : !strncmp(line, "disabled", 8) ? 0 : -2;
	pclose(p);
    }
    if (wifiRadio != 1) return;
    p = popen("nmcli -t -f IN-USE,SIGNAL,SECURITY,SSID dev wifi list --rescan no"
	      " 2>/dev/null", "r");
    if (!p) return;
    while (fgets(line, sizeof line, p)) {
	Ap *a = 0;

	if (nmFields(line, f, 4) < 4 || !f[3][0]) continue;	/* hidden */
	for (i = 0; i < nAps && !a; ++i) if (!strcmp(aps[i].ssid, f[3])) a = &aps[i];
	if (!a) {
	    if (nAps >= MAXAPS) continue;
	    a = &aps[nAps++];
	    memset(a, 0, sizeof *a);
	    snprintf(a->ssid, sizeof a->ssid, "%s", f[3]);
	    a->signal = -1;
	}
	if (f[0][0] == '*') a->inUse = 1;
	if (atoi(f[1]) > a->signal) a->signal = atoi(f[1]);
	if (f[2][0] && strcmp(f[2], "--")) a->secure = 1;
    }
    pclose(p);
    loadKnownSsids();
    for (i = 0; i < nAps; ++i) aps[i].known = strListHas(&knownSsids, aps[i].ssid);
    qsort(aps, nAps, sizeof *aps, apCmp);
}

/*
 * Run a connecting command (nmcli, bluetoothctl) in the background. Input,
 * such as a password, goes in through a pipe (never on the command line,
 * where ps would show it). A helper process waits for the command and
 * sends back its exit status on connFd, which the main loop watches.
 */
enum { JOB_WIFI, JOB_WIFI_SECURE, JOB_BT };
static int connFd = -1;
static char connName[64];	/* the network or device, for the message */
static int connKind;

static void runJob(char *const argv[], const char *input, const char *name, int kind)
{
    int in[2], st[2];
    pid_t pid;

    if (connFd >= 0) { close(connFd); connFd = -1; }
    if (pipe2(in, O_CLOEXEC) < 0) return;
    if (pipe2(st, O_CLOEXEC) < 0) { close(in[0]); close(in[1]); return; }
    pid = fork();
    if (pid == 0) {
	struct sigaction sa;
	unsigned char r = 255;
	int status;
	pid_t c;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sa, 0);
	sigaction(SIGPIPE, &sa, 0);
	sigaction(SIGHUP, &sa, 0);
	sigaction(SIGTERM, &sa, 0);
	setsid();
	close(in[1]);
	if ((c = fork()) == 0) {
	    dup2(in[0], 0);
	    execvp(argv[0], argv);
	    _exit(127);
	}
	close(in[0]);
	if (c > 0 && waitpid(c, &status, 0) == c && WIFEXITED(status))
	    r = (unsigned char)WEXITSTATUS(status);
	if (write(st[1], &r, 1) < 0) _exit(1);
	_exit(0);
    }
    close(in[0]);
    close(st[1]);
    if (pid < 0) {
	close(in[1]);
	close(st[0]);
	return;
    }
    if (input) {
	if (write(in[1], input, strlen(input)) < 0 || write(in[1], "\n", 1) < 0)
	    fprintf(stderr, PROGNAME ": %s: %s\n", argv[0], strerror(errno));
    }
    close(in[1]);
    connFd = st[0];
    snprintf(connName, sizeof connName, "%s", name);
    connKind = kind;
}

/* with a password, nmcli --ask reads it from standard input */
static void startConnect(const char *ssid, const char *password, int secure)
{
    char *argvAsk[] = { "nmcli", "--ask", "dev", "wifi", "connect", (char *)ssid, 0 };
    char *argvPlain[] = { "nmcli", "dev", "wifi", "connect", (char *)ssid, 0 };

    runJob(password ? argvAsk : argvPlain, password, ssid,
	   secure ? JOB_WIFI_SECURE : JOB_WIFI);
}


/* ------------------------------------------------------------------ */
/* Bluetooth devices (BlueZ, through bluetoothctl)                    */

typedef struct { char addr[18]; char name[64]; int connected; } BtDevice;
#define MAXBTDEVS 24
static BtDevice btDevs[MAXBTDEVS];
static int nBtDevs, haveBluetoothctl;

static int isBtAddr(const char *a)
{
    int i;
    for (i = 0; i < 17; ++i)
	if (i % 3 == 2 ? a[i] != ':' : !isxdigit((unsigned char)a[i])) return 0;
    return a[17] == '\0' || a[17] == ' ';
}

/* "Device AA:BB:CC:DD:EE:FF Name" lines; bluetoothctl waits forever
   without bluetoothd, hence the timeout */
static void btList(const char *which, int connected)
{
    FILE *p;
    char cmd[256], line[256];
    int i;

    /* "devices Paired|Connected" needs BlueZ 5.66 (2022); older ones
       (Debian 11, Ubuntu 22.04, RHEL 9.0-9.2) have paired-devices instead */
    snprintf(cmd, sizeof cmd, "timeout 3 bluetoothctl devices %s 2>/dev/null || "
	     "{ [ %s = Paired ] && timeout 3 bluetoothctl paired-devices 2>/dev/null; }", which, which);
    if (!(p = popen(cmd, "r"))) return;
    while (fgets(line, sizeof line, p)) {
	char *a = line + 7;
	BtDevice *d = 0;

	line[strcspn(line, "\r\n")] = '\0';
	if (strncmp(line, "Device ", 7) || !isBtAddr(a)) continue;
	a[17] = '\0';
	for (i = 0; i < nBtDevs && !d; ++i) if (!strcmp(btDevs[i].addr, a)) d = &btDevs[i];
	if (!d) {
	    if (connected || nBtDevs >= MAXBTDEVS) continue;	/* paired only */
	    d = &btDevs[nBtDevs++];
	    memset(d, 0, sizeof *d);
	    memcpy(d->addr, a, 18);
	    snprintf(d->name, sizeof d->name, "%.63s", a[18] ? a + 18 : a);
	}
	if (connected) d->connected = 1;
    }
    pclose(p);
}

static void loadBtDevices(void)
{
    nBtDevs = 0;
    haveBluetoothctl = inPath("bluetoothctl");
    if (!haveBluetoothctl || btState <= 0) return;
    btList("Paired", 0);
    btList("Connected", 1);
}

static void toggleBtDevice(const BtDevice *d)
{
    char *argv[] = { "bluetoothctl", d->connected ? "disconnect" : "connect",
		     (char *)d->addr, 0 };
    runJob(argv, 0, d->name, JOB_BT);
}


/* ------------------------------------------------------------------ */
/* menus                                                              */

enum { IT_APP, IT_SUB, IT_ACT, IT_SEP, IT_LABEL, IT_WIFI, IT_ENTRY, IT_BTDEV, IT_SINK,
       IT_CMD, IT_SLIDER, IT_PROFILE, IT_IME };
enum { M_ROOT, M_APPS, M_CAT, M_SESSION, M_CONTEXT, M_CONFIRM, M_WIFI,
       M_PASSWORD, M_NOTICE, M_BT, M_SOUND, M_BATTERY, M_IME };
enum { A_LOCK, A_LOGOUT, A_SUSPEND, A_ASK_REBOOT, A_ASK_POWEROFF,
       A_REBOOT, A_POWEROFF, A_CANCEL, A_TOGGLE_FAV,
       A_WIFI_ON, A_WIFI_OFF, A_WIFI_DISCONNECT, A_WIFI_SETTINGS,
       A_PW_CONNECT, A_PW_SHOW, A_BT_ON, A_BT_OFF, A_BT_SETTINGS,
       A_MUTE, A_SOUND_SETTINGS, A_BAR_SETTINGS, A_IME_SETTINGS };

/* the Wi-Fi password dialog */
static char pwSsid[64], pwText[128];
static int pwShow, pwFailed;

typedef struct {
    int kind;
    char *label;
    int app;			/* IT_APP, A_TOGGLE_FAV: index into apps */
    int subType, subArg;	/* IT_SUB */
    int act;			/* IT_ACT */
    int ap;			/* IT_WIFI: index into aps, IT_BTDEV: into btDevs,
				   IT_SINK: into sinks, IT_CMD: into favs,
				   IT_PROFILE: into profiles, IT_IME: into imes,
				   IT_SLIDER: SL_BRIGHTNESS or SL_VOLUME */
    int x, y, h;		/* inside the menu window */
} Item;

typedef struct {
    int type, arg;
    Item *items;
    int n, sel;
    int x, y, w, h, colW;
    Window win;
    Pixmap back, selGloss;
    XftDraw *draw;
} Menu;

#define MAXMENUS 8
static Menu menus[MAXMENUS];
static int itemH, sepH = 7;
static int grabbed;

static void addItem(Menu *m, int kind, const char *label)
{
    Item *it;
    m->items = realloc(m->items, (m->n + 1) * sizeof *m->items);
    it = &m->items[m->n++];
    memset(it, 0, sizeof *it);
    it->kind = kind;
    it->label = strdup(label ? label : "");
    it->app = -1;
}

static void addAction(Menu *m, const char *label, int act)
{
    addItem(m, IT_ACT, label);
    m->items[m->n - 1].act = act;
}

static void addSub(Menu *m, const char *label, int type, int arg)
{
    addItem(m, IT_SUB, label);
    m->items[m->n - 1].subType = type;
    m->items[m->n - 1].subArg = arg;
}

static void addApp(Menu *m, int app)
{
    addItem(m, IT_APP, apps[app].name);
    m->items[m->n - 1].app = app;
}

static int findApp(const char *id)
{
    int i;
    for (i = 0; i < nApps; ++i) if (!strcmp(apps[i].id, id)) return i;
    return -1;
}

static int isFav(int app)
{
    return app >= 0 && strListHas(&favs, apps[app].id);
}

/* power-profiles-daemon's profiles, for the battery menu */
#define MAXPROFILES 4
#define SLIDER_W 150		/* a slider's track */
enum { SL_BRIGHTNESS, SL_VOLUME };	/* what a slider sets (its ap) */
static struct { char name[32]; int active; } profiles[MAXPROFILES];
static int nProfiles;
static int haveBusctl, havePowerprofilesctl;
static int brightnessPct(void);
static void setBrightnessPct(int pct);

static const char *profileLabel(const char *name)
{
    if (!strcmp(name, "power-saver")) return _("Power saver");
    if (!strcmp(name, "balanced")) return _("Balanced");
    if (!strcmp(name, "performance")) return _("Performance");
    return name;
}

/* powerprofilesctl list: "* balanced:" for the active one, then details */
static void loadProfiles(void)
{
    FILE *p;
    char line[256], *s, *c;

    nProfiles = 0;
    if (!havePowerprofilesctl) return;
    if (!(p = popen("LC_ALL=C powerprofilesctl list 2>/dev/null", "r"))) return;
    while (fgets(line, sizeof line, p)) {
	int active = line[0] == '*';
	s = line + (active ? 1 : 0);
	while (*s == ' ') ++s;
	if (s - line > 2 || !(c = strchr(s, ':')) || c[1] != '\n') continue;
	*c = '\0';
	if (nProfiles < MAXPROFILES) {
	    snprintf(profiles[nProfiles].name, sizeof profiles[0].name, "%s", s);
	    profiles[nProfiles++].active = active;
	}
    }
    pclose(p);
}

static void buildItems(Menu *m)
{
    int i, n = 0;

    switch (m->type) {
    case M_ROOT:
	for (i = 0; i < favs.n; ++i) {
	    char name[256];
	    int a = findApp(favs.ids[i]);
	    if (a >= 0) { addApp(m, a); ++n; }
	    else if (favCommand(favs.ids[i], name, sizeof name)) {
		addItem(m, IT_CMD, name);
		m->items[m->n - 1].ap = i;
		++n;
	    }
	}
	if (!n) addItem(m, IT_LABEL, _("Favourites: right-click an app to add it"));
	addItem(m, IT_SEP, 0);
	addSub(m, _("Applications"), M_APPS, 0);
	addSub(m, _("Session"), M_SESSION, 0);
	if (inPath(SETTINGS_PROG)) {
	    addItem(m, IT_SEP, 0);
	    addAction(m, _("Bar settings…"), A_BAR_SETTINGS);
	}
	break;
    case M_APPS:
	for (i = 0; i < NCATS; ++i) {
	    int j, used = 0;
	    for (j = 0; j < nApps && !used; ++j) used = apps[j].cat == i;
	    if (used) addSub(m, _(cats[i].label), M_CAT, i);
	}
	if (!m->n) addItem(m, IT_LABEL, _("No applications found"));
	break;
    case M_CAT:
	for (i = 0; i < nApps; ++i) if (apps[i].cat == m->arg) addApp(m, i);
	break;
    case M_SESSION:
	addAction(m, _("Lock"), A_LOCK);
	addAction(m, _("Log out"), A_LOGOUT);
	addAction(m, _("Suspend"), A_SUSPEND);
	addItem(m, IT_SEP, 0);
	addAction(m, _("Restart…"), A_ASK_REBOOT);
	addAction(m, _("Shut down…"), A_ASK_POWEROFF);
	break;
    case M_CONTEXT:
	addAction(m, isFav(m->arg) ? _("Remove from favourites") : _("Add to favourites"),
		  A_TOGGLE_FAV);
	m->items[0].app = m->arg;
	break;
    case M_CONFIRM:
	addItem(m, IT_LABEL, m->arg == A_REBOOT ? _("Restart now?")
					       : _("Shut down now?"));
	addAction(m, m->arg == A_REBOOT ? _("Restart") : _("Shut down"), m->arg);
	addAction(m, _("Cancel"), A_CANCEL);
	break;
    case M_WIFI:
	if (wifiRadio == -1) {
	    addItem(m, IT_LABEL, _("nmcli (NetworkManager) not found"));
	    break;
	}
	if (wifiRadio == -2) {
	    addItem(m, IT_LABEL, _("NetworkManager is not running"));
	} else if (wifiRadio == 0) {
	    addAction(m, _("Turn Wi-Fi on"), A_WIFI_ON);
	} else {
	    for (i = 0; i < nAps; ++i) {
		addItem(m, IT_WIFI, aps[i].ssid);
		m->items[m->n - 1].ap = i;
		if (aps[i].inUse) ++n;
	    }
	    if (!nAps) addItem(m, IT_LABEL, _("Searching…"));
	    addItem(m, IT_SEP, 0);
	    if (n) addAction(m, _("Disconnect"), A_WIFI_DISCONNECT);
	    addAction(m, _("Turn Wi-Fi off"), A_WIFI_OFF);
	}
	if (cfg.wifiSettingsCmd[0]) addAction(m, _("Network settings…"), A_WIFI_SETTINGS);
	break;
    case M_BT:
	if (!haveBluetoothctl) {
	    addItem(m, IT_LABEL, _("bluetoothctl (BlueZ) not found"));
	} else if (btState <= 0) {
	    addAction(m, _("Turn Bluetooth on"), A_BT_ON);
	} else {
	    for (i = 0; i < nBtDevs; ++i) {
		addItem(m, IT_BTDEV, btDevs[i].name);
		m->items[m->n - 1].ap = i;
	    }
	    if (!nBtDevs) addItem(m, IT_LABEL, _("No paired devices"));
	    addItem(m, IT_SEP, 0);
	    addAction(m, _("Turn Bluetooth off"), A_BT_OFF);
	}
	if (cfg.btSettingsCmd[0]) addAction(m, _("Bluetooth settings…"), A_BT_SETTINGS);
	break;
    case M_SOUND: {
	addItem(m, IT_SLIDER, _("Volume"));
	m->items[m->n - 1].ap = SL_VOLUME;
	addItem(m, IT_SEP, 0);
	for (i = 0; i < nSinks; ++i) {
	    addItem(m, IT_SINK, sinks[i].name);
	    m->items[m->n - 1].ap = i;
	}
	if (nSinks) addItem(m, IT_SEP, 0);
	addAction(m, volMuted ? _("Unmute") : _("Mute"), A_MUTE);
	if (cfg.soundSettingsCmd[0]) addAction(m, _("Sound settings…"), A_SOUND_SETTINGS);
	break;
    }
    case M_IME:
	for (i = 0; i < nImes; ++i) {
	    addItem(m, IT_IME, imes[i].label);
	    m->items[m->n - 1].ap = i;
	}
	if (imKind == IM_IBUS && cfg.imeSettingsCmd[0]) {
	    if (m->n) addItem(m, IT_SEP, 0);
	    addAction(m, _("Input method settings…"), A_IME_SETTINGS);
	}
	break;
    case M_BATTERY: {
	char buf[128];
	if (batPresent) {
	    snprintf(buf, sizeof buf, _("Battery %d%% %s"), batCap,
		     batCharging ? _("charging") : batFull ? _("plugged in") : _("on battery"));
	    addItem(m, IT_LABEL, buf);
	}
	if (brightnessPct() >= 0) {
	    if (m->n) addItem(m, IT_SEP, 0);
	    addItem(m, IT_SLIDER, _("Brightness"));
	    m->items[m->n - 1].ap = SL_BRIGHTNESS;
	}
	if (nProfiles) {
	    if (m->n) addItem(m, IT_SEP, 0);
	    addItem(m, IT_LABEL, _("Power mode"));
	    for (i = 0; i < nProfiles; ++i) {
		addItem(m, IT_PROFILE, profileLabel(profiles[i].name));
		m->items[m->n - 1].ap = i;
	    }
	}
	break;
    }
    case M_PASSWORD: {
	char buf[200];
	if (pwFailed) addItem(m, IT_LABEL, _("Wrong password, or the network did not answer."));
	snprintf(buf, sizeof buf, _("Password for “%s”"), pwSsid);
	addItem(m, IT_LABEL, buf);
	addItem(m, IT_ENTRY, "");
	addAction(m, pwShow ? _("Hide password") : _("Show password"), A_PW_SHOW);
	addItem(m, IT_SEP, 0);
	addAction(m, _("Connect"), A_PW_CONNECT);
	addAction(m, _("Cancel"), A_CANCEL);
	break;
    }
    case M_NOTICE: {
	char buf[200];
	snprintf(buf, sizeof buf, _("Could not connect to “%s”."), connName);
	addItem(m, IT_LABEL, buf);
	addAction(m, _("OK"), A_CANCEL);
	break;
    }
    }
}

static void freeItems(Menu *m)
{
    int i;
    for (i = 0; i < m->n; ++i) free(m->items[i].label);
    free(m->items);
    m->items = 0;
    m->n = 0;
}

static int selectable(const Item *it)
{
    return it->kind == IT_APP || it->kind == IT_CMD || it->kind == IT_SUB || it->kind == IT_ACT ||
	it->kind == IT_WIFI || it->kind == IT_BTDEV || it->kind == IT_SINK ||
	it->kind == IT_SLIDER || it->kind == IT_PROFILE || it->kind == IT_IME;
}

/*
 * Lay the items out in one column, or in several of equal height when
 * they do not fit on the screen (a big category).
 */
static void layoutMenu(Menu *m)
{
    int i, maxW = 0, total = 0, rows, cols, arrow = textWidth(menuFont, "▶");

    for (i = 0; i < m->n; ++i) {
	Item *it = &m->items[i];
	int w = textWidth(menuFont, it->label) + 24;
	if (it->kind == IT_SUB) w += arrow + 16;
	if (it->kind == IT_WIFI) w += textWidth(menuFont, "✓ ") + 40;
	if (it->kind == IT_BTDEV || it->kind == IT_SINK || it->kind == IT_PROFILE ||
	    it->kind == IT_IME)
	    w += textWidth(menuFont, "✓ ");
	if (it->kind == IT_SLIDER) w += SLIDER_W + textWidth(menuFont, "100%") + 24;
	if (it->kind == IT_ENTRY) w = 320;
	if (w > maxW) maxW = w;
	total += it->kind == IT_SEP ? sepH : itemH;
    }
    if (maxW < 140) maxW = 140;
    m->colW = maxW;

    if (total + 2 <= scrH) {
	int y = 1;
	for (i = 0; i < m->n; ++i) {
	    m->items[i].x = 1;
	    m->items[i].y = y;
	    m->items[i].h = m->items[i].kind == IT_SEP ? sepH : itemH;
	    y += m->items[i].h;
	}
	m->w = maxW + 2;
	m->h = y + 1;
	return;
    }
    rows = (scrH - 2) / itemH;
    if (rows < 1) rows = 1;
    cols = (m->n + rows - 1) / rows;
    for (i = 0; i < m->n; ++i) {
	m->items[i].x = 1 + (i / rows) * maxW;
	m->items[i].y = 1 + (i % rows) * itemH;
	m->items[i].h = itemH;
    }
    m->w = cols * maxW + 2;
    m->h = (m->n < rows ? m->n : rows) * itemH + 2;
}

/* a Wi-Fi row: a tick for the network in use, a padlock, four bars */
static void drawApItem(Menu *m, Item *it, XftColor *fg, int by, int on)
{
    XftDraw *d = m->draw;
    const Ap *a = &aps[it->ap];
    int check = textWidth(menuFont, "✓ "), right = it->x + m->colW - 10;
    int bottom = it->y + it->h / 2 + 6, i, lit;

    if (a->inUse) drawLabel(d, menuFont, fg, it->x + 12, by, "✓", on && glossOn);
    drawLabel(d, menuFont, fg, it->x + 12 + check, by, it->label, on && glossOn);

    lit = a->signal >= 75 ? 4 : a->signal >= 50 ? 3 : a->signal >= 25 ? 2 : 1;
    for (i = 0; i < 4; ++i) {
	int h = 3 * (i + 1);
	XftDrawRect(d, i < lit ? fg : &cMenuLine, right - 15 + i * 4, bottom - h, 3, h);
    }
    if (a->secure) {
	int x = right - 28, y = bottom - 6;
	XftDrawRect(d, fg, x, y, 8, 6);			/* body */
	XftDrawRect(d, fg, x + 1, y - 4, 1, 4);		/* shackle */
	XftDrawRect(d, fg, x + 6, y - 4, 1, 4);
	XftDrawRect(d, fg, x + 2, y - 5, 4, 1);
    }
}

/* a slider row: the label, a track from x0 to x1 with a knob, the % */
static void sliderTrack(Menu *m, Item *it, int *x0, int *x1)
{
    *x1 = it->x + m->colW - 12 - textWidth(menuFont, "100%") - 10;
    *x0 = *x1 - SLIDER_W;
}

/* in %, -1: none */
static int sliderPct(const Item *it)
{
    return it->ap == SL_VOLUME ? (sndTool ? volPct : -1) : brightnessPct();
}

static void drawSlider(Menu *m, Item *it, XftColor *fg, int by, int on)
{
    XftDraw *d = m->draw;
    XftColor *knob = it->ap == SL_VOLUME && volMuted ? &cMenuDim : fg;	/* grey: muted */
    int x0, x1, kx, mid = it->y + it->h / 2, pct = sliderPct(it);
    char num[16];

    if (pct < 0) pct = 0;
    sliderTrack(m, it, &x0, &x1);
    drawLabel(d, menuFont, fg, it->x + 12, by, it->label, on && glossOn);
    kx = x0 + (x1 - x0) * (pct > 100 ? 100 : pct) / 100;
    XftDrawRect(d, &cMenuLine, x0, mid - 2, x1 - x0, 4);
    XftDrawRect(d, knob, x0, mid - 2, kx - x0, 4);
    XftDrawRect(d, knob, kx - 3, mid - 7, 7, 14);
    snprintf(num, sizeof num, "%d%%", pct);
    drawLabel(d, menuFont, fg, it->x + m->colW - 12 - textWidth(menuFont, num), by, num,
	      on && glossOn);
}

/* the password box: dots (or the text), the end that fits, a caret */
static void drawEntry(Menu *m, Item *it)
{
    XftDraw *d = m->draw;
    char shown[512], *p = shown;
    int x = it->x + 12, y = it->y + 3, w = m->colW - 24, h = it->h - 6, tw;
    size_t i, n = strlen(pwText);

    XftDrawRect(d, &cEntryBg, x, y, w, h);
    drawBox(d, &cMenuDim, x, y, w, h);
    if (pwShow) {
	snprintf(shown, sizeof shown, "%s", pwText);
    } else {
	shown[0] = '\0';
	for (i = 0; i < n && strlen(shown) + 4 < sizeof shown; ++i) strcat(shown, "•");
    }
    /* drop characters from the front until the rest fits */
    while (*p && (tw = textWidth(menuFont, p)) > w - 12) {
	do ++p; while ((*p & 0xc0) == 0x80);
    }
    tw = *p ? textWidth(menuFont, p) : 0;
    drawLabel(d, menuFont, &cMenuFg, x + 5, it->y + baseline(menuFont, it->h), p, 0);
    XftDrawRect(d, &cMenuFg, x + 6 + tw, y + 4, 1, h - 8);
}

static void drawMenu(Menu *m)
{
    XftDraw *d = m->draw;
    int i, arrowW = textWidth(menuFont, "▶");

    XftDrawRect(d, &cMenuBg, 0, 0, m->w, m->h);
    drawBox(d, &cMenuBorder, 0, 0, m->w, m->h);

    for (i = 0; i < m->n; ++i) {
	Item *it = &m->items[i];
	int by = it->y + baseline(menuFont, it->h);
	int on = i == m->sel && selectable(it);
	XftColor *fg = on ? &cFg : it->kind == IT_LABEL ? &cMenuDim : &cMenuFg;

	if (it->kind == IT_SEP) {
	    XftDrawRect(d, &cMenuLine, it->x + 6, it->y + it->h / 2, m->colW - 12, 1);
	    continue;
	}
	if (on)
	    XCopyArea(dpy, m->selGloss, m->back, gc, 0, 0, m->colW, it->h, it->x, it->y);
	if (it->kind == IT_WIFI) {
	    drawApItem(m, it, fg, by, on);
	    continue;
	}
	if (it->kind == IT_ENTRY) {
	    drawEntry(m, it);
	    continue;
	}
	if (it->kind == IT_SLIDER) {
	    drawSlider(m, it, fg, by, on);
	    continue;
	}
	if (it->kind == IT_BTDEV || it->kind == IT_SINK || it->kind == IT_PROFILE ||
	    it->kind == IT_IME) {
	    int check = textWidth(menuFont, "✓ ");	/* a tick: connected, the default, */
	    if (it->kind == IT_BTDEV ? btDevs[it->ap].connected :	/* the one in use */
		it->kind == IT_SINK ? sinks[it->ap].isDefault :
		it->kind == IT_IME ? imes[it->ap].current : profiles[it->ap].active)
		drawLabel(d, menuFont, fg, it->x + 12, by, "✓", on && glossOn);
	    drawLabel(d, menuFont, fg, it->x + 12 + check, by, it->label, on && glossOn);
	    continue;
	}
	drawLabel(d, menuFont, fg, it->x + 12, by, it->label, on && glossOn);
	if (it->kind == IT_SUB)
	    drawLabel(d, menuFont, fg, it->x + m->colW - 10 - arrowW, by, "▶",
		      on && glossOn);
    }
    XCopyArea(dpy, m->back, m->win, gc, 0, 0, m->w, m->h, 0, 0);
}

static Menu *menuOf(Window w)
{
    int i;
    for (i = 0; i < nMenus; ++i) if (menus[i].win == w) return &menus[i];
    return 0;
}

static void destroyMenu(Menu *m)
{
    XftDrawDestroy(m->draw);
    XFreePixmap(dpy, m->back);
    XFreePixmap(dpy, m->selGloss);
    XDestroyWindow(dpy, m->win);
    freeItems(m);
}

/* close menus[from] and everything opened from it */
static void closeMenus(int from)
{
    while (nMenus > from) destroyMenu(&menus[--nMenus]);
    if (nMenus == 0) {
	if (grabbed) {
	    XUngrabPointer(dpy, CurrentTime);
	    XUngrabKeyboard(dpy, CurrentTime);
	    grabbed = 0;
	}
	if (menuOpen) { menuOpen = 0; drawBar(); }
    }
}

static void grabInput(void)
{
    int i;
    unsigned int mask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;

    if (grabbed) return;
    for (i = 0; i < 50; ++i) {
	if (XGrabPointer(dpy, bar, True, mask, GrabModeAsync, GrabModeAsync,
			 None, None, CurrentTime) == GrabSuccess) {
	    grabbed = 1;
	    break;
	}
	usleep(10000);
    }
    for (i = 0; i < 50; ++i) {
	if (XGrabKeyboard(dpy, bar, True, GrabModeAsync, GrabModeAsync,
			  CurrentTime) == GrabSuccess) break;
	usleep(10000);
    }
    grabbed = 1;
}

static void placeMenu(Menu *m, int x, int y)
{
    if (x + m->w > scrW) x = scrW - m->w;
    if (y + m->h > scrH) y = scrH - m->h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    m->x = x;
    m->y = y;
}

/*
 * Open a menu. parent/item is the item it hangs from (the submenu
 * opens beside it), or -1/-1 with an explicit position.
 */
static Menu *openMenu(int type, int arg, int parent, int item, int px, int py)
{
    Menu *m;
    XSetWindowAttributes a;

    if (nMenus >= MAXMENUS) return 0;
    m = &menus[nMenus];
    memset(m, 0, sizeof *m);
    m->type = type;
    m->arg = arg;
    m->sel = -1;
    buildItems(m);
    layoutMenu(m);

    if (parent >= 0) {
	Menu *p = &menus[parent];
	Item *it = &p->items[item];
	int x = p->x + it->x + p->colW;
	if (x + m->w > scrW) x = p->x + it->x - m->w;
	placeMenu(m, x, p->y + it->y - 1);
    } else {
	placeMenu(m, px, py);
    }

    a.override_redirect = True;
    a.save_under = True;
    a.background_pixmap = None;
    a.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
	PointerMotionMask | LeaveWindowMask;
    m->win = XCreateWindow(dpy, root, m->x, m->y, m->w, m->h, 0, depth,
			   InputOutput, vis, CWOverrideRedirect | CWSaveUnder |
			   CWBackPixmap | CWEventMask, &a);
    m->back = XCreatePixmap(dpy, root, m->w, m->h, depth);
    m->selGloss = renderGloss(m->colW, itemH, glossBase);
    m->draw = XftDrawCreate(dpy, m->back, vis, cmap);
    ++nMenus;
    drawMenu(m);
    XMapRaised(dpy, m->win);
    grabInput();
    return m;
}

/* rebuild a menu in place (after the favourites changed) */
static void rebuildMenu(int idx)
{
    Menu *m = &menus[idx];

    freeItems(m);
    buildItems(m);
    layoutMenu(m);
    XftDrawDestroy(m->draw);
    XFreePixmap(dpy, m->back);
    XFreePixmap(dpy, m->selGloss);
    m->back = XCreatePixmap(dpy, root, m->w, m->h, depth);
    m->selGloss = renderGloss(m->colW, itemH, glossBase);
    m->draw = XftDrawCreate(dpy, m->back, vis, cmap);
    if (m->sel >= m->n) m->sel = -1;
    XResizeWindow(dpy, m->win, m->w, m->h);
    drawMenu(m);
}

static void openRootMenu(void)
{
    loadApps();
    loadFavs();
    menuOpen = 1;
    drawBar();
    openMenu(M_ROOT, 0, -1, -1, 0, barH);
}

/* under the Wi-Fi icon, its right edge at x */
static void openWifiMenu(int x)
{
    Menu *m;

    loadAps();
    if (wifiRadio == 1) {
	spawn("nmcli dev wifi rescan");
	wifiRefreshAt = time(0) + 4;
    }
    if (!(m = openMenu(M_WIFI, 0, -1, -1, x, barH))) return;
    placeMenu(m, x - m->w, barH);
    XMoveWindow(dpy, m->win, m->x, m->y);
}

/* under the Bluetooth icon, its right edge at x */
static void openBtMenu(int x)
{
    Menu *m;

    readBluetooth();
    loadBtDevices();
    if (!(m = openMenu(M_BT, 0, -1, -1, x, barH))) return;
    placeMenu(m, x - m->w, barH);
    XMoveWindow(dpy, m->win, m->x, m->y);
}

/* under the speaker, its right edge at x */
static void openSoundMenu(int x)
{
    Menu *m;

    readVolume();
    loadSinks();
    if (!(m = openMenu(M_SOUND, 0, -1, -1, x, barH))) return;
    placeMenu(m, x - m->w, barH);
    XMoveWindow(dpy, m->win, m->x, m->y);
}

/* under the input method, its right edge at x */
static void openImeMenu(int x)
{
    Menu *m;

    readIme();
    loadImes();
    if (!(m = openMenu(M_IME, 0, -1, -1, x, barH))) return;
    placeMenu(m, x - m->w, barH);
    XMoveWindow(dpy, m->win, m->x, m->y);
}

/* under the battery, its right edge at x */
static void openBatteryMenu(int x)
{
    Menu *m;

    loadProfiles();
    if (!(m = openMenu(M_BATTERY, 0, -1, -1, x, barH))) return;
    placeMenu(m, x - m->w, barH);
    XMoveWindow(dpy, m->win, m->x, m->y);
}

/* the rescan has had time to finish: show what it found */
static void refreshWifiMenu(void)
{
    if (!wifiRefreshAt || time(0) < wifiRefreshAt) return;
    wifiRefreshAt = 0;
    if (nMenus != 1 || menus[0].type != M_WIFI) return;
    loadAps();
    rebuildMenu(0);
}

/* a menu in the middle of the screen, with act selected */
static Menu *openDialog(int type, int arg, int act)
{
    Menu *m;
    int i;

    closeMenus(0);
    m = openMenu(type, arg, -1, -1, 0, 0);
    if (!m) return 0;
    XMoveWindow(dpy, m->win, (scrW - m->w) / 2, (scrH - m->h) / 3);
    m->x = (scrW - m->w) / 2;
    m->y = (scrH - m->h) / 3;
    for (i = 0; i < m->n; ++i)
	if (m->items[i].kind == IT_ACT && m->items[i].act == act) m->sel = i;
    drawMenu(m);
    return m;
}

static void openConfirm(int act)
{
    openDialog(M_CONFIRM, act, A_CANCEL);
}

static void openPassword(const char *ssid, int failed)
{
    snprintf(pwSsid, sizeof pwSsid, "%s", ssid);
    explicit_bzero(pwText, sizeof pwText);
    pwShow = 0;
    pwFailed = failed;
    openDialog(M_PASSWORD, 0, A_PW_CONNECT);
}

/* a network we have no password for asks for one */
static void connectAp(const Ap *a)
{
    if (a->secure && !a->known) openPassword(a->ssid, 0);
    else startConnect(a->ssid, 0, a->secure);
}

/* nmcli has finished */
static void connectDone(void)
{
    unsigned char r = 255;

    if (read(connFd, &r, 1) <= 0) r = 255;
    close(connFd);
    connFd = -1;
    readNet();
    readBluetooth();
    drawBar();
    if (r == 0 || nMenus) return;	/* connected, or busy with a menu */
    if (connKind == JOB_WIFI_SECURE) openPassword(connName, 1);
    else openDialog(M_NOTICE, 0, A_CANCEL);
}

static int itemAt(Menu *m, int x, int y)
{
    int i;
    for (i = 0; i < m->n; ++i) {
	Item *it = &m->items[i];
	if (x >= it->x && x < it->x + m->colW && y >= it->y && y < it->y + it->h)
	    return i;
    }
    return -1;
}

static void openSubmenu(int idx, int item)
{
    Item *it = &menus[idx].items[item];
    closeMenus(idx + 1);
    openMenu(it->subType, it->subArg, idx, item, 0, 0);
}

static void activate(int idx, int item)
{
    Item *it = &menus[idx].items[item];
    int app = it->app;

    switch (it->kind) {
    case IT_APP:
	closeMenus(0);
	if (app >= 0 && app < nApps) launchApp(&apps[app]);
	return;
    case IT_CMD: {
	char name[256];
	const char *cmd = it->ap < favs.n ? favCommand(favs.ids[it->ap], name, sizeof name) : 0;
	closeMenus(0);			/* frees it */
	spawn(cmd);
	return;
    }
    case IT_WIFI:
	closeMenus(0);
	if (it->ap < nAps && !aps[it->ap].inUse) connectAp(&aps[it->ap]);
	return;
    case IT_BTDEV:
	closeMenus(0);
	if (it->ap < nBtDevs) toggleBtDevice(&btDevs[it->ap]);
	return;
    case IT_SINK:
	closeMenus(0);
	if (it->ap < nSinks && !sinks[it->ap].isDefault) setDefaultSink(&sinks[it->ap]);
	drawBar();
	return;
    case IT_IME:
	closeMenus(0);
	if (it->ap < nImes && !imes[it->ap].current) selectIme(&imes[it->ap]);
	drawBar();
	return;
    case IT_PROFILE:
	if (it->ap < nProfiles && !profiles[it->ap].active) {
	    char cmd[128];
	    snprintf(cmd, sizeof cmd, "powerprofilesctl set '%s'", profiles[it->ap].name);
	    spawn(cmd);
	}
	closeMenus(0);
	return;
    case IT_SLIDER:		/* moved with the pointer, the wheel or ← → */
	return;
    case IT_SUB:
	openSubmenu(idx, item);
	return;
    case IT_ACT:
	break;
    default:
	return;
    }

    switch (it->act) {
    case A_LOCK:     closeMenus(0); spawn(cfg.lockCmd); break;
    case A_LOGOUT:   closeMenus(0); spawn(cfg.logoutCmd); break;
    case A_SUSPEND:  closeMenus(0); spawn(cfg.suspendCmd); break;
    case A_REBOOT:   closeMenus(0); spawn(cfg.rebootCmd); break;
    case A_POWEROFF: closeMenus(0); spawn(cfg.poweroffCmd); break;
    case A_CANCEL:   closeMenus(0); explicit_bzero(pwText, sizeof pwText); break;
    case A_ASK_REBOOT:   openConfirm(A_REBOOT); break;
    case A_ASK_POWEROFF: openConfirm(A_POWEROFF); break;
    case A_WIFI_ON:  closeMenus(0); spawn("nmcli radio wifi on"); break;
    case A_WIFI_OFF: closeMenus(0); spawn("nmcli radio wifi off"); break;
    case A_WIFI_SETTINGS: closeMenus(0); spawn(cfg.wifiSettingsCmd); break;
    case A_BT_ON:
	closeMenus(0);
	if (btBlocked()) {
	    btUnblock();
	    spawn("sleep 1; timeout 5 bluetoothctl power on");	/* once it is back */
	} else {
	    spawn("timeout 5 bluetoothctl power on");
	}
	break;
    case A_BT_OFF: closeMenus(0); spawn("timeout 5 bluetoothctl power off"); break;
    case A_BT_SETTINGS: closeMenus(0); spawn(cfg.btSettingsCmd); break;
    case A_MUTE: closeMenus(0); toggleMute(0); drawBar(); break;
    case A_SOUND_SETTINGS: closeMenus(0); spawn(cfg.soundSettingsCmd); break;
    case A_IME_SETTINGS: closeMenus(0); spawn(cfg.imeSettingsCmd); break;
    case A_BAR_SETTINGS: closeMenus(0); spawn(SETTINGS_PROG); break;
    case A_PW_SHOW:
	pwShow = !pwShow;
	rebuildMenu(idx);
	break;
    case A_PW_CONNECT:
	if (!pwText[0]) break;
	closeMenus(0);
	startConnect(pwSsid, pwText, 1);
	explicit_bzero(pwText, sizeof pwText);
	break;
    case A_WIFI_DISCONNECT: {
	char cmd[96];
	closeMenus(0);
	if (netKind != NET_WIFI || !netIf[0]) break;
	snprintf(cmd, sizeof cmd, "nmcli dev disconnect '%s'", netIf);
	spawn(cmd);
	break;
    }
    case A_TOGGLE_FAV:
	if (app >= 0 && app < nApps) toggleFav(apps[app].id);
	closeMenus(idx);
	if (nMenus > 0 && menus[0].type == M_ROOT) {
	    closeMenus(1);	/* the root grows or shrinks; drop its children */
	    rebuildMenu(0);
	}
	break;
    }
}

static void moveSel(Menu *m, int dir)
{
    int i, s = m->sel;

    for (i = 0; i < m->n; ++i) {
	s = s < 0 ? (dir > 0 ? 0 : m->n - 1) : (s + dir + m->n) % m->n;
	if (selectable(&m->items[s])) { m->sel = s; break; }
    }
    drawMenu(m);
}

/* typing into the password box; returns 1 if the key was used */
static int entryKey(XKeyEvent *e, Menu *m)
{
    char buf[16];
    KeySym k;
    int n = XLookupString(e, buf, sizeof buf - 1, &k, 0), len = (int)strlen(pwText);

    if (k == XK_BackSpace) {
	if (len) pwText[len - 1] = '\0';
    } else if ((e->state & ControlMask) && (k == XK_u || k == XK_U)) {
	explicit_bzero(pwText, sizeof pwText);
    } else if (n > 0 && !(e->state & ControlMask) && (unsigned char)buf[0] >= 0x20 &&
	       buf[0] != 0x7f) {
	if (len + n < (int)sizeof pwText) memcpy(pwText + len, buf, (size_t)n + 1);
    } else {
	return 0;
    }
    explicit_bzero(buf, sizeof buf);
    drawMenu(m);
    return 1;
}

/* the brightness and volume sliders: set from a pointer position, or step
   by the wheel and ← → */
static Menu *dragMenu;
static int dragItem = -1;

static void sliderPut(Menu *m, Item *it, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    if (it->ap == SL_VOLUME) {
	if (pct == volPct) return;
	setVolumePct(pct, pct > volPct);
	drawBar();
    } else {
	setBrightnessPct(pct);
    }
    drawMenu(m);
}

static void sliderSet(Menu *m, int i, int xRoot)
{
    int x0, x1;

    sliderTrack(m, &m->items[i], &x0, &x1);
    sliderPut(m, &m->items[i], ((xRoot - m->x) - x0) * 100 / (x1 - x0));
}

static void sliderStep(Menu *m, int i, int dir)
{
    Item *it = &m->items[i];
    int step = it->ap == SL_VOLUME ? cfg.volumeStep : cfg.brightnessStep,
	pct = sliderPct(it);

    if (pct < 0) return;
    if (it->ap == SL_VOLUME && pct > 100) pct = 100;
    sliderPut(m, it, pct + dir * (step > 0 ? step : 5));
}

static void menuKey(XKeyEvent *e)
{
    KeySym k = XLookupKeysym(e, 0);
    int idx = nMenus - 1;
    Menu *m = &menus[idx];

    if (m->type == M_PASSWORD && k != XK_Escape && k != XK_Return &&
	k != XK_KP_Enter && k != XK_Up && k != XK_Down && entryKey(e, m))
	return;
    switch (k) {
    case XK_Escape:
	closeMenus(0);
	explicit_bzero(pwText, sizeof pwText);
	break;
    case XK_Up:   case XK_KP_Up:   moveSel(m, -1); break;
    case XK_Down: case XK_KP_Down: moveSel(m, +1); break;
    case XK_Left: case XK_KP_Left:
	if (m->sel >= 0 && m->items[m->sel].kind == IT_SLIDER) sliderStep(m, m->sel, -1);
	else if (nMenus > 1) closeMenus(idx);
	break;
    case XK_Right: case XK_KP_Right:
	if (m->sel >= 0 && m->items[m->sel].kind == IT_SLIDER) {
	    sliderStep(m, m->sel, 1);
	    break;
	}
	/* fall through */
    case XK_Return: case XK_KP_Enter: case XK_space:
	if (m->sel < 0) break;
	if (m->items[m->sel].kind == IT_SUB) {
	    openSubmenu(idx, m->sel);
	    if (nMenus > idx + 1) moveSel(&menus[nMenus - 1], +1);
	} else if (k != XK_Right && k != XK_KP_Right) {
	    activate(idx, m->sel);
	}
	break;
    }
}

static void menuMotion(Menu *m, int x, int y)
{
    int idx = (int)(m - menus), i = itemAt(m, x, y);

    if (i >= 0 && !selectable(&m->items[i])) i = -1;
    if (i == m->sel) return;
    if (i < 0) {
	/* keep the item a submenu hangs from lit while the pointer
	   moves over to the submenu */
	if (nMenus > idx + 1) return;
	m->sel = -1;
	drawMenu(m);
	return;
    }
    m->sel = i;
    drawMenu(m);
    if (m->items[i].kind == IT_SUB) openSubmenu(idx, i);
    else closeMenus(idx + 1);
}

/* returns 1 if the event was used by the menus */
static int menuEvent(XEvent *e)
{
    Menu *m;

    if (!nMenus) return 0;
    switch (e->type) {
    case KeyPress:
	menuKey(&e->xkey);
	return 1;
    case MotionNotify:
	if (dragMenu) sliderSet(dragMenu, dragItem, e->xmotion.x_root);
	else if ((m = menuOf(e->xmotion.window)))
	    menuMotion(m, e->xmotion.x, e->xmotion.y);
	return 1;
    case ButtonPress:
	if (!(m = menuOf(e->xbutton.window))) {
	    closeMenus(0);			/* clicked outside */
	} else {
	    int i = itemAt(m, e->xbutton.x, e->xbutton.y);
	    if (i < 0 || m->items[i].kind != IT_SLIDER) return 1;
	    if (e->xbutton.button == Button1) {
		dragMenu = m;
		dragItem = i;
		sliderSet(m, i, e->xbutton.x_root);
	    } else if (e->xbutton.button == Button4 || e->xbutton.button == Button5) {
		sliderStep(m, i, e->xbutton.button == Button4 ? 1 : -1);
	    }
	}
	return 1;
    case ButtonRelease:
	if (dragMenu) {
	    dragMenu = 0;
	    return 1;
	}
	if ((m = menuOf(e->xbutton.window))) {
	    int idx = (int)(m - menus), i = itemAt(m, e->xbutton.x, e->xbutton.y);
	    if (i < 0 || !selectable(&m->items[i])) return 1;
	    if (e->xbutton.button == Button1) {
		activate(idx, i);
	    } else if (e->xbutton.button == Button3 && m->items[i].kind == IT_APP) {
		closeMenus(idx + 1);
		m->sel = i;
		drawMenu(m);
		openMenu(M_CONTEXT, m->items[i].app, -1, -1,
			 e->xbutton.x_root + 2, e->xbutton.y_root + 2);
	    }
	}
	return 1;
    case Expose:
	if ((m = menuOf(e->xexpose.window)) && e->xexpose.count == 0)
	    XCopyArea(dpy, m->back, m->win, gc, 0, 0, m->w, m->h, 0, 0);
	return menuOf(e->xexpose.window) != 0;
    }
    return 0;
}


/* ------------------------------------------------------------------ */
/* power: brightness keys, and what follows the power source          */

static KeyCode kcBrightUp, kcBrightDown;
static int lastAc = -1;

/* the optional commands: a feature whose command is missing turns itself off */
static void checkCommands(void)
{
    haveBusctl = inPath("busctl") ? 1 : inPath("dbus-send") ? 2 : 0;	/* 2: elogind systems */
    havePowerprofilesctl = inPath("powerprofilesctl");
    sndTool = inPath("wpctl") ? SND_WPCTL : inPath("pactl") ? SND_PACTL : SND_NONE;
    if (sndTool == SND_PACTL) {		/* get-sink-volume etc. came with PulseAudio 15 (2021) */
	FILE *p = popen("pactl --version 2>/dev/null", "r");
	char line[128];
	int major = 99;
	if (p) {
	    if (fgets(line, sizeof line, p) && !strncmp(line, "pactl ", 6)) major = atoi(line + 6);
	    pclose(p);
	}
	if (major < 15) {
	    sndTool = SND_NONE;
	    fprintf(stderr, PROGNAME ": pactl %d is too old (needs 15), no volume control\n", major);
	}
    }
    haveFcitx = inPath("fcitx5-remote");
    haveIbus = inPath("ibus");
    if (!sndTool)
	fprintf(stderr, PROGNAME ": neither wpctl (PipeWire) nor pactl (PulseAudio) "
		"found, no volume control\n");
    if (cfg.brightnessStep > 0 && !haveBusctl)
	fprintf(stderr, PROGNAME ": neither busctl (systemd) nor dbus-send found, "
		"leaving the brightness keys alone\n");
    /* Terminal=true entries: the configured terminal, $TERMINAL, or the first one found */
    if (!inPath(cfg.terminal)) {
	const char *t = getenv("TERMINAL"), *known[] = {
	    "x-terminal-emulator", "xfce4-terminal", "gnome-terminal", "konsole", "mate-terminal",
	    "lxterminal", "qterminal", "alacritty", "kitty", "tilix", "urxvt", "xterm", 0 };
	int i;
	if (cfg.terminal[0])
	    fprintf(stderr, PROGNAME ": terminal \"%s\" not found\n", cfg.terminal);
	if (!(t && *t && inPath(t)))
	    for (i = 0, t = ""; known[i]; ++i)
		if (inPath(known[i])) { t = known[i]; break; }
	SET(terminal, t);
    }
    if ((cfg.profileAc[0] || cfg.profileBattery[0]) && !havePowerprofilesctl)
	fprintf(stderr, PROGNAME ": powerprofilesctl (power-profiles-daemon) "
		"not found, not switching power profiles\n");
}

static void grabBrightnessKeys(void)
{
    if (cfg.brightnessStep <= 0 || !haveBusctl) return;
    kcBrightUp = XKeysymToKeycode(dpy, XF86XK_MonBrightnessUp);
    kcBrightDown = XKeysymToKeycode(dpy, XF86XK_MonBrightnessDown);
    if (kcBrightUp)
	XGrabKey(dpy, kcBrightUp, AnyModifier, root, False, GrabModeAsync, GrabModeAsync);
    if (kcBrightDown)
	XGrabKey(dpy, kcBrightDown, AnyModifier, root, False, GrabModeAsync, GrabModeAsync);
}

static int findBacklight(char *name, size_t n)
{
    DIR *d;
    struct dirent *e;

    if (cfg.backlight[0]) {
	snprintf(name, n, "%s", cfg.backlight);
	return 1;
    }
    if (!(d = opendir("/sys/class/backlight"))) return 0;
    name[0] = '\0';
    /* readdir order is creation order; with acpi_video0 next to
       intel_backlight/amdgpu_bl0 prefer by type like logind: raw > platform > firmware */
    {
	int best = -1;
	while ((e = readdir(d))) {
	    char path[512], buf[32];
	    int rank;
	    if (e->d_name[0] == '.') continue;
	    snprintf(path, sizeof path, "/sys/class/backlight/%s/type", e->d_name);
	    if (readFile(path, buf, sizeof buf) <= 0) rank = 0;
	    else rank = !strcmp(buf, "raw") ? 3 : !strcmp(buf, "platform") ? 2 : 1;
	    if (rank > best) {
		best = rank;
		snprintf(name, n, "%.63s", e->d_name);
	    }
	}
    }
    closedir(d);
    return name[0] != '\0';
}

/*
 * Writing /sys needs root, so ask logind to do it for this session
 * (org.freedesktop.login1.Session.SetBrightness). Key repeat and the
 * slider come faster than sysfs catches up, so for a second keep
 * counting from the last value sent.
 */
static long brTarget = -1;
static struct timeval brSent;

static int readBrightness(char *name, size_t n, long *cur, long *max)
{
    char path[512], buf[32];
    struct timeval now;

    if (!haveBusctl || !findBacklight(name, n)) return 0;
    snprintf(path, sizeof path, "/sys/class/backlight/%s/max_brightness", name);
    if (readFile(path, buf, sizeof buf) <= 0 || (*max = atol(buf)) <= 0) return 0;
    snprintf(path, sizeof path, "/sys/class/backlight/%s/brightness", name);
    if (readFile(path, buf, sizeof buf) <= 0) return 0;
    *cur = atol(buf);
    gettimeofday(&now, 0);
    if (brTarget >= 0 && (now.tv_sec - brSent.tv_sec) * 1000 +
	(now.tv_usec - brSent.tv_usec) / 1000 < 1000)
	*cur = brTarget;
    return 1;
}

static void setBrightness(const char *name, long v, long max)
{
    char cmd[512];
    long least = max / 100 > 1 ? max / 100 : 1;		/* never black */

    if (v > max) v = max;
    if (v < least) v = least;
    if (v == brTarget) return;
    if (haveBusctl == 2)
	snprintf(cmd, sizeof cmd, "dbus-send --system --dest=org.freedesktop.login1 "
		 "/org/freedesktop/login1/session/auto org.freedesktop.login1.Session.SetBrightness "
		 "string:backlight string:'%s' uint32:%ld", name, v);
    else
	snprintf(cmd, sizeof cmd, "busctl call org.freedesktop.login1 "
		 "/org/freedesktop/login1/session/auto org.freedesktop.login1.Session "
		 "SetBrightness ssu backlight '%s' %ld", name, v);
    spawn(cmd);
    brTarget = v;
    gettimeofday(&brSent, 0);
}

static void stepBrightness(int dir)
{
    char name[64];
    long cur, max, step;

    if (!readBrightness(name, sizeof name, &cur, &max)) return;
    step = max * cfg.brightnessStep / 100;
    if (step < 1) step = 1;
    setBrightness(name, cur + dir * step, max);
}

/* in %, -1: no backlight that can be set */
static int brightnessPct(void)
{
    char name[64];
    long cur, max;

    if (!readBrightness(name, sizeof name, &cur, &max)) return -1;
    return (int)((cur * 100 + max / 2) / max);
}

static void setBrightnessPct(int pct)
{
    char name[64];
    long cur, max;

    if (readBrightness(name, sizeof name, &cur, &max))
	setBrightness(name, (max * pct + 50) / 100, max);
}

static void setScreenOff(int minutes)
{
    int ev, err;

    if (minutes < 0 || !DPMSQueryExtension(dpy, &ev, &err)) return;
    if (minutes == 0) {
	DPMSDisable(dpy);
	return;
    }
    DPMSEnable(dpy);
    DPMSSetTimeouts(dpy, 0, 0, (CARD16)(minutes * 60));
}

static void followPowerSource(void)
{
    const char *profile;
    char cmd[128];

    if (acOnline < 0 || acOnline == lastAc) return;
    lastAc = acOnline;
    profile = acOnline ? cfg.profileAc : cfg.profileBattery;
    if (profile[0] && havePowerprofilesctl) {
	snprintf(cmd, sizeof cmd, "powerprofilesctl set '%s'", profile);
	spawn(cmd);
    }
    setScreenOff(acOnline ? cfg.screenOffAc : cfg.screenOffBattery);
}


/* ------------------------------------------------------------------ */
/* main                                                               */

static Bool isKey(Display *d, XEvent *e, XPointer k)
{
    (void)d;
    return (e->type == KeyPress || e->type == KeyRelease) &&
	e->xkey.keycode == *(KeyCode *)k;
}

/* take the repeats of a held key that are already queued, so the
   volume does not run on after the key is let go */
static int queuedRepeats(KeyCode k)
{
    XEvent e;
    int n = 0;
    while (XCheckIfEvent(dpy, &e, isKey, (XPointer)&k)) n += e.type == KeyPress;
    return n;
}

static void barEvent(XEvent *e)
{
    Hit *h;

    switch (e->type) {
    case Expose:
	if (e->xexpose.window == bar && e->xexpose.count == 0)
	    XCopyArea(dpy, barBack, bar, gc, 0, 0, scrW, barH, 0, 0);
	break;
    case KeyPress: {
	KeyCode k = e->xkey.keycode;
	if (!k) break;
	if (k == kcBrightUp) stepBrightness(1);
	else if (k == kcBrightDown) stepBrightness(-1);
	else if (k == kcVolUp || k == kcVolDown) {
	    int n = 1 + queuedRepeats(k);
	    changeVolume(k == kcVolUp ? n : -n);
	    flashTip(H_VOL, 0);
	    drawBar();
	} else if (k == kcMute || k == kcMicMute) {
	    toggleMute(k == kcMicMute);
	    flashTip(H_VOL, k == kcMicMute);
	    drawBar();
	}
	break;
    }
    case ButtonPress:
	if (e->xbutton.window != bar) break;
	h = hitAt(e->xbutton.x);
	if (e->xbutton.button == Button4 || e->xbutton.button == Button5) {
	    if (h && h->kind == H_DESKTOP)
		switchDesktop(curDesktop + (e->xbutton.button == Button4 ? -1 : 1));
	    else if (h && h->kind == H_VOL) {
		changeVolume(e->xbutton.button == Button4 ? 1 : -1);
		drawBar();
	    }
	} else if (e->xbutton.button == Button1 && h) {
	    if (h->kind == H_MENU) openRootMenu();
	    else if (h->kind == H_DESKTOP) switchDesktop(h->arg);
	    else if (h->kind == H_TASK) clickTask(h->arg);
	    else if (h->kind == H_NET) openWifiMenu(h->x1);
	    else if (h->kind == H_BT) openBtMenu(h->x1);
	    else if (h->kind == H_VOL) openSoundMenu(h->x1);
	    else if (h->kind == H_BAT) openBatteryMenu(h->x1);
	    else if (h->kind == H_IME && imKind == IM_FCITX5) { toggleIme(); drawBar(); }
	    else if (h->kind == H_IME) openImeMenu(h->x1);
	}
	break;
    case MotionNotify:
	if (e->xmotion.window == bar) {
	    h = hitAt(e->xmotion.x);
	    int k = h ? h->kind : H_NONE, a = h ? h->arg : -1;
	    if (k != hoverKind || a != hoverArg) {
		hoverKind = k; hoverArg = a;
		drawBar();
	    }
	}
	break;
    case LeaveNotify:
	if (e->xcrossing.window == bar && hoverKind != H_NONE) {
	    hoverKind = H_NONE; hoverArg = -1;
	    drawBar();
	}
	break;
    case PropertyNotify:
	if (e->xproperty.window == root) {
	    if (e->xproperty.atom == aNumDesktops || e->xproperty.atom == aCurDesktop) {
		readDesktops();
		drawBar();
	    } else if (e->xproperty.atom == aActive) {
		readActive();
		drawBar();
	    } else if (e->xproperty.atom == aClientList) {
		readTasks();
		drawBar();
	    }
	} else {
	    Task *t = taskOf(e->xproperty.window);
	    Atom a = e->xproperty.atom;
	    if (t && (a == aNetWmName || a == XA_WM_NAME || a == aIcccmState ||
		      a == aWmDesktop || a == aWmState)) {
		if (a == aWmState && skipTaskbar(t->w)) readTasks();
		else readTask(t);
		drawBar();
	    }
	}
	break;
    case ConfigureNotify:
	if (e->xconfigure.window == root &&
	    (e->xconfigure.width != scrW || e->xconfigure.height != scrH)) {
	    scrW = e->xconfigure.width;
	    scrH = e->xconfigure.height;
	    XResizeWindow(dpy, bar, scrW, barH);
	    setStruts();
	    makeBarBuffers();
	    shapeBar();
	    drawBar();
	}
	break;
    }
}

static void onSignal(int sig)
{
    if (sig == SIGHUP) gotHup = 1;
    else gotTerm = 1;
}

static void setupColours(void)
{
    XColor x;

    glossOn = strcmp(cfg.gloss, "none") != 0 && vis->class == TrueColor &&
	XParseColor(dpy, cmap, cfg.gloss, &x);
    if (glossOn) {
	glossBase[0] = x.red / 65535.0;
	glossBase[1] = x.green / 65535.0;
	glossBase[2] = x.blue / 65535.0;
	/* the active window's button: the same gloss on its own colour, so
	   that it does not run into the tab at the left end */
	memcpy(taskBase, glossBase, sizeof taskBase);
	if (strcmp(cfg.taskGloss, "theme") && XParseColor(dpy, cmap, cfg.taskGloss, &x)) {
	    taskBase[0] = x.red / 65535.0;
	    taskBase[1] = x.green / 65535.0;
	    taskBase[2] = x.blue / 65535.0;
	}
    } else if (!strcmp(cfg.foreground, "white")) {
	SET(foreground, "black");	/* the plain grey wm2 tab */
    }

    allocColor(&cFg, cfg.foreground, 0xffff);
    allocColor(&cShadow, "#00001f", 0x9999);	/* wm2 tab: 0,0,0.12 at 60% */
    allocColor(&cHover, "white", 0x2000);
    allocColor(&cDeskCur, "white", 0x4800);
    allocColor(&cDeskLine, "#5a5f68", 0xffff);
    allocColor(&cButton, cfg.buttonBackground, 0xffff);
    allocColor(&cButtonOn, glossOn ? cfg.gloss : "gray60", 0xffff);
    allocColor(&cBarBg, cfg.barBackground, 0xffff);
    allocColor(&cText, cfg.textColour, 0xffff);
    allocColor(&cDim, "#a8adb5", 0xffff);
    allocColor(&cWell, "#16181c", 0xffff);
    allocColor(&cWellLine, "#4a4f58", 0xffff);
    allocColor(&cMeter, glossOn ? cfg.gloss : "#2a7de1", 0xffff);
    allocColor(&cHot, "#f0603c", 0xffff);
    allocColor(&cOff, "#4a4f58", 0xffff);
    allocColor(&cBatFill, "#d8dce2", 0xffff);
    allocColor(&cTask, "#2a2d33", 0xffff);
    allocColor(&cTaskHover, "#3a3e46", 0xffff);
    allocColor(&cWarn, "#ffd24a", 0xffff);
    allocColor(&cMenuBg, "#f4f5f7", 0xffff);
    allocColor(&cMenuFg, "#1a1a1a", 0xffff);
    allocColor(&cMenuDim, "#7a7f88", 0xffff);
    allocColor(&cMenuLine, "#c4c8d0", 0xffff);
    allocColor(&cEntryBg, "white", 0xffff);
    allocColor(&cBorder, cfg.borders, 0xffff);
    allocColor(&cMenuBorder, "black", 0xffff);
    allocColor(&cFlatBg, cfg.background, 0xffff);
}

static XftFont *openFont(const char *name, const char *fallback)
{
    XftFont *f = XftFontOpenName(dpy, scr, name);
    if (!f) {
	fprintf(stderr, PROGNAME ": cannot open font \"%s\"\n", name);
	f = XftFontOpenName(dpy, scr, fallback);
    }
    return f;
}

static void tick(int slow)
{
    readCpu();
    readMem();
    if (slow) {
	readNet(); readBluetooth(); readBattery(); followPowerSource();
	readVolume();
    }
    if (slow || (imName[0] && imKind != IM_XKB)) readIme();	/* none: look less often; XKB sends events */
    if (flashKind != H_NONE && time(0) >= flashUntil) flashKind = H_NONE;
    refreshWifiMenu();
}

int main(int argc, char **argv)
{
    struct sigaction sa;
    int fd, count = 0;
    time_t lastTick = 0;

    if (argc > 1) {
	if (!strcmp(argv[1], "--version") || !strcmp(argv[1], "-v")) {
	    printf(PROGNAME " " VERSION "\n");
	    return 0;
	}
	fprintf(stderr, "usage: " PROGNAME " [--version]\n"
		"  config: ~/.config/" PROGNAME "/config (reload: kill -HUP)\n");
	return 2;
    }

    /* a locale that is not generated (common on Arch/Gentoo/Void, musl): stay
       UTF-8 at least, so Xft gets valid text */
    if (!setlocale(LC_ALL, "")) {
	fprintf(stderr, PROGNAME ": locale \"%s\" not available, using C.UTF-8\n",
		getenv("LANG") ? getenv("LANG") : "");
	if (!setlocale(LC_ALL, "C.UTF-8")) setlocale(LC_ALL, "C");
    }
    bindtextdomain(PROGNAME, LOCALEDIR);
    bind_textdomain_codeset(PROGNAME, "UTF-8");
    textdomain(PROGNAME);
    setLocaleKeys();
    if (!(dpy = XOpenDisplay(0))) {
	fprintf(stderr, PROGNAME ": cannot open display\n");
	return 1;
    }
    fcntl(ConnectionNumber(dpy), F_SETFD, FD_CLOEXEC);
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    vis = DefaultVisual(dpy, scr);
    cmap = DefaultColormap(dpy, scr);
    depth = DefaultDepth(dpy, scr);
    gc = XCreateGC(dpy, root, 0, 0);
    scrW = DisplayWidth(dpy, scr);
    scrH = DisplayHeight(dpy, scr);
    internAtoms();

    if (XGetSelectionOwner(dpy, aSelection) != None) {
	fprintf(stderr, PROGNAME ": already running on this screen\n");
	return 1;
    }

    loadConfig();
    setupColours();
    barFont = openFont(cfg.font, "sans-10");
    menuFont = openFont(cfg.menuFont, "sans-11");
    if (!barFont || !menuFont) return 1;

    barH = cfg.height;
    if (barH <= 0) {
	int fontH = barFont->ascent + barFont->descent;
	int tw = cfg.tabWidth > fontH + 4 ? cfg.tabWidth : fontH + 4;
	barH = tw + 2;		/* the wm2 tab is tab_width + 2 across */
    }
    itemH = menuFont->ascent + menuFont->descent + 10;

    if (!cfg.tabLabel[0]) gethostname(cfg.tabLabel, sizeof cfg.tabLabel - 1);
    if (cfg.tabLabel[0] == ' ' && !cfg.tabLabel[1]) cfg.tabLabel[0] = '\0';
    if (cfg.tabLength <= 0)	/* the button, the label, a little room */
	cfg.tabLength = notchHi() + 8 +
	    (cfg.tabLabel[0] ? textWidth(barFont, cfg.tabLabel) + 10 : 0);

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = onSignal;
    sigaction(SIGHUP, &sa, 0);
    sigaction(SIGTERM, &sa, 0);
    sigaction(SIGINT, &sa, 0);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, 0);		/* nmcli may exit before reading */
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, 0);

    XSetErrorHandler(ignoreXError);
    createBar();
    XSetSelectionOwner(dpy, aSelection, bar, CurrentTime);
    XSelectInput(dpy, root, PropertyChangeMask | StructureNotifyMask);
    checkCommands();
    grabBrightnessKeys();
    grabVolumeKeys();
    trayStart();
    xkbStart();
    readDesktops();
    readActive();
    readTasks();
    tick(1);
    drawBar();
    fd = ConnectionNumber(dpy);

    while (!gotTerm) {
	struct timeval tv, now;
	fd_set fds;

	while (XPending(dpy)) {
	    XEvent e;
	    XNextEvent(dpy, &e);
	    if (e.type == SelectionClear && e.xselectionclear.window == bar) {
		gotTerm = 1;	/* another instance took over */
		break;
	    }
	    if (xkbBase >= 0 && e.type == xkbBase) {	/* the layout group changed */
		readIme();
		drawBar();
		continue;
	    }
	    if (!trayEvent(&e) && !menuEvent(&e)) barEvent(&e);
	}
	if (gotHup) {
	    /* reload: start again with the new configuration */
	    trayStop();
	    XCloseDisplay(dpy);
	    execvp(argv[0], argv);	/* the binary may have been replaced */
	    execv("/proc/self/exe", argv);
	    return 1;
	}
	XFlush(dpy);

	/* wake on the next second, for the clock */
	gettimeofday(&now, 0);
	if (now.tv_sec != lastTick) {
	    lastTick = now.tv_sec;
	    tick(++count % 5 == 0);
	    drawBar();
	    XFlush(dpy);
	}
	tv.tv_sec = 0;
	tv.tv_usec = 1000000 - now.tv_usec;
	if (tv.tv_usec >= 1000000) tv.tv_usec = 999999;
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	if (connFd >= 0) FD_SET(connFd, &fds);
	if (select((connFd > fd ? connFd : fd) + 1, &fds, 0, 0, &tv) > 0 &&
	    connFd >= 0 && FD_ISSET(connFd, &fds))
	    connectDone();
    }

    closeMenus(0);
    trayStop();
    XCloseDisplay(dpy);
    return 0;
}
