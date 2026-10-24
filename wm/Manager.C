
#include "Manager.h"
#include "Client.h"
#include <string.h>
#include <strings.h>
#include <X11/Xproto.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "Cursors.h"
#include <X11/cursorfont.h>

Atom    Atoms::wm_state;
Atom    Atoms::wm_changeState;
Atom    Atoms::wm_protocols;
Atom    Atoms::wm_delete;
Atom    Atoms::wm_takeFocus;
Atom    Atoms::wm_colormaps;
Atom    Atoms::wm2_running;

Atom    Atoms::net_supported;
Atom    Atoms::net_number_of_desktops;
Atom    Atoms::net_current_desktop;
Atom    Atoms::net_wm_desktop;
Atom    Atoms::net_client_list;
Atom    Atoms::net_client_list_stacking;
Atom    Atoms::net_active_window;
Atom    Atoms::net_supporting_wm_check;
Atom    Atoms::net_wm_name;
Atom    Atoms::utf8_string;
Atom    Atoms::net_wm_window_type;
Atom    Atoms::net_wm_window_type_dock;
Atom    Atoms::net_wm_window_type_desktop;
Atom    Atoms::net_wm_window_type_notification;
Atom    Atoms::net_wm_window_type_splash;
Atom    Atoms::net_wm_window_type_tooltip;
Atom    Atoms::net_wm_window_type_popup_menu;
Atom    Atoms::net_wm_window_type_dropdown_menu;
Atom    Atoms::net_wm_window_type_menu;
Atom    Atoms::net_wm_state;
Atom    Atoms::net_wm_state_maximized_vert;
Atom    Atoms::net_wm_state_maximized_horz;
Atom    Atoms::net_wm_state_fullscreen;
Atom    Atoms::wm2_restore_geometry;
Atom    Atoms::wm2_off_desktop;
Atom    Atoms::net_wm_strut;
Atom    Atoms::net_wm_strut_partial;

volatile sig_atomic_t WindowManager::m_signalled = False;
int     WindowManager::m_restart = False;

void WindowManager::hupHandler(int)
{
    m_restart = True;
    m_signalled = True;
}
Boolean WindowManager::m_initialising = False;
Boolean ignoreBadWindowErrors;

const char *const WindowManager::m_menuCreateLabel = "New";

implementPList(ClientList, Client);


WindowManager::WindowManager() :
    m_menuGC(0), m_menuWindow(0), m_menuFont(0), m_menuDraw(0),
    m_focusChanging(False)
{
    fprintf(stderr, "\nwm2: Copyright (c) 1996-7 Chris Cannam."
	    "  Fourth release, March 1997\n"
	    "     Parts derived from 9wm Copyright (c) 1994-96 David Hogan\n"
	    "     %s\n     Copying and redistribution encouraged.  "
	    "No warranty.\n\n", XV_COPYRIGHT);

    if (CONFIG_AUTO_RAISE) {
	if (CONFIG_CLICK_TO_FOCUS) {
	    fatal("can't have auto-raise-with-delay with click-to-focus");
	} else if (CONFIG_RAISE_ON_FOCUS) {
	    fatal("can't have raise-on-focus AND auto-raise-with-delay");
	} else {
	    fprintf(stderr, "     Focus follows, auto-raise with delay.  ");
	}

    } else {
	if (CONFIG_CLICK_TO_FOCUS) {
	    if (CONFIG_RAISE_ON_FOCUS) {
		fprintf(stderr, "     Click to focus.  ");
	    } else {
		fatal("can't have click-to-focus without raise-on-focus");
	    }
	} else {
	    if (CONFIG_RAISE_ON_FOCUS) {
		fprintf(stderr, "     Focus follows, auto-raise.  ");
	    } else {
		fprintf(stderr, "     Focus follows pointer.  ");
	    }
	}
    }

    if (CONFIG_EVERYTHING_ON_ROOT_MENU) {
	fprintf(stderr, "All clients on menu.\n");
    } else {
	fprintf(stderr, "Hidden clients only on menu.\n");
    }

    if (CONFIG_PROD_SHAPE) {
	fprintf(stderr, "     Shape prodding on.  ");
    } else {
	fprintf(stderr, "     Shape prodding off.  ");
    }

    fprintf(stderr, "\n     (To reconfigure, simply edit and recompile.)\n\n");

    m_display = XOpenDisplay(NULL);
    if (!m_display) fatal("can't open display");
    if (XSupportsLocale()) XSetLocaleModifiers("");

    m_shell = (char *)getenv("SHELL");
    if (!m_shell) m_shell = NewString("/bin/sh");

    m_initialising = True;
    XSetErrorHandler(errorHandler);
    ignoreBadWindowErrors = False;

    // 9wm does more, I think for nohup
    installSignal(SIGTERM, sigHandler);
    installSignal(SIGINT,  sigHandler);
    installSignal(SIGHUP,  hupHandler);

    m_currentTime = -1;
    m_activeClient = 0;

    m_currentDesktop = 0;
    m_numDesktops    = wm2config.num_desktops;
    m_wmCheck = None;
    m_dockCount = 0;
    m_strutTop = m_strutBottom = 0;
    if (m_numDesktops < 1) m_numDesktops = 1;
    // Dynamic mode: allow the maximum while windows come back after a
    // restart (_NET_WM_DESKTOP); updateDynamicDesktops() trims it.
    if (wm2config.dynamic_desktops) m_numDesktops = 16;

    Atoms::wm_state      = XInternAtom(m_display, "WM_STATE",            False);
    Atoms::wm_changeState= XInternAtom(m_display, "WM_CHANGE_STATE",     False);
    Atoms::wm_protocols  = XInternAtom(m_display, "WM_PROTOCOLS",        False);
    Atoms::wm_delete     = XInternAtom(m_display, "WM_DELETE_WINDOW",    False);
    Atoms::wm_takeFocus  = XInternAtom(m_display, "WM_TAKE_FOCUS",       False);
    Atoms::wm_colormaps  = XInternAtom(m_display, "WM_COLORMAP_WINDOWS", False);
    Atoms::wm2_running   = XInternAtom(m_display, "_WM2_RUNNING",        False);

    Atoms::net_supported           = XInternAtom(m_display, "_NET_SUPPORTED",           False);
    Atoms::net_number_of_desktops  = XInternAtom(m_display, "_NET_NUMBER_OF_DESKTOPS",  False);
    Atoms::net_current_desktop     = XInternAtom(m_display, "_NET_CURRENT_DESKTOP",     False);
    Atoms::net_wm_desktop          = XInternAtom(m_display, "_NET_WM_DESKTOP",          False);
    Atoms::net_client_list         = XInternAtom(m_display, "_NET_CLIENT_LIST",         False);
    Atoms::net_client_list_stacking= XInternAtom(m_display, "_NET_CLIENT_LIST_STACKING",False);
    Atoms::net_active_window       = XInternAtom(m_display, "_NET_ACTIVE_WINDOW",       False);
    Atoms::net_supporting_wm_check = XInternAtom(m_display, "_NET_SUPPORTING_WM_CHECK", False);
    Atoms::net_wm_name             = XInternAtom(m_display, "_NET_WM_NAME",             False);
    Atoms::utf8_string             = XInternAtom(m_display, "UTF8_STRING",              False);
    Atoms::net_wm_window_type      = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE",      False);
    Atoms::net_wm_window_type_dock = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    Atoms::net_wm_window_type_desktop      = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    Atoms::net_wm_window_type_notification = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    Atoms::net_wm_window_type_splash       = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    Atoms::net_wm_window_type_tooltip      = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    Atoms::net_wm_window_type_popup_menu   = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    Atoms::net_wm_window_type_dropdown_menu= XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
    Atoms::net_wm_window_type_menu = XInternAtom(m_display, "_NET_WM_WINDOW_TYPE_MENU", False);
    Atoms::net_wm_state            = XInternAtom(m_display, "_NET_WM_STATE",            False);
    Atoms::net_wm_state_maximized_vert = XInternAtom(m_display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    Atoms::net_wm_state_maximized_horz = XInternAtom(m_display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atoms::net_wm_state_fullscreen = XInternAtom(m_display, "_NET_WM_STATE_FULLSCREEN", False);
    Atoms::wm2_restore_geometry    = XInternAtom(m_display, "_WM2_RESTORE_GEOMETRY",    False);
    Atoms::wm2_off_desktop         = XInternAtom(m_display, "_WM2_OFF_DESKTOP",         False);

    Atoms::net_wm_strut            = XInternAtom(m_display, "_NET_WM_STRUT",            False);
    Atoms::net_wm_strut_partial    = XInternAtom(m_display, "_NET_WM_STRUT_PARTIAL",    False);

    int dummy;
    if (!XShapeQueryExtension(m_display, &m_shapeEvent, &dummy))
	fatal("no shape extension, can't run without it");

    // we only cope with one screen!
    initialiseScreen();

    XSetSelectionOwner(m_display, Atoms::wm2_running,
		       m_menuWindow, timestamp(True));
    XSync(m_display, False);
    m_initialising = False;
    m_returnCode = 0;
    
    clearFocus();
    scanInitialWindows();
    updateDynamicDesktops();
    loop();
}
    

WindowManager::~WindowManager()
{
    // empty
}


void WindowManager::release()
{
    if (m_returnCode != 0) return; // hasty exit

    ClientList normalList, unparentList;
    Client *c;
    int i;

    // Requests redirected to us that the main loop left when the signal
    // came (it stops reading at once) would be lost with our connection:
    // a program started from the root menu -- which also holds the SIGHUP
    // back until it closes -- asks to be mapped just as we go, and its
    // window never appeared (the wallpaper chooser "froze", 2026-10-24).
    // Grab the server so no more come to us, take them out of the queue
    // (the XSync(True) in Client::unreparent() would drop them) and carry
    // them out before we disconnect, which also ends the grab.
    XGrabServer(m_display);
    XSync(m_display, False);
    Window pendingMap[64];
    int pendingMaps = 0;
    XEvent pe;
    while (XCheckMaskEvent(m_display, SubstructureRedirectMask, &pe)) {
	if (pe.type == MapRequest && pendingMaps < 64) {
	    pendingMap[pendingMaps++] = pe.xmaprequest.window;
	} else if (pe.type == ConfigureRequest) {
	    XWindowChanges wc;
	    wc.x = pe.xconfigurerequest.x;
	    wc.y = pe.xconfigurerequest.y;
	    wc.width = pe.xconfigurerequest.width;
	    wc.height = pe.xconfigurerequest.height;
	    wc.border_width = pe.xconfigurerequest.border_width;
	    ignoreBadWindowErrors = True;
	    XConfigureWindow(m_display, pe.xconfigurerequest.window,
			     pe.xconfigurerequest.value_mask &
			     (CWX | CWY | CWWidth | CWHeight | CWBorderWidth), &wc);
	    ignoreBadWindowErrors = False;
	}
    }

    for (i = 0; i < m_clients.count(); ++i) {
	c = m_clients.item(i);
//    fprintf(stderr, "client %d is %p\n", i, c);

	if (c->isNormal()) normalList.append(c);
	else unparentList.append(c);
    }

    // Normal windows go in their stacking order, bottom first: each
    // XReparentWindow puts the window on top, so the topmost goes last and
    // the restarted wm2tj, which adopts them in XQueryTree order (bottom to
    // top), keeps the stack.  Going by creation order turned the stack over
    // on every SIGHUP (the favourites editor ended up under the terminal).
    ClientList stacked;
    Window w1, w2, *wins = 0;
    unsigned int k, n = 0;
    if (XQueryTree(m_display, m_root, &w1, &w2, &wins, &n)) {
	for (k = 0; k < n; ++k) {
	    for (i = 0; i < normalList.count(); ++i) {
		c = normalList.item(i);
		if (c && c->parent() == wins[k]) {
		    stacked.append(c);
		    normalList.item(i) = 0;
		    break;
		}
	    }
	}
	if (wins) XFree((void *)wins);
    }
    for (i = normalList.count()-1; i >= 0; --i) {	// not found: at the bottom
	if (normalList.item(i)) unparentList.append(normalList.item(i));
    }
    for (i = 0; i < stacked.count(); ++i) {
	unparentList.append(stacked.item(i));
    }

    m_clients.remove_all();

    // No client is active while they go: Client::release() of the active
    // one would activate its m_revert, which may be released (deleted)
    // already -- skipInRevert() cannot fix it with m_clients empty.
    // That crashed the SIGHUP restart and ended the session (2026-10-23).
    setActiveClient(0);

    for (i = 0; i < unparentList.count(); ++i) {
//	fprintf(stderr, "unparenting client %p\n",unparentList.item(i));
	Client *uc = unparentList.item(i);
	uc->unreparent();
	// On restart, windows hidden for other desktops stay unmapped and are
	// marked, so the new instance adopts them hidden (it restores their
	// _NET_WM_DESKTOP).  Mapping them here made apps such as Electron ask
	// for activation (switching desktop) and could leave them unmanaged.
	// Off the save-set too, or the server maps them when we disconnect.
	if (m_restart && uc->isNormal() && uc->isOffDesktop()) {
	    unsigned long one = 1;
	    XChangeProperty(m_display, uc->window(), Atoms::wm2_off_desktop,
			    XA_CARDINAL, 32, PropModeReplace,
			    (unsigned char *)&one, 1);
	    XRemoveFromSaveSet(m_display, uc->window());
	} else if (m_restart && !uc->isWithdrawn()) {
	    XMapWindow(m_display, uc->window());
	}
	unparentList.item(i)->release();
	unparentList.item(i) = 0;
    }

    // the windows that asked to be mapped: viewable, the restarted wm2tj
    // frames them (without us nobody would map them); on top, as a new
    // window would be (the others were just stacked over it)
    ignoreBadWindowErrors = True;
    for (i = 0; i < pendingMaps; ++i) XMapRaised(m_display, pendingMap[i]);
    XSync(m_display, False);
    ignoreBadWindowErrors = False;

    XSetInputFocus(m_display, PointerRoot, RevertToPointerRoot,
		   timestamp(False));
    installColormap(None);

    XFreeCursor(m_display, m_cursor);
    XFreeCursor(m_display, m_xCursor);
    XFreeCursor(m_display, m_vCursor);
    XFreeCursor(m_display, m_hCursor);
    XFreeCursor(m_display, m_vhCursor);

    if (m_menuDraw) XftDrawDestroy(m_menuDraw);
    XftColorFree(m_display, DefaultVisual(m_display, m_screenNumber),
                 m_defaultColormap, &m_menuXftColor);
    if (m_menuFont) XftFontClose(m_display, m_menuFont);
    XFreeGC(m_display, m_menuGC);

    XCloseDisplay(m_display);
}


void WindowManager::fatal(const char *message)
{
    fprintf(stderr, "wm2tj: ");
    perror(message);
    fprintf(stderr, "\n");
    exit(1);
}


int WindowManager::errorHandler(Display *d, XErrorEvent *e)
{
    if (m_initialising && (e->request_code == X_ChangeWindowAttributes) &&
	e->error_code == BadAccess) {
	fprintf(stderr, "wm2tj: another window manager running?\n");
	exit(1);
    }

    // ugh
    if (ignoreBadWindowErrors == True && e->error_code == BadWindow) return 0;

    char msg[100], number[30], request[100];
    XGetErrorText(d, e->error_code, msg, 100);
    sprintf(number, "%d", e->request_code);
    XGetErrorDatabaseText(d, "XRequest", number, "", request, 100);

    if (request[0] == '\0') sprintf(request, "<request-code-%d>",
				    e->request_code);

    fprintf(stderr, "wm2tj: %s (0x%lx): %s\n", request, e->resourceid, msg);

    if (m_initialising) {
	fprintf(stderr, "wm2tj: failure during initialisation, abandoning\n");
	exit(1);
    }

    return 0;
}    


static Cursor makeCursor(Display *d, Window w,
			 unsigned char *bits, unsigned char *mask_bits,
			 int width, int height, int xhot, int yhot,
			 XColor *fg, XColor *bg)
{
    Pixmap pixmap =
 	XCreateBitmapFromData(d, w, (const char *)bits, width, height);

    Pixmap mask =
 	XCreateBitmapFromData(d, w, (const char *)mask_bits, width, height);

    Cursor cursor = XCreatePixmapCursor(d, pixmap, mask, fg, bg, xhot, yhot);
    XFreePixmap(d, pixmap);
    XFreePixmap(d, mask);

    return cursor;
}


void WindowManager::initialiseScreen()
{
    int i = 0;
    m_screenNumber = i;

    m_root = RootWindow(m_display, i);

    // Restarted (SIGHUP): stay on the desktop the previous instance was on.
    // Read it here, after m_root is known and before we publish our own.
    {
	Atom type; int format; unsigned long n, after;
	unsigned char *data = 0;
	if (XGetWindowProperty(m_display, m_root, Atoms::net_current_desktop,
			       0, 1, False, XA_CARDINAL, &type, &format, &n,
			       &after, &data) == Success && data && n == 1) {
	    int cd = (int)*(unsigned long *)data;
	    if (cd > 0 && cd < m_numDesktops) m_currentDesktop = cd;
	}
	if (data) XFree(data);
    }
    m_defaultColormap = DefaultColormap(m_display, i);
    m_minimumColormaps = MinCmapsOfScreen(ScreenOfDisplay(m_display, i));

    XColor black, white, temp;

    if (!XAllocNamedColor(m_display, m_defaultColormap, "black", &black, &temp))
	fatal("couldn't load colour \"black\"!");
    if (!XAllocNamedColor(m_display, m_defaultColormap, "white", &white, &temp))
	fatal("couldn't load colour \"white\"!");

    // Font cursors: with Xcursor.theme_core set, libX11 swaps these for the
    // Xcursor theme (DMZ-Blue), so the root window matches the apps.
    m_cursor = XCreateFontCursor(m_display, XC_left_ptr);
    m_xCursor = XCreateFontCursor(m_display, XC_X_cursor);
    m_hCursor = XCreateFontCursor(m_display, XC_right_side);
    m_vCursor = XCreateFontCursor(m_display, XC_bottom_side);
    m_vhCursor = XCreateFontCursor(m_display, XC_bottom_right_corner);
    (void)makeCursor; (void)black; (void)white;

    XSetWindowAttributes attr;
    attr.cursor = m_cursor;
    attr.event_mask = SubstructureRedirectMask | SubstructureNotifyMask |
	ColormapChangeMask | ButtonPressMask | ButtonReleaseMask | 
	PropertyChangeMask | FocusChangeMask;
    XChangeWindowAttributes(m_display, m_root, CWCursor | CWEventMask, &attr);
    XSync(m_display, False);

    m_menuForegroundPixel =
	allocateColour(CONFIG_MENU_FOREGROUND, "menu foreground");
    m_menuBackgroundPixel =
	allocateColour(CONFIG_MENU_BACKGROUND, "menu background");
    // "auto" falls back to the window borders; with borders "none" the
    // menu frame takes the menu background so it doesn't show either
    const char *mb = strcmp(CONFIG_MENU_BORDERS, "auto") ? CONFIG_MENU_BORDERS
	: CONFIG_BORDERS;
    if (!strcasecmp(mb, "none")) mb = CONFIG_MENU_BACKGROUND;
    m_menuBorderPixel = allocateColour(mb, "menu border");

    m_menuWindow = XCreateSimpleWindow
	(m_display, m_root, 0, 0, 1, 1, MENU_BORDER_WIDTH,
	 m_menuBorderPixel, m_menuBackgroundPixel);
    for (int k = 0; k < MENU_DEPTH - 1; ++k) {
	m_subMenuWindow[k] = XCreateSimpleWindow
	    (m_display, m_root, 0, 0, 1, 1, MENU_BORDER_WIDTH,
	     m_menuBorderPixel, m_menuBackgroundPixel);
    }

    if (DoesSaveUnders(ScreenOfDisplay(m_display, m_screenNumber))) {
	XSetWindowAttributes attr;
	attr.save_under = True;
	XChangeWindowAttributes(m_display, m_menuWindow, CWSaveUnder, &attr);
	for (int k = 0; k < MENU_DEPTH - 1; ++k) {
	    XChangeWindowAttributes(m_display, m_subMenuWindow[k],
				    CWSaveUnder, &attr);
	}
    }

    XGCValues values;
    values.background = m_menuBackgroundPixel;
    values.foreground = m_menuForegroundPixel ^ m_menuBackgroundPixel;
    values.function = GXxor;
    values.line_width = 0;
    values.subwindow_mode = IncludeInferiors;

    m_menuGC = XCreateGC
	(display(), root(), GCForeground | GCBackground |
	 GCFunction | GCLineWidth | GCSubwindowMode, &values);

    // Xft menu font — supports modern TTFs (e.g. "Hack-11" or
    // "Hack:size=11:antialias=true").
    m_menuFont = XftFontOpenName(display(), m_screenNumber,
                                 CONFIG_NICE_MENU_FONT);
    if (!m_menuFont) m_menuFont = XftFontOpenName(display(), m_screenNumber,
                                                  "Sans-11");
    if (!m_menuFont) fatal("couldn't load any menu font\n");

    Visual   *vis  = DefaultVisual(display(), m_screenNumber);
    Colormap  cmap = m_defaultColormap;
    m_menuDraw = XftDrawCreate(display(), m_menuWindow, vis, cmap);
    for (int k = 0; k < MENU_DEPTH - 1; ++k) {
	m_subMenuDraw[k] = XftDrawCreate(display(), m_subMenuWindow[k], vis, cmap);
    }

    XRenderColor rc;
    rc.red   = ((m_menuForegroundPixel >> 16) & 0xff) * 0x101;
    rc.green = ((m_menuForegroundPixel >>  8) & 0xff) * 0x101;
    rc.blue  = ( m_menuForegroundPixel        & 0xff) * 0x101;
    rc.alpha = 0xffff;
    // Use the named color route to ensure proper allocation regardless
    // of the menu pixel's encoding under odd visuals.
    if (!XftColorAllocName(display(), vis, cmap,
                           CONFIG_MENU_FOREGROUND, &m_menuXftColor)) {
        XftColorAllocValue(display(), vis, cmap, &rc, &m_menuXftColor);
    }

    // EWMH: announce desktop support on the root window
    Atom supported[] = {
	Atoms::net_number_of_desktops, Atoms::net_current_desktop,
	Atoms::net_wm_desktop, Atoms::net_client_list,
	Atoms::net_client_list_stacking, Atoms::net_active_window,
	Atoms::net_supporting_wm_check, Atoms::net_wm_name,
	Atoms::net_wm_window_type, Atoms::net_wm_window_type_dock,
	Atoms::net_wm_strut, Atoms::net_wm_strut_partial,
	Atoms::net_wm_state, Atoms::net_wm_state_maximized_vert,
	Atoms::net_wm_state_maximized_horz, Atoms::net_wm_state_fullscreen,
    };
    XChangeProperty(m_display, m_root, Atoms::net_supported, XA_ATOM, 32,
		    PropModeReplace, (unsigned char *)supported,
		    sizeof(supported) / sizeof(supported[0]));

    // _NET_SUPPORTING_WM_CHECK: libwnck (panel tasklist/pager) ignores
    // EWMH hints unless this child window exists.
    m_wmCheck = XCreateSimpleWindow(m_display, m_root, -1, -1, 1, 1, 0, 0, 0);
    XChangeProperty(m_display, m_root, Atoms::net_supporting_wm_check,
		    XA_WINDOW, 32, PropModeReplace,
		    (unsigned char *)&m_wmCheck, 1);
    XChangeProperty(m_display, m_wmCheck, Atoms::net_supporting_wm_check,
		    XA_WINDOW, 32, PropModeReplace,
		    (unsigned char *)&m_wmCheck, 1);
    XChangeProperty(m_display, m_wmCheck, Atoms::net_wm_name,
		    Atoms::utf8_string, 8, PropModeReplace,
		    (unsigned char *)"wm2tj", 5);
    updateClientList();

    // ICCCM WM_S0 (xfce4-panel's "is a WM running" check)
    char selName[32];
    snprintf(selName, sizeof(selName), "WM_S%d", m_screenNumber);
    XSetSelectionOwner(m_display, XInternAtom(m_display, selName, False),
		       m_wmCheck, CurrentTime);

    publishDesktops();
}


void WindowManager::publishDesktops()
{
    // _NET_DESKTOP_NAMES "1".."N" so the panel pager shows short labels
    char names[16 * 4];
    int len = 0;
    for (int i = 0; i < m_numDesktops; ++i)
	len += sprintf(names + len, "%d", i + 1) + 1;
    XChangeProperty(m_display, m_root,
		    XInternAtom(m_display, "_NET_DESKTOP_NAMES", False),
		    Atoms::utf8_string, 8, PropModeReplace,
		    (unsigned char *)names, len);

    unsigned long nd = (unsigned long)m_numDesktops;
    XChangeProperty(m_display, m_root, Atoms::net_number_of_desktops,
		    XA_CARDINAL, 32, PropModeReplace,
		    (unsigned char *)&nd, 1);

    updateNetCurrentDesktop();
}


void WindowManager::updateNetCurrentDesktop()
{
    unsigned long cd = (unsigned long)m_currentDesktop;
    XChangeProperty(m_display, m_root, Atoms::net_current_desktop,
		    XA_CARDINAL, 32, PropModeReplace,
		    (unsigned char *)&cd, 1);
}



// GNOME-like dynamic desktops (dynamic_desktops on): there is always exactly
// one empty desktop at the end, and empty desktops other than the current
// one are removed, but never below "desktops" (wm2config.num_desktops).
// Minimised windows count as being on their desktop.
void WindowManager::updateDynamicDesktops()
{
    if (!wm2config.dynamic_desktops) return;

    enum { MaxDesktops = 16 };
    Boolean used[MaxDesktops] = { False };
    int n = m_numDesktops;
    if (n > MaxDesktops) n = MaxDesktops;

    for (int i = 0; i < m_clients.count(); ++i) {
	Client *c = m_clients.item(i);
	if (c->isWithdrawn() || c->window() == None || c->isSticky()) continue;
	int d = c->desktop();
	if (d < 0) d = 0;
	if (d >= n) d = n - 1;
	used[d] = True;
    }

    // keep used desktops and the current one; renumber the rest away
    int map[MaxDesktops];
    int k = 0;
    for (int d = 0; d < n; ++d) {
	if (used[d] || d == m_currentDesktop) map[d] = k++;
	else map[d] = -1;
    }
    // ...then make sure the last one is empty
    int last = -1;
    for (int d = n - 1; d >= 0; --d) if (map[d] >= 0) { last = d; break; }
    if (last < 0 || used[last]) {
	if (k < MaxDesktops) ++k;
    }
    if (k < 1) k = 1;
    // never fewer than "desktops" in ~/.wm2rc (so there are 3 right after login)
    if (k < wm2config.num_desktops) k = wm2config.num_desktops;
    if (k > MaxDesktops) k = MaxDesktops;

    Boolean changed = (k != m_numDesktops);
    for (int d = 0; d < n; ++d) if (map[d] >= 0 && map[d] != d) changed = True;
    if (!changed) return;

    for (int i = 0; i < m_clients.count(); ++i) {
	Client *c = m_clients.item(i);
	if (c->isSticky()) continue;
	int d = c->desktop();
	if (d < 0) d = 0;
	if (d >= n) d = n - 1;
	if (map[d] >= 0) c->setDesktop(map[d]);
    }
    m_currentDesktop = map[m_currentDesktop] >= 0 ? map[m_currentDesktop] : 0;
    m_numDesktops = k;
    publishDesktops();
    updateClientList();
}


void WindowManager::switchToDesktop(int d)
{
    if (d < 0) d = 0;
    if (d >= m_numDesktops) d = m_numDesktops - 1;
    if (d == m_currentDesktop) return;

    int old = m_currentDesktop;
    m_currentDesktop = d;

    for (int i = 0; i < m_clients.count(); ++i) {
	Client *c = m_clients.item(i);
	if (!c->isNormal()) continue;
	if (c->isSticky()) continue;
	if (c->desktop() == old && !c->isOffDesktop()) {
	    c->hideForDesktop();
	}
    }
    for (int i = 0; i < m_clients.count(); ++i) {
	Client *c = m_clients.item(i);
	if (!c->isNormal()) continue;
	if (c->isSticky()) {
	    if (c->isOffDesktop()) c->unhideForDesktop();
	    continue;
	}
	if (c->desktop() == d && c->isOffDesktop()) {
	    c->unhideForDesktop();
	}
    }

    updateNetCurrentDesktop();
}


unsigned long WindowManager::allocateColour(const char *name, const char *desc)
{
    XColor nearest, ideal;

    if (!XAllocNamedColor
	(display(), DefaultColormap(display(), m_screenNumber), name,
	 &nearest, &ideal)) {

	char error[100];
	sprintf(error, "couldn't load %s colour", desc);
	fatal(error);

    } else return nearest.pixel;
}


void WindowManager::installCursor(RootCursor c)
{
    installCursorOnWindow(c, m_root);
}


void WindowManager::installCursorOnWindow(RootCursor c, Window w)
{
    XSetWindowAttributes attr;

    switch (c) {
    case DeleteCursor:    attr.cursor = m_xCursor;  break;
    case DownCursor:      attr.cursor = m_vCursor;  break;
    case RightCursor:     attr.cursor = m_hCursor;  break;
    case DownrightCursor: attr.cursor = m_vhCursor; break;
    case NormalCursor:    attr.cursor = m_cursor;   break;
    }

    XChangeWindowAttributes(m_display, w, CWCursor, &attr);
}
	

Time WindowManager::timestamp(Boolean reset)
{
    if (reset) m_currentTime = CurrentTime;

    if (m_currentTime == CurrentTime) {

	XEvent event;
	XChangeProperty(m_display, m_root, Atoms::wm2_running,
			Atoms::wm2_running, 8, PropModeAppend,
			(unsigned char *)"", 0);
	XMaskEvent(m_display, PropertyChangeMask, &event);

	m_currentTime = event.xproperty.time;
    }

    return m_currentTime;
}

void WindowManager::sigHandler(int)
{
    m_signalled = True;
}

void WindowManager::scanInitialWindows()
{
    unsigned int i, n;
    Window w1, w2, *wins;
    XWindowAttributes attr;

    XQueryTree(m_display, m_root, &w1, &w2, &wins, &n);

    for (i = 0; i < n; ++i) {

	XGetWindowAttributes(m_display, wins[i], &attr);
	if (attr.override_redirect || wins[i] == m_menuWindow) continue;
	if (isMenuWindow(wins[i])) continue;	// a cascading submenu
	if (wins[i] == m_wmCheck) continue;

	// after a restart the panel etc. are already mapped: re-adopt them
	// as unmanaged instead of framing them
	Boolean isDock;
	if (isUnmanagedType(wins[i], &isDock)) {
	    if (isDock && attr.map_state == IsViewable && m_dockCount < 32) {
		m_docks[m_dockCount++] = wins[i];
		XSelectInput(m_display, wins[i],
			     PropertyChangeMask | StructureNotifyMask);
	    }
	    continue;
	}

	(void)windowToClient(wins[i], True);
    }

    XFree((void *)wins);
    recomputeStruts();
    updateClientList();
}

Client *WindowManager::windowToClient(Window w, Boolean create)
{
    if (w == 0) return 0;

    for (int i = m_clients.count()-1; i >= 0; --i) {

	if (m_clients.item(i)->hasWindow(w)) {
	    return m_clients.item(i);
	}
    }

    if (!create) return 0;
    else {
	Client *newC = new Client(this, w);
	m_clients.append(newC);
	return newC;
    }
}

void WindowManager::installColormap(Colormap cmap)
{
    if (cmap == None) {
	XInstallColormap(m_display, m_defaultColormap);
    } else {
	XInstallColormap(m_display, cmap);
    }
}

void WindowManager::clearFocus()
{
    static Window w = 0;
    Client *active = activeClient();

    if (CONFIG_AUTO_RAISE || !CONFIG_CLICK_TO_FOCUS) {
	setActiveClient(0);
	return;
    }

    if (active) {

	setActiveClient(0);
	active->deactivate();

	for (Client *c = active->revertTo(); c; c = c->revertTo()) {
	    if (c->isNormal() && !c->isOffDesktop()) {
		c->activate();
		return;
	    }
	}

	installColormap(None);
    }

    if (w == 0) {

	XSetWindowAttributes attr;
	int mask = CWOverrideRedirect;
	attr.override_redirect = 1;

	w = XCreateWindow(display(), root(), 0, 0, 1, 1, 0,
			  CopyFromParent, InputOnly, CopyFromParent,
			  mask, &attr);

	XMapWindow(display(), w);
    }

    XSetInputFocus(display(), w, RevertToPointerRoot, timestamp(False));
}


void WindowManager::skipInRevert(Client *c, Client *myRevert)
{
    for (int i = 0; i < m_clients.count(); ++i) {
	if (m_clients.item(i) != c &&
	    m_clients.item(i)->revertTo() == c) {
	    m_clients.item(i)->setRevertTo(myRevert);
	}
    }
}


void WindowManager::addToHiddenList(Client *c)
{
    for (int i = 0; i < m_hiddenClients.count(); ++i) {
	if (m_hiddenClients.item(i) == c) return;
    }

    m_hiddenClients.append(c);
}


void WindowManager::removeFromHiddenList(Client *c)
{
    for (int i = 0; i < m_hiddenClients.count(); ++i) {
	if (m_hiddenClients.item(i) == c) {
	    m_hiddenClients.remove(i);
	    return;
	}
    }
}


Boolean WindowManager::raiseTransients(Client *c)
{
    Client *first = 0;

    if (!c->isNormal()) return False;

    for (int i = 0; i < m_clients.count(); ++i) {

	if (m_clients.item(i)->isNormal() &&
	    m_clients.item(i)->isTransient()) {

	    if (c->hasWindow(m_clients.item(i)->transientFor())) {

		if (!first) first = m_clients.item(i);
		else m_clients.item(i)->mapRaised();
	    }
	}
    }

    if (first) {
	first->mapRaised();
	return True;
    } else {
	return False;
    }
}

#ifdef sgi
extern "C" {
extern int putenv(char *);	/* not POSIX */
}
#endif

void WindowManager::spawn()
{
    // strange code thieved from 9wm to avoid leaving zombies

    char *displayName = DisplayString(m_display);

    pid_t child = fork();
    if (child == 0) {
	if (fork() == 0) {

	    close(ConnectionNumber(m_display));

	    // if you don't have putenv, miss out this next
	    // conditional and its contents

	    if (displayName && (displayName[0] != '\0')) {

		char *pstring = (char *)malloc(strlen(displayName) + 10);
		sprintf(pstring, "DISPLAY=%s", displayName);
		putenv(pstring);
	    }

	    // new_window from ~/.wm2rc if set and its program is installed;
	    // otherwise $TERMINAL, the distribution's default (Debian's
	    // x-terminal-emulator) and the usual terminals, xterm last.
	    // Always through /bin/sh ($SHELL may be fish/csh with other syntax).
	    static const char *script =
		"cmd=$1; set -- $cmd; "
		"if [ -n \"$1\" ] && command -v \"$1\" >/dev/null 2>&1; then eval \"exec $cmd\"; fi; "
		"for t in $TERMINAL x-terminal-emulator xfce4-terminal gnome-terminal "
		"konsole mate-terminal lxterminal qterminal alacritty kitty urxvt xterm; do "
		"command -v \"$t\" >/dev/null 2>&1 && exec \"$t\"; done; "
		"echo \"wm2tj: new_window: $cmd: not found, nor any terminal\" >&2; exit 127";
	    execl("/bin/sh", "sh", "-c", script, "sh", CONFIG_NEW_WINDOW_COMMAND, (char *)NULL);
	    perror("wm2tj: exec /bin/sh failed");
	    exit(1);
	}
	exit(0);
    }
    // reap exactly our intermediate child (wm2 also inherits the session
    // script's background jobs, so a bare wait() can pick the wrong one)
    if (child > 0) waitpid(child, 0, 0);
}


void WindowManager::spawn(const char *command)
{
    if (!command || !*command) return;

    char *displayName = DisplayString(m_display);

    pid_t child = fork();
    if (child == 0) {
	if (fork() == 0) {

	    close(ConnectionNumber(m_display));

	    if (displayName && (displayName[0] != '\0')) {
		char *pstring = (char *)malloc(strlen(displayName) + 10);
		sprintf(pstring, "DISPLAY=%s", displayName);
		putenv(pstring);
	    }

	    execl("/bin/sh", "sh", "-c", command, (char *)NULL);
	    fprintf(stderr, "wm2tj: exec sh -c \"%s\"", command);
	    perror(" failed");
	    exit(1);
	}
	exit(0);
    }
    // reap exactly our intermediate child (wm2 also inherits the session
    // script's background jobs, so a bare wait() can pick the wrong one)
    if (child > 0) waitpid(child, 0, 0);
}




// ---------------------------------------------------------------------
// EWMH / xfce4-panel support (2026-10-02)

void WindowManager::updateClientList()
{
    int n = m_clients.count();
    Window *list = (Window *)malloc(sizeof(Window) * (n + 1));
    int k = 0;

    for (int i = 0; i < n; ++i) {
	Client *c = m_clients.item(i);
	if (c->isWithdrawn() || c->window() == None) continue;
	list[k++] = c->window();
	unsigned long d = c->isSticky() ? 0xFFFFFFFFUL
	                                : (unsigned long)c->desktop();
	XChangeProperty(m_display, c->window(), Atoms::net_wm_desktop,
			XA_CARDINAL, 32, PropModeReplace,
			(unsigned char *)&d, 1);
    }

    XChangeProperty(m_display, m_root, Atoms::net_client_list, XA_WINDOW,
		    32, PropModeReplace, (unsigned char *)list, k);
    XChangeProperty(m_display, m_root, Atoms::net_client_list_stacking,
		    XA_WINDOW, 32, PropModeReplace, (unsigned char *)list, k);
    free(list);
}

void WindowManager::updateActiveWindow()
{
    Window w = (m_activeClient && m_activeClient->window() != None)
	? m_activeClient->window() : None;
    XChangeProperty(m_display, m_root, Atoms::net_active_window, XA_WINDOW,
		    32, PropModeReplace, (unsigned char *)&w, 1);
}

// Panels, desktops, notifications, splashes and menus are mapped as-is
// without a wm2 frame.
Boolean WindowManager::isUnmanagedType(Window w, Boolean *isDock)
{
    Atom type;
    int format;
    unsigned long n, after;
    unsigned char *data = 0;
    Boolean result = False;

    *isDock = False;
    if (XGetWindowProperty(m_display, w, Atoms::net_wm_window_type, 0, 16,
			   False, XA_ATOM, &type, &format, &n, &after,
			   &data) != Success || !data) return False;

    Atom *a = (Atom *)data;
    for (unsigned long i = 0; i < n; ++i) {
	if (a[i] == Atoms::net_wm_window_type_dock) {
	    *isDock = True; result = True;
	} else if (a[i] == Atoms::net_wm_window_type_desktop ||
		   a[i] == Atoms::net_wm_window_type_notification ||
		   a[i] == Atoms::net_wm_window_type_splash ||
		   a[i] == Atoms::net_wm_window_type_tooltip ||
		   a[i] == Atoms::net_wm_window_type_popup_menu ||
		   a[i] == Atoms::net_wm_window_type_dropdown_menu ||
		   a[i] == Atoms::net_wm_window_type_menu) {
	    // MENU: torn-off/standalone menus such as the panel's Whisker Menu
	    result = True;
	}
    }
    XFree(data);
    return result;
}

void WindowManager::mapUnmanaged(Window w, Boolean isDock)
{
    // Drop the placeholder Client created at CreateNotify time; it was
    // never reparented, so releasing it touches nothing on screen.
    for (int i = m_clients.count()-1; i >= 0; --i) {
	Client *c = m_clients.item(i);
	if (c->window() == w) {
	    m_clients.remove(i);
	    c->release();
	    break;
	}
    }

    XSelectInput(m_display, w, PropertyChangeMask | StructureNotifyMask);
    XMapRaised(m_display, w);

    // Menus with a search box (Whisker Menu) need keyboard focus, and
    // close themselves on focus-out if they never get it.
    if (!isDock) {
	XWMHints *h = XGetWMHints(m_display, w);
	Boolean wantsInput = (!h || !(h->flags & InputHint) || h->input);
	if (h) XFree(h);
	if (wantsInput) {
	    ignoreBadWindowErrors = True;
	    XSetInputFocus(m_display, w, RevertToPointerRoot, CurrentTime);
	    XSync(m_display, False);
	    ignoreBadWindowErrors = False;
	}
    }

    if (isDock) {
	int i;
	for (i = 0; i < m_dockCount; ++i) if (m_docks[i] == w) break;
	if (i == m_dockCount && m_dockCount < 32) m_docks[m_dockCount++] = w;
	recomputeStruts();
    }
    updateClientList();
}

void WindowManager::forgetDock(Window w)
{
    for (int i = 0; i < m_dockCount; ++i) {
	if (m_docks[i] == w) {
	    m_docks[i] = m_docks[--m_dockCount];
	    recomputeStruts();
	    return;
	}
    }
}

void WindowManager::raiseDocks()
{
    for (int i = 0; i < m_dockCount; ++i) XRaiseWindow(m_display, m_docks[i]);
}

void WindowManager::recomputeStruts()
{
    int prevTop = m_strutTop, prevBottom = m_strutBottom;
    m_strutTop = m_strutBottom = 0;

    for (int i = 0; i < m_dockCount; ++i) {
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = 0;

	if ((XGetWindowProperty(m_display, m_docks[i],
				Atoms::net_wm_strut_partial, 0, 12, False,
				XA_CARDINAL, &type, &format, &n, &after,
				&data) == Success && data && n >= 4) ||
	    (data ? (XFree(data), data = 0) : 0,
	     XGetWindowProperty(m_display, m_docks[i], Atoms::net_wm_strut,
				0, 4, False, XA_CARDINAL, &type, &format, &n,
				&after, &data) == Success && data && n >= 4)) {
	    long *s = (long *)data;
	    if (s[2] > m_strutTop)    m_strutTop = (int)s[2];
	    if (s[3] > m_strutBottom) m_strutBottom = (int)s[3];
	}
	if (data) XFree(data);
    }

    if (m_strutTop != prevTop || m_strutBottom != prevBottom) {
	for (int i = 0; i < m_clients.count(); ++i) {
	    Client *c = m_clients.item(i);
	    if (c->isMaximized()) c->applyStateGeometry();
	}
    }
}
