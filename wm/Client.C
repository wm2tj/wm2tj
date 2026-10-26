
#include "Manager.h"
#include "Client.h"

#include <X11/Xutil.h>

const char *const Client::m_defaultLabel = "incognito";



Client::Client(WindowManager *const wm, Window w) :
    m_window(w),
    m_transient(None),
    m_revert(0),
    m_fixedSize(False),
    m_state(WithdrawnState),
    m_managed(False),
    m_reparenting(False),
    m_stubborn(False),
    m_lastPopTime(0L),
    m_desktop(wm->currentDesktop()),
    m_offDesktop(False),
    m_adoptOffDesktop(False),
    m_ignoreUnmaps(0),
    m_sticky(False),
    m_maxH(False),
    m_maxV(False),
    m_fullscreen(False),
    m_haveRestore(False),
    m_colormap(None),
    m_colormapWinCount(0),
    m_colormapWindows(NULL),
    m_windowColormaps(NULL),
    m_windowManager(wm)
{
    XWindowAttributes attr;
    XGetWindowAttributes(display(), m_window, &attr);

    m_x = attr.x;
    m_y = attr.y;
    m_w = attr.width;
    m_h = attr.height;
    m_bw = attr.border_width;
    m_name = m_iconName = 0;
    m_sizeHints.flags = 0L;

    m_label = NewString(m_defaultLabel);
    m_border = new Border(this, w);

    if (attr.map_state == IsViewable) manage(True);
    else {
	// left unmapped by a restarting wm2 because it was on another desktop
	Atom type; int format; unsigned long n, after;
	unsigned char *data = 0;
	if (XGetWindowProperty(display(), m_window, Atoms::wm2_off_desktop,
			       0, 1, True, XA_CARDINAL, &type, &format, &n,
			       &after, &data) == Success && data && n == 1) {
	    m_adoptOffDesktop = True;
	    manage(True);
	    m_adoptOffDesktop = False;
	}
	if (data) XFree(data);
    }
}


Client::~Client()
{
    // empty
}    


void Client::release()
{
    // assume wm called for this, and will remove me from its list itself

//    fprintf(stderr, "deleting client %p\n",this);

    if (m_window == None) {
	fprintf(stderr,
		"wm2tj: invalid parent in Client::release (released twice?)\n");
    }

    windowManager()->skipInRevert(this, m_revert);

//    fprintf(stderr, "deleting %lx\n",m_window);

    if (isHidden()) unhide(False);

    delete m_border;
    m_window = None;

    if (isActive()) {
	if (CONFIG_CLICK_TO_FOCUS) {
	    if (m_revert && !m_revert->isOffDesktop() && !m_revert->isHidden()) {
		// clear first: activate() would skip XSetInputFocus if the
		// revert window were already marked active
		windowManager()->setActiveClient(0);
		m_revert->activate();
	    } else windowManager()->setActiveClient(0);// windowManager()->clearFocus();
	} else {
	    windowManager()->setActiveClient(0);
	}
    }

    if (m_colormapWinCount > 0) {
	XFree((char *)m_colormapWindows);
	free((char *)m_windowColormaps); // not allocated through X
    }

    if (m_iconName) XFree(m_iconName);
    if (m_name)     XFree(m_name);
    if (m_label) free((void *)m_label);

    delete this;
}


void Client::unreparent()
{
    XWindowChanges wc;

    if (!isWithdrawn()) {
	gravitate(True);
	XReparentWindow(display(), m_window, root(), m_x, m_y);
    }

    wc.border_width = m_bw;
    XConfigureWindow(display(), m_window, CWBorderWidth, &wc);

    XSync(display(), True);
}


void Client::installColormap()
{
    Client *cc = 0;
    int i, found;

    if (m_colormapWinCount != 0) {

	found = 0;

	for (i = m_colormapWinCount - 1; i >= 0; --i) {
	    windowManager()->installColormap(m_windowColormaps[i]);
	    if (m_colormapWindows[i] == m_window) ++found;
	}

	if (found == 0) {
	    windowManager()->installColormap(m_colormap);
	}

    } else if (m_transient != None &&
	       (cc = windowManager()->windowToClient(m_transient))) {

	cc->installColormap();
    } else {
	windowManager()->installColormap(m_colormap);
    }
}


void Client::manage(Boolean mapped)
{
    Boolean shouldHide, reshape;
    XWMHints *hints;
    Display *d = display();
    long mSize;
    int state;

    XSelectInput(d, m_window, ColormapChangeMask | EnterWindowMask |
		 PropertyChangeMask | FocusChangeMask);

    m_iconName = getProperty(XA_WM_ICON_NAME);
    m_name = getProperty(XA_WM_NAME);
    setLabel();

    getColormaps();
    getProtocols();
    getTransient();

    hints = XGetWMHints(d, m_window);

    if (!getState(&state)) {
	state = hints ? hints->initial_state : NormalState;
    }

    shouldHide = (state == IconicState);
    if (hints) XFree(hints);

    if (XGetWMNormalHints(d, m_window, &m_sizeHints, &mSize) == 0 ||
	m_sizeHints.flags == 0) {
	m_sizeHints.flags = PSize;
    }

    m_fixedSize = False;
//    if ((m_sizeHints.flags & (USSize | PSize))) m_fixedSize = True;
    if ((m_sizeHints.flags & (PMinSize | PMaxSize)) == (PMinSize | PMaxSize) &&
	(m_sizeHints.min_width  == m_sizeHints.max_width &&
	 m_sizeHints.min_height == m_sizeHints.max_height)) m_fixedSize = True;

    reshape = !mapped;

    if (m_fixedSize) {
	if ((m_sizeHints.flags & USPosition)) reshape = False;
	if ((m_sizeHints.flags & PPosition) && shouldHide) reshape = False;
	if ((m_transient != None)) reshape = False;
    }

    if ((m_sizeHints.flags & PBaseSize)) {
	m_minWidth  = m_sizeHints.base_width;
	m_minHeight = m_sizeHints.base_height;
    } else if ((m_sizeHints.flags & PMinSize)) {
	m_minWidth  = m_sizeHints.min_width;
	m_minHeight = m_sizeHints.min_height;
    } else {
	m_minWidth = m_minHeight = 50;
    }

    // act

    gravitate(False);

    // A new window the application did not place goes to the monitor the
    // pointer is on, at the same spot it would have had on the first one;
    // any window is then kept within one monitor.
    const WindowManager::Monitor *mon =
	&windowManager()->monitorFor(m_x, m_y, m_w, m_h);
    if (!mapped && m_transient == None &&
	!(m_sizeHints.flags & (USPosition | PPosition))) {
	const WindowManager::Monitor &pm = windowManager()->pointerMonitor();
	if (&pm != mon) {
	    m_x += pm.x - mon->x;
	    m_y += pm.y - mon->y;
	    mon = &pm;
	}
    }
    int dx0 = mon->x, dy0 = mon->y;
    int dw = mon->x + mon->w, dh = mon->y + mon->h;

    if (m_w < m_minWidth) {
	m_w = m_minWidth; m_fixedSize = False; reshape = True;
    }
    if (m_h < m_minHeight) {
	m_h = m_minHeight; m_fixedSize = False; reshape = True;
    }

    if (m_w > mon->w - 8) m_w = mon->w - 8;
    if (m_h > mon->h - 8) m_h = mon->h - 8;

    if (m_x > dw - m_border->xIndent()) {
	m_x = dw - m_border->xIndent();
    }

    if (m_y > dh - m_border->yIndent()) {
	m_y = dh - m_border->yIndent();
    }

    if (m_x < dx0 + m_border->xIndent()) m_x = dx0 + m_border->xIndent();
    if (m_y < dy0 + m_border->yIndent()) m_y = dy0 + m_border->yIndent();
    if (m_y < minY()) m_y = minY();

    m_border->configure(m_x, m_y, m_w, m_h, 0L, Above);

    if (mapped && !m_adoptOffDesktop) m_reparenting = True;
    if (reshape && !m_fixedSize) XResizeWindow(d, m_window, m_w, m_h);
    XSetWindowBorderWidth(d, m_window, 0);

    m_border->reparent();

    // Focus follows the pointer, so the window under it is already active
    // and its clicks go straight to the app.  Catch buttons 1-3 on the
    // client window (sync) so a click raises it; eventButton replays them.
    // The wheel is left alone so scrolling doesn't raise.
    for (unsigned int b = Button1; b <= Button3; ++b) {
	XGrabButton(d, b, AnyModifier, m_window, False,
		    ButtonPressMask | ButtonReleaseMask,
		    GrabModeSync, GrabModeAsync, None, None);
    }

    readNetState();
    if (m_maxH || m_maxV || m_fullscreen) {
	if (m_fixedSize) m_maxH = m_maxV = False;
	applyStateGeometry();
    }

    // (support for shaped windows absent)

    XAddToSaveSet(d, m_window);
    m_managed = True;

    if (shouldHide) hide();
    else if (m_adoptOffDesktop) {
	// stays unmapped; the desktop is restored below
	setState(NormalState);
	deactivate();
    } else {
	XMapWindow(d, m_window);
	m_border->map();
	setState(NormalState);

	if (CONFIG_CLICK_TO_FOCUS ||
	    (m_transient != None && activeClient() &&
	    activeClient()->m_window == m_transient)) {
	    activate();
	    mapRaised();
	} else {
	    deactivate();
	}
    }
    
    if (mapped && !shouldHide) {
	// Restarted wm2: put the window back on the desktop it was on.
	Atom type; int format; unsigned long n, after;
	unsigned char *data = 0;
	if (XGetWindowProperty(d, m_window, Atoms::net_wm_desktop, 0, 1, False,
			       XA_CARDINAL, &type, &format, &n, &after,
			       &data) == Success && data && n == 1) {
	    unsigned long v = *(unsigned long *)data;
	    if (v == 0xFFFFFFFFUL) setSticky(True);
	    else {
		int dsk = (int)v;
		if (dsk >= windowManager()->numDesktops())
		    dsk = windowManager()->numDesktops() - 1;
		m_desktop = dsk;
		if (dsk != windowManager()->currentDesktop()) {
		    if (m_adoptOffDesktop) m_offDesktop = True;
		    else hideForDesktop();
		}
	    }
	}
	if (data) XFree(data);
	if (m_adoptOffDesktop && !m_offDesktop) {
	    // its desktop is the current one after all: show it
	    XMapWindow(d, m_window);
	    m_border->map();
	}
    }

    if (activeClient() && !isActive()) {
	activeClient()->installColormap();
    }

    if (CONFIG_AUTO_RAISE) {
	m_windowManager->stopConsideringFocus();
	focusIfAppropriate(False);
    }
}


void Client::selectOnMotion(Window w, Boolean select)
{
    if (!CONFIG_AUTO_RAISE) return;
    if (!w || w == root()) return;

    if (w == m_window || m_border->hasWindow(w)) {
	XSelectInput(display(), m_window, // not "w"
		     ColormapChangeMask | EnterWindowMask |
		     PropertyChangeMask | FocusChangeMask |
		     (select ? PointerMotionMask : 0L));
    } else {
	XSelectInput(display(), w, select ? PointerMotionMask : 0L);
    }
}


void Client::decorate(Boolean active)
{
    m_border->decorate(active, m_w, m_h);
}


void Client::activate()
{
//    fprintf(stderr, "Client::activate (this = %p, window = %x, parent = %x)\n",
//	    this, m_window, parent());

    if (parent() == root()) {
	fprintf(stderr, "wm2tj: warning: bad parent in Client::activate\n");
	return;
    }

    // A window hidden for another desktop is unmapped: X refuses the focus
    // (BadMatch) but wm2 would still believe it active, and a later click
    // on it would skip XSetInputFocus -- the keyboard then stays dead.
    if (!m_managed || isHidden() || isWithdrawn() || m_offDesktop) return;

    if (isActive()) {
	decorate(True);
	if (CONFIG_AUTO_RAISE || CONFIG_RAISE_ON_FOCUS) mapRaised();
	return;
    }

    if (activeClient()) {
	activeClient()->deactivate();
	// & some other-screen business
    }

    XUngrabButton(display(), AnyButton, AnyModifier, parent());

    XSetInputFocus(display(), m_window, RevertToPointerRoot,
		   windowManager()->timestamp(False));

    if (m_protocol & PtakeFocus) {
	sendMessage(Atoms::wm_protocols, Atoms::wm_takeFocus);
    }

    // now set revert of window that reverts to this one so as to
    // revert to the window this one used to revert to (huh?)

    windowManager()->skipInRevert(this, m_revert);

    m_revert = activeClient();
    while (m_revert && !m_revert->isNormal()) m_revert = m_revert->revertTo();

    windowManager()->setActiveClient(this);
//    if (CONFIG_AUTO_RAISE || CONFIG_RAISE_ON_FOCUS) mapRaised();
    decorate(True);

    installColormap();		// new!
}


void Client::deactivate()	// called from wm?
{
//    fprintf(stderr, 
//	    "Client::deactivate (this = %p, window = %x, parent = %x)\n",
//	    this, m_window, parent());

    if (parent() == root()) {
	fprintf(stderr, "wm2tj: warning: bad parent in Client::deactivate\n");
	return;
    }

    XGrabButton(display(), AnyButton, AnyModifier, parent(), False,
		ButtonPressMask | ButtonReleaseMask,
		GrabModeAsync, GrabModeSync, None, None);

    decorate(False);
}


void Client::sendMessage(Atom a, long l)
{
    XEvent ev;
    int status;
    long mask;

    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = m_window;
    ev.xclient.message_type = a;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = l;
    ev.xclient.data.l[1] = windowManager()->timestamp(False);
    mask = 0L;
    status = XSendEvent(display(), m_window, False, mask, &ev);

    if (status == 0) {
	fprintf(stderr, "wm2tj: warning: Client::sendMessage failed\n");
    }
}


static int getProperty_aux(Display *d, Window w, Atom a, Atom type, long len,
			   unsigned char **p)
{
    Atom realType;
    int format;
    unsigned long n, extra;
    int status;

    status = XGetWindowProperty(d, w, a, 0L, len, False, type, &realType,
				&format, &n, &extra, p);

    if (status != Success || *p == 0) return -1;
    if (n == 0) XFree((void *) *p);

    return n;
}


char *Client::getProperty(Atom a)
{
    unsigned char *p;

    // Prefer the EWMH UTF-8 title (Japanese etc.); WM_NAME is often
    // COMPOUND_TEXT, which the XA_STRING read below rejects.
    Atom utf = 0;
    if (a == XA_WM_NAME)
	utf = XInternAtom(display(), "_NET_WM_NAME", False);
    else if (a == XA_WM_ICON_NAME)
	utf = XInternAtom(display(), "_NET_WM_ICON_NAME", False);
    if (utf && getProperty_aux(display(), m_window, utf,
			       Atoms::utf8_string, 256L, &p) > 0) {
	return (char *)p;
    }

    if (getProperty_aux(display(), m_window, a, XA_STRING, 100L, &p) <= 0) {
	return NULL;
    }
    return (char *)p;
}


void Client::setState(int state)
{
    m_state = state;

//    fprintf(stderr, "state set to %d\n",state);

    long data[2];
    data[0] = (long)state;
    data[1] = (long)None;

    XChangeProperty(display(), m_window, Atoms::wm_state, Atoms::wm_state,
		    32, PropModeReplace, (unsigned char *)data, 2);
}


Boolean Client::getState(int *state)
{
    long *p = 0;

    if (getProperty_aux(display(), m_window, Atoms::wm_state, Atoms::wm_state,
			2L, (unsigned char **)&p) <= 0) {
	return False;
    }

    *state = (int) *p;
    XFree((char *)p);
    return True;
}


void Client::getProtocols()
{
    long n;
    Atom *p;

    m_protocol = 0;
    if ((n = getProperty_aux(display(), m_window, Atoms::wm_protocols, XA_ATOM,
			     20L, (unsigned char **)&p)) <= 0) {
	return;
    }

    for (int i = 0; i < n; ++i) {
	if (p[i] == Atoms::wm_delete) {
	    m_protocol |= Pdelete;
	} else if (p[i] == Atoms::wm_takeFocus) {
	    m_protocol |= PtakeFocus;
	}
    }

    XFree((char *) p);
}


void Client::gravitate(Boolean invert)
{
    int gravity;
    int w = 0, h = 0, xdelta, ydelta;

    // possibly shouldn't work if we haven't been managed yet?

    gravity = NorthWestGravity;
    if (m_sizeHints.flags & PWinGravity) gravity = m_sizeHints.win_gravity;

    xdelta = m_bw - m_border->xIndent();
    ydelta = m_bw - m_border->yIndent();

    // note that right and bottom borders have indents of 1

    switch (gravity) {

    case NorthWestGravity:
	break;

    case NorthGravity:
	w = xdelta;
	break;

    case NorthEastGravity:
	w = xdelta + m_bw-1;
	break;

    case WestGravity:
	h = ydelta;
	break;

    case CenterGravity:
    case StaticGravity:
	w = xdelta;
	h = ydelta;
	break;

    case EastGravity:
	w = xdelta + m_bw-1;
	h = ydelta;
	break;

    case SouthWestGravity:
	h = ydelta + m_bw-1;
	break;

    case SouthGravity:
	w = xdelta;
	h = ydelta + m_bw-1;
	break;

    case SouthEastGravity:
	w = xdelta + m_bw-1;
	h = ydelta + m_bw-1;
	break;

    default:
	fprintf(stderr, "wm2tj: bad window gravity %d for window 0x%lx\n",
		gravity, m_window);
	return;
    }

    w += m_border->xIndent();
    h += m_border->yIndent();

    if (invert) { w = -w; h = -h; }

    m_x += w;
    m_y += h;
}


Boolean Client::setLabel(void)
{
    const char *newLabel;

    if (m_name) newLabel = m_name;
    else if (m_iconName) newLabel = m_iconName;
    else newLabel = m_defaultLabel;

    if (!m_label) {

	m_label = NewString(newLabel);
	return True;

    } else if (strcmp(m_label, newLabel)) {

	free((void *)m_label);
	m_label = NewString(newLabel);
	return True;

    } else return True;//False;// dammit!
}


void Client::getColormaps(void)
{
    int i, n;
    Window *cw;
    XWindowAttributes attr;

    if (!m_managed) {
	XGetWindowAttributes(display(), m_window, &attr);
	m_colormap = attr.colormap;

//	fprintf(stderr, "colormap for %s is %p\n",m_label, (void *)m_colormap);
    }

    n = getProperty_aux(display(), m_window, Atoms::wm_colormaps, XA_WINDOW,
			100L, (unsigned char **)&cw);

    if (m_colormapWinCount != 0) {
	XFree((char *)m_colormapWindows);
	free((char *)m_windowColormaps);
    }

    if (n <= 0) {
	m_colormapWinCount = 0;
	return;
    }
    
    m_colormapWinCount = n;
    m_colormapWindows = cw;

    m_windowColormaps = (Colormap *)malloc(n * sizeof(Colormap));

    for (i = 0; i < n; ++i) {
	if (cw[i] == m_window) {
	    m_windowColormaps[i] = m_colormap;
	} else {
	    XSelectInput(display(), cw[i], ColormapChangeMask);
	    XGetWindowAttributes(display(), cw[i], &attr);
	    m_windowColormaps[i] = attr.colormap;
	}
    }
}


void Client::getTransient()
{
    Window t = None;

    if (XGetTransientForHint(display(), m_window, &t) != 0) {

	if (windowManager()->windowToClient(t) == this) {
	    fprintf(stderr,
		    "wm2tj: warning: client \"%s\" thinks it's a transient "
		    "for\nitself -- ignoring WM_TRANSIENT_FOR property...\n",
		    m_label ? m_label : "(no name)");
	    m_transient = None;
	} else {		
	    m_transient = t;
	}
    } else {
	m_transient = None;
    }
}


void Client::hide()
{
    // Idempotent: even if already Iconic, re-unmap defensively in case the
    // border got re-mapped out of band (e.g. a stale mapRaised). Without this
    // the frame can persist as a blank white ghost.
    m_border->unmap();
    unmapClientWindow();

    if (isHidden()) return;

    if (isActive()) windowManager()->clearFocus();

    setState(IconicState);
    windowManager()->addToHiddenList(this);
}


void Client::unhide(Boolean map)
{
    if (!isHidden()) {
	fprintf(stderr, "wm2tj: Client not hidden in Client::unhide\n");
	return;
    }

    windowManager()->removeFromHiddenList(this);

    if (map) {
	// User unhide: pull this window onto the current desktop.
	m_desktop = windowManager()->currentDesktop();
	m_offDesktop = False;
	setState(NormalState);
	XMapWindow(display(), m_window);
	mapRaised();

	if (CONFIG_AUTO_RAISE) focusIfAppropriate(False);
	else if (CONFIG_CLICK_TO_FOCUS) activate();
    }
}


void Client::hideForDesktop()
{
    if (m_offDesktop) return;
    if (!isNormal()) return;
    m_offDesktop = True;
    m_border->unmap();
    unmapClientWindow();
    if (isActive()) windowManager()->clearFocus();
}


// Unmap the client window ourselves, remembering that the resulting
// UnmapNotify is ours.  Otherwise a fast desktop switch (hide, then show
// again before the notify arrives) makes eventUnmap see a NormalState
// unmap and withdraw() the now-mapped window: it is reparented to root
// still mapped, unframed, and shows through on every desktop.
void Client::unmapClientWindow()
{
    XWindowAttributes attr;
    if (XGetWindowAttributes(display(), m_window, &attr) &&
	attr.map_state != IsUnmapped) ++m_ignoreUnmaps;
    XUnmapWindow(display(), m_window);
}


void Client::unhideForDesktop()
{
    if (!m_offDesktop) return;
    m_offDesktop = False;
    XMapWindow(display(), m_window);
    // Map without raising: an unmapped frame keeps its place in the stack,
    // so the desktop comes back as it was left.  mapRaised() here raised
    // every client in creation order, putting the newest one on top.
    m_border->map();
}


void Client::sendConfigureNotify()
{
    XConfigureEvent ce;

    ce.type   = ConfigureNotify;
    ce.event  = m_window;
    ce.window = m_window;

    ce.x = m_x;
    ce.y = m_y;
    ce.width  = m_w;
    ce.height = m_h;
    ce.border_width = m_bw;
    ce.above = None;
    ce.override_redirect = 0;

    XSendEvent(display(), m_window, False, StructureNotifyMask, (XEvent*)&ce);
}


void Client::refocus()
{
    XSetInputFocus(display(), m_window, RevertToPointerRoot, CurrentTime);
    if (m_protocol & PtakeFocus) {
	sendMessage(Atoms::wm_protocols, Atoms::wm_takeFocus);
    }
}


void Client::withdraw(Boolean changeState)
{
//    fprintf(stderr,"withdrawing\n");

    XUngrabButton(display(), AnyButton, AnyModifier, m_window);

    m_border->unmap();

    gravitate(True);
    XReparentWindow(display(), m_window, root(), m_x, m_y);

    gravitate(False);

    if (changeState) {
	XRemoveFromSaveSet(display(), m_window);
	setState(WithdrawnState);
    }

    ignoreBadWindowErrors = True;
    XSync(display(), False);
    ignoreBadWindowErrors = False;
}


void Client::rename()
{
    m_border->configure(0, 0, m_w, m_h, CWWidth | CWHeight, Above);
}


void Client::mapRaised()
{
    // Hidden/withdrawn clients must not have their frame mapped — that's how
    // we end up with a blank white ghost frame on top of nothing. Callers
    // that want to unhide should go through unhide(True) which transitions
    // state and remaps both window and frame.
    if (isHidden() || isWithdrawn()) return;

    // If we got here while marked off-desktop, the inner window is unmapped
    // and showing only the frame would look like a blank white ghost.
    // Pull the client onto the current desktop instead.
    if (m_offDesktop) {
	m_desktop = windowManager()->currentDesktop();
	m_offDesktop = False;
	XMapWindow(display(), m_window);
    }
    m_border->mapRaised();
    if (!m_fullscreen) windowManager()->raiseDocks();
    windowManager()->raiseTransients(this);
}


void Client::kill()
{
    if (m_protocol & Pdelete) {
	sendMessage(Atoms::wm_protocols, Atoms::wm_delete);
    } else {
	XKillClient(display(), m_window);
    }
}


void Client::ensureVisible()
{
    int mx = DisplayWidth(display(), 0) - 1; // hack
    int my = DisplayHeight(display(), 0) - 1;
    int px = m_x;
    int py = m_y;
    
    if (m_x + m_w > mx) m_x = mx - m_w;
    if (m_y + m_h > my) m_y = my - m_h;
    if (m_x < 0) m_x = 0;
    if (m_y < minY()) m_y = minY();

    if (m_x != px || m_y != py) m_border->moveTo(m_x, m_y);
}


// A dock (panel) with a top strut owns that strip of the screen: like
// GNOME, no window may be put under it, not even by hand.  Fullscreen
// windows are the exception and go to 0,0.
int Client::minY()
{
    return minYAt(m_x, m_y);
}

// the y the client may have when at x, y: below a dock's strip on that monitor
int Client::minYAt(int x, int y)
{
    if (m_fullscreen) return 0;
    int yi = m_border->yIndent();
    return windowManager()->topLimit(x + m_w / 2, y - yi) + yi;
}


void Client::keepOffStrut()
{
    if (m_y >= minY()) return;
    m_y = minY();
    m_border->moveTo(m_x, m_y);
    sendConfigureNotify();
}


void Client::lower()
{
    m_border->lower();
}




// Maximize and fullscreen.  m_x/m_y/m_w/m_h always describe the client
// area; the frame sits xIndent/yIndent outside it plus one pixel right and
// below.  Fullscreen puts the client at 0,0 and pushes the tab off-screen.

void Client::readNetState()
{
    Atom type; int format; unsigned long n, after;
    unsigned char *data = 0;

    m_maxH = m_maxV = m_fullscreen = False;
    if (XGetWindowProperty(display(), m_window, Atoms::net_wm_state, 0, 32,
			   False, XA_ATOM, &type, &format, &n, &after,
			   &data) == Success && data) {
	Atom *a = (Atom *)data;
	for (unsigned long i = 0; i < n; ++i) {
	    if (a[i] == Atoms::net_wm_state_maximized_horz) m_maxH = True;
	    if (a[i] == Atoms::net_wm_state_maximized_vert) m_maxV = True;
	    if (a[i] == Atoms::net_wm_state_fullscreen) m_fullscreen = True;
	}
    }
    if (data) XFree(data);

    // left behind by a previous wm2 (SIGHUP restart)
    data = 0;
    if (XGetWindowProperty(display(), m_window, Atoms::wm2_restore_geometry,
			   0, 4, False, XA_CARDINAL, &type, &format, &n,
			   &after, &data) == Success && data && n == 4) {
	long *g = (long *)data;
	m_restoreX = (int)g[0]; m_restoreY = (int)g[1];
	m_restoreW = (int)g[2]; m_restoreH = (int)g[3];
	m_haveRestore = True;
    }
    if (data) XFree(data);
}


void Client::writeNetState()
{
    Atom a[32];
    int n = 0;

    // keep atoms set by others (skip-taskbar, above, ...)
    Atom type; int format; unsigned long count, after;
    unsigned char *data = 0;
    if (XGetWindowProperty(display(), m_window, Atoms::net_wm_state, 0, 29,
			   False, XA_ATOM, &type, &format, &count, &after,
			   &data) == Success && data) {
	Atom *old = (Atom *)data;
	for (unsigned long i = 0; i < count; ++i) {
	    if (old[i] != Atoms::net_wm_state_maximized_horz &&
		old[i] != Atoms::net_wm_state_maximized_vert &&
		old[i] != Atoms::net_wm_state_fullscreen) a[n++] = old[i];
	}
    }
    if (data) XFree(data);

    if (m_maxH) a[n++] = Atoms::net_wm_state_maximized_horz;
    if (m_maxV) a[n++] = Atoms::net_wm_state_maximized_vert;
    if (m_fullscreen) a[n++] = Atoms::net_wm_state_fullscreen;

    XChangeProperty(display(), m_window, Atoms::net_wm_state, XA_ATOM, 32,
		    PropModeReplace, (unsigned char *)a, n);

    if (m_haveRestore) {
	long g[4] = { m_restoreX, m_restoreY, m_restoreW, m_restoreH };
	XChangeProperty(display(), m_window, Atoms::wm2_restore_geometry,
			XA_CARDINAL, 32, PropModeReplace, (unsigned char *)g, 4);
    } else {
	XDeleteProperty(display(), m_window, Atoms::wm2_restore_geometry);
    }
}


void Client::applyStateGeometry()
{
    int incW, incH;

    if (!m_maxH && !m_maxV && !m_fullscreen) {
	if (!m_haveRestore) return;
	m_x = m_restoreX; m_y = m_restoreY;
	m_w = m_restoreW; m_h = m_restoreH;
	m_haveRestore = False;
    } else {
	if (!m_haveRestore) {
	    m_restoreX = m_x; m_restoreY = m_y;
	    m_restoreW = m_w; m_restoreH = m_h;
	    m_haveRestore = True;
	}
	// the monitor the window is on (as it was before maximizing)
	const WindowManager::Monitor &mon = windowManager()->monitorFor(
	    m_restoreX, m_restoreY, m_restoreW, m_restoreH);
	if (m_fullscreen) {
	    m_x = mon.x; m_y = mon.y; m_w = mon.w; m_h = mon.h;
	} else {
	    if (m_maxH) {
		m_x = mon.x + mon.left + m_border->xIndent();
		m_w = mon.x + mon.w - mon.right - m_x - 1;
	    } else {
		m_x = m_restoreX; m_w = m_restoreW;
	    }
	    if (m_maxV) {
		m_y = mon.y + mon.top + m_border->yIndent();
		m_h = mon.y + mon.h - mon.bottom - m_y - 1;
	    } else {
		m_y = m_restoreY; m_h = m_restoreH;
	    }
	    fixResizeDimensions(m_w, m_h, incW, incH);
	}
    }

    m_border->configure(m_x, m_y, m_w, m_h, CWX | CWY | CWWidth | CWHeight,
			0, True);
    XMoveResizeWindow(display(), m_window,
		      m_border->xIndent(), m_border->yIndent(), m_w, m_h);
    sendConfigureNotify();
    writeNetState();

    if (m_fullscreen && isNormal()) XRaiseWindow(display(), parent());
}


void Client::setNetState(Boolean maxH, Boolean maxV, Boolean fullscreen)
{
    if (isFixedSize()) maxH = maxV = False;
    if (maxH == m_maxH && maxV == m_maxV && fullscreen == m_fullscreen) return;

    m_maxH = maxH;
    m_maxV = maxV;
    m_fullscreen = fullscreen;
    applyStateGeometry();
    if (!m_fullscreen && isNormal()) mapRaised();	// docks back on top
}


void Client::toggleMaximize()
{
    if (m_fullscreen || isFixedSize()) return;
    if (m_maxH && m_maxV) setNetState(False, False, False);
    else setNetState(True, True, False);
}


void Client::dropMaximize()
{
    if (!m_maxH && !m_maxV) return;
    m_maxH = m_maxV = False;
    m_haveRestore = False;	// keep the size the user just made
    writeNetState();
}


static Boolean netStateAction(long action, Boolean current)
{
    if (action == 0) return False;	// _NET_WM_STATE_REMOVE
    if (action == 1) return True;	// _NET_WM_STATE_ADD
    return !current;			// _NET_WM_STATE_TOGGLE
}


void Client::eventNetWmState(XClientMessageEvent *e)
{
    Boolean maxH = m_maxH, maxV = m_maxV, fs = m_fullscreen;

    for (int i = 1; i <= 2; ++i) {
	Atom p = (Atom)e->data.l[i];
	if (p == Atoms::net_wm_state_maximized_horz)
	    maxH = netStateAction(e->data.l[0], m_maxH);
	else if (p == Atoms::net_wm_state_maximized_vert)
	    maxV = netStateAction(e->data.l[0], m_maxV);
	else if (p == Atoms::net_wm_state_fullscreen)
	    fs = netStateAction(e->data.l[0], m_fullscreen);
    }

    setNetState(maxH, maxV, fs);
}
