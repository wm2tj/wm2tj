
#include "Manager.h"
#include "Client.h"
#include <sys/time.h>
#include <sys/select.h>
#include <ctype.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

// Wait until the X connection has data or timeout_us elapses, instead of
// sleeping blindly (the stock 50ms sleep capped drags at ~20fps).
// Returns elapsed milliseconds.
static unsigned long waitForX(Display *d, long timeout_us)
{
    struct timeval t0, t1, tv;
    fd_set rfds;
    int fd = ConnectionNumber(d);
    gettimeofday(&t0, 0);
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = timeout_us;
    select(fd + 1, &rfds, 0, 0, &tv);
    gettimeofday(&t1, 0);
    return (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_usec - t0.tv_usec) / 1000;
}

#define AllButtonMask	( Button1Mask | Button2Mask | Button3Mask \
			| Button4Mask | Button5Mask )
#define ButtonMask	( ButtonPressMask | ButtonReleaseMask )
#define DragMask        ( ButtonMask | ButtonMotionMask )
#define MenuMask	( ButtonMask | ButtonMotionMask | ExposureMask )
#define MenuGrabMask	( ButtonMask | ButtonMotionMask | StructureNotifyMask )


void WindowManager::eventButton(XButtonEvent *e)
{
    Client *c = windowToClient(e->window);

    // click caught by the grab on the client window: raise, then let the
    // app have the click as if we'd never seen it
    if (c && e->window == c->window()) {
	if (e->type == ButtonPress && c->isNormal()) {
	    c->mapRaised();
	    if (!c->isActive()) c->activate();
	}
	XAllowEvents(display(), ReplayPointer, e->time);
	return;
    }

    // A touchpad double-tap sends release and the next press at the same
    // instant, so the press lands while our drag grab on the root is still
    // active.  Hand it back to the frame it was really over.
    if (e->window == e->root && e->subwindow != None &&
	(c = windowToClient(e->subwindow)) != 0) {
	Window child;
	e->window = e->subwindow;
	XTranslateCoordinates(display(), e->root, e->window,
			      e->x_root, e->y_root, &e->x, &e->y, &child);
	e->subwindow = child;
    }

    if (e->window == e->root) {
	// Root: right-click (2-finger tap on a touchpad) opens the menu,
	// left-click cycles through windows. This is reversed from the
	// 1996 default for touchpad ergonomics.
	if (e->button == Button3) menu(e);
	else if (e->button == Button1) circulate(True);
	return;
    }

    // Non-root: right-click anywhere still cycles windows (original).
    if (e->button == Button3) {
	circulate(False);
	return;
    }

    if (c) {
	c->eventButton(e);
	return;
    }
}


void WindowManager::circulate(Boolean activeFirst)
{
    Client *c = 0;
    if (activeFirst) c = m_activeClient;
    // Active client may be off-desktop (hidden by a desktop switch). Mapping
    // it would show only the empty frame, so fall through to the search.
    if (c && c->isOffDesktop()) c = 0;

    if (!c) {

	int i, j;

	if (!m_activeClient) i = -1;
	else {
	    for (i = 0; i < m_clients.count(); ++i) {
		if (m_clients.item(i) == m_activeClient) break;
	    }

	    if (i >= m_clients.count()-1) i = -1;
	}

	for (j = i + 1;
	     (!m_clients.item(j)->isNormal() ||
	      m_clients.item(j)->isTransient() ||
	      m_clients.item(j)->isOffDesktop()); ++j) {
	    if (j >= m_clients.count() - 1) j = -1;
	    if (j == i) return; // no suitable clients
	}

	c = m_clients.item(j);
    }

    c->activateAndWarp();
}


void Client::activateAndWarp()
{
    mapRaised();
    ensureVisible();
    XWarpPointer(display(), None, parent(), 0, 0, 0, 0,
		 m_border->xIndent() / 2, m_border->xIndent() + 8);
    activate();
}


void Client::eventButton(XButtonEvent *e)
{
    if (e->type != ButtonPress) return;

    // middle-click (three-finger tap) on the tab hides the window
    if (e->button == Button2 && isNormal() && !isTransient() &&
	m_border->isTabEvent(e)) {
	hide();
	return;
    }

    mapRaised();

    if (e->button == Button1) {
	if (m_border->hasWindow(e->window)) {

	    m_border->eventButton(e);
	}
    }

    if (!isNormal() || isActive() || e->send_event) return;
    activate();
}


static long msSince(const struct timeval &t)
{
    struct timeval now;
    gettimeofday(&now, 0);
    return (now.tv_sec - t.tv_sec) * 1000 + (now.tv_usec - t.tv_usec) / 1000;
}

static int nobuttons(XButtonEvent *e) // straight outta 9wm
{
    int state;
    state = (e->state & AllButtonMask);
    return (e->type == ButtonRelease) && (state & (state - 1)) == 0;
}


int WindowManager::attemptGrab(Window w, Window constrain, int mask, int t)
{
    int status;
    if (t == 0) t = timestamp(False);
    status = XGrabPointer(display(), w, False, mask, GrabModeAsync,
			  GrabModeAsync, constrain, None, t);
    return status;
}


void WindowManager::releaseGrab(XButtonEvent *e)
{
    XEvent ev;
    if (!nobuttons(e)) {
	for (;;) {
	    XMaskEvent(display(), ButtonMask | ButtonMotionMask, &ev);
	    if (ev.type == MotionNotify) continue;
	    e = &ev.xbutton;
	    if (nobuttons(e)) break;
	}
    }

    XUngrabPointer(display(), e->time);
    m_currentTime = e->time;
}


// Display one level of menu and return the index the user picked
// (or -1 if dismissed outside the menu bounds).
//
// anchor_x/anchor_y are the screen coords to center the menu on; the
// helper may adjust them (and warp the pointer) when the menu would
// fall off-screen, so the caller can pass the same anchor next call.
int WindowManager::runMenuLoop(int &anchor_x, int &anchor_y,
                               const char **labels, int n)
{
    const Monitor &mon = monitorAt(anchor_x, anchor_y);
    int mx0 = mon.x, my0 = mon.y;
    int mx = mon.x + mon.w - 1;
    int my = mon.y + mon.h - 1;

    int maxWidth = 10;
    XGlyphInfo gi;
    for (int i = 0; i < n; ++i) {
	XftTextExtentsUtf8(display(), m_menuFont,
			   (FcChar8 *)labels[i], strlen(labels[i]), &gi);
	if (gi.xOff > maxWidth) maxWidth = gi.xOff;
    }
    maxWidth += 32;

    int entryHeight = m_menuFont->ascent + m_menuFont->descent + 4;
    int totalHeight = entryHeight * n + 13;
    int x = anchor_x - maxWidth/2;
    int y = anchor_y - 2;
    Boolean warp = False;

    if (x < mx0) {
	anchor_x += mx0 - x; x = mx0; warp = True;
    } else if (x + maxWidth >= mx) {
	anchor_x -= x + maxWidth - mx; x = mx - maxWidth; warp = True;
    }

    if (y < my0) {
	anchor_y += my0 - y; y = my0; warp = True;
    } else if (y + totalHeight >= my) {
	anchor_y -= y + totalHeight - my; y = my - totalHeight; warp = True;
    }

    if (warp) XWarpPointer(display(), None, root(),
			   None, None, None, None, anchor_x, anchor_y);

    // menu_borders auto: the outer frame follows the tab colour (the
    // left_line), falling back to the window borders
    unsigned long edge;
    if (!strcmp(CONFIG_MENU_BORDERS, "auto") && Border::edgePixel(this, &edge))
	XSetWindowBorder(display(), m_menuWindow, edge);

    XMoveResizeWindow(display(), m_menuWindow, x, y, maxWidth, totalHeight);
    XSelectInput(display(), m_menuWindow, MenuMask);
    XMapRaised(display(), m_menuWindow);

    if (attemptGrab(m_menuWindow, None, MenuGrabMask, 0) != GrabSuccess) {
	XUnmapWindow(display(), m_menuWindow);
	return -1;
    }

    int selecting = -1, prev = -1;
    Boolean done = False;
    Boolean drawn = False;
    XEvent event;
    int i;

    while (!done) {
	XMaskEvent(display(), MenuMask, &event);

	switch (event.type) {

	default:
	    fprintf(stderr, "wm2tj: unknown event type %d\n", event.type);
	    break;

	case ButtonPress:
	    if (!drawn) break;

	    x = event.xbutton.x;
	    y = event.xbutton.y - 11;
	    i = y / entryHeight;

	    if (selecting >= 0 && y >= selecting * entryHeight - 3 &&
		y <= (selecting+1) * entryHeight - 3) i = selecting;

	    if (x < 0 || x > maxWidth || y < -3) i = -1;
	    else if (i < 0 || i >= n) i = -1;

	    XUngrabPointer(display(), event.xbutton.time);
	    m_currentTime = event.xbutton.time;
	    XUnmapWindow(display(), m_menuWindow);
	    selecting = i;
	    done = True;
	    break;

	case ButtonRelease:
	    break;

	case MotionNotify:
	    if (!drawn) break;
	    x = event.xbutton.x;
	    y = event.xbutton.y - 11;
	    prev = selecting;
	    selecting = y / entryHeight;

	    if (prev >= 0 && y >= prev * entryHeight - 3 &&
		y <= (prev+1) * entryHeight - 3) selecting = prev;

	    if (x < 0 || x > maxWidth || y < -3) selecting = -1;
	    else if (selecting < 0 || selecting > n) selecting = -1;

	    if (selecting == prev) break;

	    if (prev >= 0 && prev < n) {
		XFillRectangle(display(), m_menuWindow, m_menuGC,
			       4, prev * entryHeight + 9,
			       maxWidth - 8, entryHeight);
	    }

	    if (selecting >= 0 && selecting < n) {
		XFillRectangle(display(), m_menuWindow, m_menuGC,
			       4, selecting * entryHeight + 9,
			       maxWidth - 8, entryHeight);
	    }

	    break;

	case Expose:
	    XClearWindow(display(), m_menuWindow);

	    XDrawRectangle(display(), m_menuWindow, m_menuGC, 2, 7,
			   maxWidth - 5, totalHeight - 10);

	    for (i = 0; i < n; ++i) {
		int dy = i * entryHeight + m_menuFont->ascent + 10;
		XftDrawStringUtf8(m_menuDraw, &m_menuXftColor, m_menuFont,
				  8, dy,
				  (FcChar8 *)labels[i], strlen(labels[i]));
	    }

	    if (selecting >= 0 && selecting < n) {
		XFillRectangle(display(), m_menuWindow, m_menuGC,
			       4, selecting * entryHeight + 9,
			       maxWidth - 8, entryHeight);
	    }

	    drawn = True;
	}
    }

    return selecting;
}


// Kind codes for each rendered row
enum { K_NEW = 0, K_LEAF, K_SUBMENU, K_HIDDEN, K_CLIENT, K_EXIT, K_BACK };

// "▶ name" (or "name  >" with no submenu_prefix); caller frees
static char *submenuLabel(const char *label)
{
    int sz = strlen(CONFIG_SUBMENU_PREFIX) + strlen(label) + 8;
    char *s = (char *)malloc(sz);
    // a prefix already marks it as a submenu, so drop the trailing ">"
    snprintf(s, sz, *CONFIG_SUBMENU_PREFIX ? "%s%s" : "%s%s  >",
	     CONFIG_SUBMENU_PREFIX, label);
    return s;
}


void WindowManager::menu(XButtonEvent *e)
{
    if (e->window == m_menuWindow) return;

    // wm2-menu-gen rewrote ~/.wm2rc: take the new menu (no restart)
    if (wm2ReloadMenu()) fprintf(stderr, "wm2tj: menu reread from ~/.wm2rc\n");

    int i;
    ClientList clients;

    for (i = 0; i < m_hiddenClients.count(); ++i) {
	clients.append(m_hiddenClients.item(i));
    }
    int nh_base = clients.count();   // # of hidden clients

    if (CONFIG_EVERYTHING_ON_ROOT_MENU) {
	for (i = 0; i < m_clients.count(); ++i) {
	    if (m_clients.item(i)->isNormal()) {
		clients.append(m_clients.item(i));
	    }
	}
    }

    const Monitor &mon = monitorAt(e->x, e->y);
    int mx = mon.x + mon.w - 1;
    int my = mon.y + mon.h - 1;
    Boolean allowExit = ((e->x > mx-3) && (e->y > my-3));

    if (strcmp(CONFIG_SUBMENU_STYLE, "replace")) {
	cascadeMenu(e, clients, nh_base, allowExit);
	clients.remove_all();
	return;
    }

    // Submenu navigation stack. stack[top] is the currently displayed
    // level. Depth 0 is the root (mixes New / user menu / hidden / exit);
    // deeper levels are pure WM2MenuEntry children with a "<- Back" header.
    struct Frame {
	WM2MenuEntry *items;
	int count;
	const char *title;
    } stack[8];
    int top = 0;
    stack[0].items = wm2config.user_menu;
    stack[0].count = wm2config.user_menu_count;
    stack[0].title = "";

    int anchor_x = e->x;
    int anchor_y = e->y;

    while (True) {
	Frame &f = stack[top];
	Boolean isRoot = (top == 0);

	int capN = f.count + clients.count() + 4;
	const char **labels = new const char *[capN];
	int *kinds = new int[capN];
	int *idxs  = new int[capN];
	char **owned = new char *[f.count + 1];
	int ownedN = 0;
	char back_label[128];

	int n = 0;
	if (isRoot) {
	    // "new_label none" hides the built-in new-window entry (2026-10-21)
	    if (*CONFIG_NEW_LABEL && strcmp(CONFIG_NEW_LABEL, "none")) {
		labels[n] = CONFIG_NEW_LABEL;
		kinds[n] = K_NEW; idxs[n] = -1; ++n;
	    }
	} else {
	    snprintf(back_label, sizeof(back_label),
		     "<- %s", f.title ? f.title : "");
	    labels[n] = back_label;
	    kinds[n] = K_BACK; idxs[n] = -1; ++n;
	}
	for (i = 0; i < f.count; ++i) {
	    if (f.items[i].child_count > 0) {
		char *s = submenuLabel(f.items[i].label);
		owned[ownedN++] = s;
		labels[n] = s;
		kinds[n] = K_SUBMENU; idxs[n] = i; ++n;
	    } else {
		labels[n] = f.items[i].label;
		kinds[n] = K_LEAF; idxs[n] = i; ++n;
	    }
	}
	if (isRoot) {
	    for (i = 0; i < nh_base; ++i) {
		labels[n] = clients.item(i)->label();
		kinds[n] = K_HIDDEN; idxs[n] = i; ++n;
	    }
	    for (i = nh_base; i < clients.count(); ++i) {
		labels[n] = clients.item(i)->label();
		kinds[n] = K_CLIENT; idxs[n] = i; ++n;
	    }
	    if (allowExit) {
		labels[n] = "[Exit wm2tj]";
		kinds[n] = K_EXIT; idxs[n] = -1; ++n;
	    }
	}

	int sel = runMenuLoop(anchor_x, anchor_y, labels, n);

	int selKind = (sel >= 0 && sel < n) ? kinds[sel] : -1;
	int selIdx  = (sel >= 0 && sel < n) ? idxs[sel]  : -1;

	for (i = 0; i < ownedN; ++i) free(owned[i]);
	delete[] owned;
	delete[] labels;
	delete[] kinds;
	delete[] idxs;

	if (sel < 0) {
	    clients.remove_all();
	    return;
	}

	if (selKind == K_BACK) {
	    if (top > 0) --top;
	    continue;
	}
	if (selKind == K_SUBMENU) {
	    if (top + 1 >= 8) continue;
	    WM2MenuEntry *sm = &f.items[selIdx];
	    ++top;
	    stack[top].items = sm->children;
	    stack[top].count = sm->child_count;
	    stack[top].title = sm->label;
	    continue;
	}

	// Leaf actions terminate the menu.
	if (selKind == K_NEW) {
	    spawn();
	} else if (selKind == K_LEAF) {
	    spawn(f.items[selIdx].command);
	} else if (selKind == K_HIDDEN) {
	    clients.item(selIdx)->unhide(True);
	} else if (selKind == K_CLIENT) {
	    Client *sc = clients.item(selIdx);
	    if (sc->isOffDesktop() && !sc->isSticky())
		switchToDesktop(sc->desktop());
	    if (CONFIG_CLICK_TO_FOCUS) {
		clients.item(selIdx)->mapRaised();	// switching no longer raises
		clients.item(selIdx)->activate();
	    } else {
		clients.item(selIdx)->mapRaised();
	    }
	    clients.item(selIdx)->ensureVisible();
	} else if (selKind == K_EXIT) {
	    m_signalled = True;
	}

	clients.remove_all();
	return;
    }
}


// ---- cascading root menu (submenu_style cascade, 2026-10-21) ----
//
// The main menu stays up and a folder opens beside it, as in a panel
// menu.  Resting on a folder opens it after CASCADE_DELAY ms, clicking
// opens it at once; the delay also lets the pointer cut across other
// rows on its way into an open submenu.
//
// A search row on top (search_label) filters every program in the menu
// as you type.  Text comes through the input method named in XMODIFIERS
// (XIM) when its server is running, so Japanese can be typed; kana are
// also matched as romaji ("tekisuto" finds テキスト), and the command
// name is searched too.  The highlight takes the frame colour.

#define CASCADE_DELAY 150	// ms
#define SEARCH_MAX 20		// results shown
#define QUERY_MAX 120		// bytes of UTF-8

// hover moves the highlight without a button held, unlike the stock menu
#define CascadeMask	( MenuMask | PointerMotionMask | KeyPressMask | KeyReleaseMask )
#define CascadeGrabMask	( MenuGrabMask | PointerMotionMask )

enum { K_SEARCH = K_BACK + 1 };

struct MenuLevel {
    int n;			// rows
    const char **labels;
    int *kinds, *idxs;		// idxs: client index for window rows
    WM2MenuEntry **ents;	// menu entry for program and folder rows
    char **owned;		// labels we malloc'd
    int ownedN;
    int x, y, w, h;		// outer position, inner size
    int minW;			// root: keep the unfiltered width
    int sel;			// highlighted row, or -1
    int open;			// row whose folder shows at the next level, or -1
};

static void allocMenuLevel(MenuLevel &l, int cap)
{
    l.labels = new const char *[cap];
    l.kinds = new int[cap];
    l.idxs = new int[cap];
    l.ents = new WM2MenuEntry *[cap];
    l.owned = new char *[cap];
    l.ownedN = 0;
    l.n = 0;
    l.minW = 0;
    l.sel = l.open = -1;
}

static void addMenuRow(MenuLevel &l, int kind, const char *label,
		       WM2MenuEntry *ent, int idx)
{
    if (kind == K_SUBMENU) label = l.owned[l.ownedN++] = submenuLabel(label);
    l.labels[l.n] = label;
    l.kinds[l.n] = kind;
    l.ents[l.n] = ent;
    l.idxs[l.n++] = idx;
}

static void freeMenuLevel(MenuLevel &l)
{
    for (int i = 0; i < l.ownedN; ++i) free(l.owned[i]);
    delete[] l.owned;
    delete[] l.labels;
    delete[] l.kinds;
    delete[] l.idxs;
    delete[] l.ents;
}

static void fillMenuLevel(MenuLevel &l, WM2MenuEntry *items, int count)
{
    allocMenuLevel(l, count);
    for (int i = 0; i < count; ++i) {
	addMenuRow(l, items[i].child_count > 0 ? K_SUBMENU : K_LEAF,
		   items[i].label, &items[i], -1);
    }
}

static long nowMs()
{
    struct timeval t;
    gettimeofday(&t, 0);
    return t.tv_sec * 1000L + t.tv_usec / 1000;
}


// -- search keys: lowercase letters and digits only, kana as romaji, and
// spellings folded together (shi/si, chi/ti, tsu/tu, j/zy, f/h, doubled
// letters) so either way of typing matches

// katakana U+30A1..U+30F6 (hiragana is the same 0x60 lower); "" = small kana
static const char *const kanaRomaji[] = {
    "", "a", "", "i", "", "u", "", "e", "", "o",
    "ka", "ga", "ki", "gi", "ku", "gu", "ke", "ge", "ko", "go",
    "sa", "za", "si", "zi", "su", "zu", "se", "ze", "so", "zo",
    "ta", "da", "ti", "di", "", "tu", "du", "te", "de", "to", "do",
    "na", "ni", "nu", "ne", "no",
    "ha", "ba", "pa", "hi", "bi", "pi", "hu", "bu", "pu",
    "he", "be", "pe", "ho", "bo", "po",
    "ma", "mi", "mu", "me", "mo",
    "", "ya", "", "yu", "", "yo",
    "ra", "ri", "ru", "re", "ro",
    "", "wa", "i", "e", "o", "n", "bu", "ka", "ke",
};

static int isVowel(char c)
{
    return c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o';
}

static void appendKana(char *out, int &len, int sz, unsigned int cp)
{
    if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;	// hiragana
    const char *r = kanaRomaji[cp - 0x30A1];
    char small = 0;
    switch (cp) {
    case 0x30A1: small = 'a'; break;
    case 0x30A3: small = 'i'; break;
    case 0x30A5: small = 'u'; break;
    case 0x30A7: small = 'e'; break;
    case 0x30A9: small = 'o'; break;
    case 0x30EE: small = 'a'; break;
    }
    if (cp == 0x30E3 || cp == 0x30E5 || cp == 0x30E7) {
	// キャ: drop the i and go on with ya/yu/yo
	if (len >= 2 && out[len - 1] == 'i' && !isVowel(out[len - 2])) --len;
	r = cp == 0x30E3 ? "ya" : cp == 0x30E5 ? "yu" : "yo";
    } else if (small) {
	// ファ, ティ: swap the vowel; ウィ becomes wi
	if (len >= 1 && isVowel(out[len - 1])) {
	    if (out[len - 1] == 'u' && (len < 2 || isVowel(out[len - 2])))
		out[len - 1] = 'w';
	    else --len;
	}
	char v[2] = { small, 0 };
	r = v;
	if (len < sz - 1) out[len++] = small;
	return;
    }
    while (*r && len < sz - 1) out[len++] = *r++;	// ッ and ー add nothing
}

// fold spellings; works in place since nothing gets longer except j
static void foldKey(char *s, int sz)
{
    static const char *const from[] = { "shi", "chi", "tsu", "ji", "sh", "ch", "j", "f" };
    static const char *const to[]   = { "si",  "ti",  "tu",  "zi", "sy", "ty", "zy", "h" };
    char *buf = (char *)malloc(sz);
    for (unsigned int r = 0; r < sizeof(from) / sizeof(from[0]); ++r) {
	int n = 0, fl = strlen(from[r]), tl = strlen(to[r]);
	for (char *p = s; *p; ) {
	    if (!strncmp(p, from[r], fl) && n + tl < sz) {
		memcpy(buf + n, to[r], tl); n += tl; p += fl;
	    } else if (n < sz - 1) {
		buf[n++] = *p++;
	    } else break;
	}
	buf[n] = 0;
	strcpy(s, buf);
    }
    free(buf);
    int n = 0;
    for (char *p = s; *p; ++p) {
	if (n == 0 || s[n - 1] != *p) s[n++] = *p;
    }
    s[n] = 0;
}

static void searchKey(const char *text, char *out, int sz)
{
    int len = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p && len < sz - 4) {
	unsigned int cp;
	if (*p < 0x80) {
	    if (isalnum(*p)) out[len++] = tolower(*p);
	    ++p;
	    continue;
	}
	if ((*p & 0xf0) == 0xe0 && p[1] && p[2]) {
	    cp = ((p[0] & 0x0f) << 12) | ((p[1] & 0x3f) << 6) | (p[2] & 0x3f);
	    p += 3;
	    if ((cp >= 0x3041 && cp <= 0x3096) || (cp >= 0x30A1 && cp <= 0x30F6))
		appendKana(out, len, sz, cp);
	    continue;
	}
	++p;			// kanji and the rest have no reading here
    }
    out[len] = 0;
    foldKey(out, sz);
}

// ASCII lowercased, the rest (kanji, kana) left as it is
static void lowerCopy(const char *s, char *out, int sz)
{
    int n = 0;
    while (*s && n < sz - 1) out[n++] = tolower((unsigned char)*s++);
    out[n] = 0;
}

// q: the folded key of the query, raw: the query as typed, lowercased;
// tried on the label, then on the program name from the command
static Boolean searchMatch(const char *label, const char *command,
			   const char *q, const char *raw)
{
    char key[512];
    int n;
    lowerCopy(label, key, sizeof(key));
    if (*raw && strstr(key, raw)) return True;
    searchKey(label, key, 256);
    if (*q && strstr(key, q)) return True;
    if (!command) return False;
    const char *s = command, *e;
    while (*s == ' ') ++s;
    for (e = s; *e && *e != ' '; ++e) if (*e == '/') s = e + 1;
    n = e - s < 255 ? e - s : 255;
    char word[256];
    memcpy(word, s, n); word[n] = 0;
    searchKey(word, key, 256);
    return *q && strstr(key, q) != 0;
}

static void searchEntries(MenuLevel &l, WM2MenuEntry *items, int count,
			  const char *q, const char *raw)
{
    for (int i = 0; i < count && l.n < SEARCH_MAX + 1; ++i) {
	WM2MenuEntry *m = &items[i];
	if (m->child_count > 0) {
	    searchEntries(l, m->children, m->child_count, q, raw);
	    continue;
	}
	if (!searchMatch(m->label, m->command, q, raw)) continue;
	int dup = 0;		// the same program is often in two folders
	for (int j = 0; j < l.n && !dup; ++j) {
	    dup = l.ents[j] && l.kinds[j] == K_LEAF &&
		!strcmp(l.ents[j]->label, m->label) &&
		!strcmp(l.ents[j]->command, m->command);
	}
	if (!dup) addMenuRow(l, K_LEAF, m->label, m, -1);
    }
}

// the root level: search row, New, the user menu, windows, Exit; or,
// with a query, the programs and windows that match it
static void buildRootLevel(MenuLevel &r, ClientList &clients, int nh,
			   Boolean allowExit, Boolean search, const char *query)
{
    allocMenuLevel(r, wm2config.user_menu_count + clients.count() + SEARCH_MAX + 4);
    if (search) addMenuRow(r, K_SEARCH, query, 0, -1);

    if (*query) {
	char q[QUERY_MAX * 2 + 8], raw[QUERY_MAX + 1];
	searchKey(query, q, sizeof(q));
	lowerCopy(query, raw, sizeof(raw));
	searchEntries(r, wm2config.user_menu, wm2config.user_menu_count, q, raw);
	for (int i = 0; i < clients.count() && r.n < SEARCH_MAX + 1; ++i) {
	    if (searchMatch(clients.item(i)->label(), 0, q, raw)) {
		addMenuRow(r, i < nh ? K_HIDDEN : K_CLIENT,
			   clients.item(i)->label(), 0, i);
	    }
	}
	return;
    }

    // "new_label none" hides the built-in new-window entry (2026-10-21)
    if (*CONFIG_NEW_LABEL && strcmp(CONFIG_NEW_LABEL, "none")) {
	addMenuRow(r, K_NEW, CONFIG_NEW_LABEL, 0, -1);
    }
    for (int i = 0; i < wm2config.user_menu_count; ++i) {
	WM2MenuEntry *m = &wm2config.user_menu[i];
	addMenuRow(r, m->child_count > 0 ? K_SUBMENU : K_LEAF, m->label, m, -1);
    }
    for (int i = 0; i < clients.count(); ++i) {
	addMenuRow(r, i < nh ? K_HIDDEN : K_CLIENT,
		   clients.item(i)->label(), 0, i);
    }
    if (allowExit) addMenuRow(r, K_EXIT, "[Exit wm2tj]", 0, -1);
}


// -- input method.  Opened at the first menu after its server is up (the
// server usually starts after the window manager), kept until the server
// goes away, then looked for again.

static XIM s_im;

static void imDestroyed(XIM, XPointer, XPointer)
{
    s_im = 0;
}

// is the server named in XMODIFIERS (@im=fcitx) registered on the display?
static Boolean imServerUp(Display *d)
{
    const char *mod = getenv("XMODIFIERS");
    const char *p = mod ? strstr(mod, "@im=") : 0;
    if (!p) return False;
    char want[64];
    int n = snprintf(want, sizeof(want), "@server=%s", p + 4);
    for (int i = 8; i < n && i < (int)sizeof(want); ++i) {
	if (want[i] == '@' || want[i] == ' ') { want[i] = 0; break; }
    }

    Atom servers = XInternAtom(d, "XIM_SERVERS", True);
    if (servers == None) return False;
    Atom type, *list = 0;
    int format;
    unsigned long count, after;
    Boolean up = False;
    if (XGetWindowProperty(d, DefaultRootWindow(d), servers, 0, 1024, False,
			   XA_ATOM, &type, &format, &count, &after,
			   (unsigned char **)&list) == Success && list) {
	for (unsigned long i = 0; i < count && !up; ++i) {
	    char *name = XGetAtomName(d, list[i]);
	    if (name) {
		up = !strcmp(name, want);
		XFree(name);
	    }
	}
	XFree(list);
    }
    return up;
}

static XIC openMenuIC(Display *d, Window w)
{
    if (!s_im) {
	if (!imServerUp(d)) return 0;
	if (!(s_im = XOpenIM(d, 0, 0, 0))) return 0;
	XIMCallback cb;
	cb.client_data = 0;
	cb.callback = imDestroyed;
	XSetIMValues(s_im, XNDestroyCallback, &cb, NULL);
    }

    // the server draws its own window for what is being converted
    XIMStyles *styles = 0;
    XIMStyle style = 0;
    if (XGetIMValues(s_im, XNQueryInputStyle, &styles, NULL) || !styles)
	return 0;
    for (int i = 0; i < styles->count_styles; ++i) {
	XIMStyle s = styles->supported_styles[i];
	if (s == (XIMPreeditNothing | XIMStatusNothing)) style = s;
	else if (!style && s == (XIMPreeditNone | XIMStatusNone)) style = s;
    }
    XFree(styles);
    if (!style) return 0;

    return XCreateIC(s_im, XNInputStyle, style, XNClientWindow, w,
		     XNFocusWindow, w, NULL);
}

// tell the server where the caret is, for its conversion window
static void placeMenuIC(XIC ic, int x, int y)
{
    if (!ic) return;
    XPoint spot;
    spot.x = x; spot.y = y;
    XVaNestedList attr = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
    XSetICValues(ic, XNPreeditAttributes, attr, NULL);
    XFree(attr);
}

// events the menu doesn't use wait until it closes
struct DeferredEvents {
    XEvent *ev;
    int n, cap;
};

static void deferEvent(DeferredEvents &d, XEvent *e)
{
    if (d.n == d.cap) {
	d.cap = d.cap ? d.cap * 2 : 32;
	d.ev = (XEvent *)realloc(d.ev, d.cap * sizeof(XEvent));
    }
    d.ev[d.n++] = *e;
}


// paint for the open menu
static GC s_bgGC, s_fgGC, s_hlGC;
static XftColor s_hlText, s_dimText;


Window WindowManager::menuLevelWindow(int k)
{
    return k == 0 ? m_menuWindow : m_subMenuWindow[k - 1];
}

Boolean WindowManager::isMenuWindow(Window w)
{
    for (int k = 0; k < MENU_DEPTH; ++k) {
	if (menuLevelWindow(k) == w) return True;
    }
    return False;
}

void WindowManager::sizeMenuLevel(MenuLevel &l)
{
    XGlyphInfo gi;
    l.w = 10;
    for (int i = 0; i < l.n; ++i) {
	const char *s = l.kinds[i] == K_SEARCH ? CONFIG_SEARCH_LABEL : l.labels[i];
	XftTextExtentsUtf8(display(), m_menuFont,
			   (FcChar8 *)s, strlen(s), &gi);
	if (gi.xOff > l.w) l.w = gi.xOff;
    }
    l.w += 32;
    if (l.w < l.minW) l.w = l.minW;
    l.h = (m_menuFont->ascent + m_menuFont->descent + 4) * l.n + 13;
}

void WindowManager::drawMenuRow(MenuLevel &l, int k, int i)
{
    Window w = menuLevelWindow(k);
    XftDraw *d = k == 0 ? m_menuDraw : m_subMenuDraw[k - 1];
    int eh = m_menuFont->ascent + m_menuFont->descent + 4;
    int y = i * eh + 9, base = y + m_menuFont->ascent + 1;

    if (l.kinds[i] == K_SEARCH) {
	XFillRectangle(display(), w, s_bgGC, 4, y, l.w - 8, eh);
	XDrawRectangle(display(), w, s_fgGC, 5, y + 1, l.w - 11, eh - 3);
	const char *q = l.labels[i];
	if (*q) {
	    XGlyphInfo gi;
	    XftDrawStringUtf8(d, &m_menuXftColor, m_menuFont, 8, base,
			      (FcChar8 *)q, strlen(q));
	    XftTextExtentsUtf8(display(), m_menuFont, (FcChar8 *)q,
			       strlen(q), &gi);
	    XFillRectangle(display(), w, s_fgGC, 9 + gi.xOff, y + 4,
			   1, eh - 8);	// caret
	} else {
	    XftDrawStringUtf8(d, &s_dimText, m_menuFont, 8, base,
			      (FcChar8 *)CONFIG_SEARCH_LABEL,
			      strlen(CONFIG_SEARCH_LABEL));
	}
	return;
    }

    Boolean on = (i == l.sel);
    XFillRectangle(display(), w, on ? s_hlGC : s_bgGC, 4, y, l.w - 8, eh);
    XftDrawStringUtf8(d, on ? &s_hlText : &m_menuXftColor, m_menuFont,
		      8, base, (FcChar8 *)l.labels[i], strlen(l.labels[i]));
}

void WindowManager::drawMenuLevel(MenuLevel &l, int k)
{
    Window w = menuLevelWindow(k);
    XClearWindow(display(), w);
    XDrawRectangle(display(), w, s_fgGC, 2, 7, l.w - 5, l.h - 10);
    for (int i = 0; i < l.n; ++i) drawMenuRow(l, k, i);
}

void WindowManager::highlightMenuRow(MenuLevel &l, int k, int row)
{
    if (row >= 0 && l.kinds[row] == K_SEARCH) row = -1;
    if (l.sel == row) return;
    int old = l.sel;
    l.sel = row;
    if (old >= 0) drawMenuRow(l, k, old);
    if (row >= 0) drawMenuRow(l, k, row);
}


void WindowManager::cascadeMenu(XButtonEvent *e, ClientList &clients,
				int nh, Boolean allowExit)
{
    const Monitor &mon = monitorAt(e->x, e->y);	// the menu stays on it
    int mx0 = mon.x, my0 = mon.y;
    int mx = mon.x + mon.w;
    int my = mon.y + mon.h;
    int bw = MENU_BORDER_WIDTH;
    int eh = m_menuFont->ascent + m_menuFont->descent + 4;
    int i;

    // menu_borders auto: the outer frame follows the tab colour (the
    // left_line), falling back to the window borders
    unsigned long frame = m_menuBorderPixel, edge;
    if (!strcmp(CONFIG_MENU_BORDERS, "auto") && Border::edgePixel(this, &edge)) {
	frame = edge;
    }
    for (int k = 0; k < MENU_DEPTH; ++k) {
	XSetWindowBorder(display(), menuLevelWindow(k), frame);
    }

    // the highlight is the frame colour, with white text if that's dark
    Visual *vis = DefaultVisual(display(), m_screenNumber);
    XColor fc;
    fc.pixel = frame;
    XQueryColor(display(), m_defaultColormap, &fc);
    Boolean dark = (fc.red * 3 + fc.green * 6 + fc.blue) / 10 < 0x8000;
    if (!dark) s_hlText = m_menuXftColor;
    else XftColorAllocName(display(), vis, m_defaultColormap, "white", &s_hlText);
    XftColorAllocName(display(), vis, m_defaultColormap, "gray45", &s_dimText);
    XGCValues gv;
    gv.foreground = frame;
    s_hlGC = XCreateGC(display(), root(), GCForeground, &gv);
    gv.foreground = m_menuBackgroundPixel;
    s_bgGC = XCreateGC(display(), root(), GCForeground, &gv);
    gv.foreground = m_menuForegroundPixel;
    s_fgGC = XCreateGC(display(), root(), GCForeground, &gv);

    Boolean search = *CONFIG_SEARCH_LABEL && strcmp(CONFIG_SEARCH_LABEL, "none");
    char query[QUERY_MAX + 1] = "";

    MenuLevel lv[MENU_DEPTH];
    MenuLevel &r = lv[0];
    buildRootLevel(r, clients, nh, allowExit, search, query);
    int depth = 1;
    int pickK = -1, pickRow = -1;
    XEvent ev;
    XIC ic = 0;
    DeferredEvents deferred = { 0, 0, 0 };

    if (r.n == 0) goto out;

    // centred on the pointer, and kept on screen by moving the pointer
    // along with it, as the stock menu does
    {
	sizeMenuLevel(r);
	r.minW = r.w;
	r.x = e->x - r.w / 2 - bw;
	r.y = e->y - 2 - bw;
	int ox = r.x, oy = r.y;
	if (r.x + r.w + 2 * bw > mx) r.x = mx - r.w - 2 * bw;
	if (r.y + r.h + 2 * bw > my) r.y = my - r.h - 2 * bw;
	if (r.x < mx0) r.x = mx0;
	if (r.y < my0) r.y = my0;
	if (r.x != ox || r.y != oy) {
	    XWarpPointer(display(), None, root(), None, None, None, None,
			 e->x + r.x - ox, e->y + r.y - oy);
	}
    }

    XMoveResizeWindow(display(), m_menuWindow, r.x, r.y, r.w, r.h);
    XSelectInput(display(), m_menuWindow, CascadeMask);
    XMapRaised(display(), m_menuWindow);

    if (attemptGrab(m_menuWindow, None, CascadeGrabMask, 0) != GrabSuccess) {
	XUnmapWindow(display(), m_menuWindow);
	goto out;
    }
    // no keyboard just means no typing; no input method, ASCII only
    if (search) {
	XGrabKeyboard(display(), m_menuWindow, False, GrabModeAsync,
		      GrabModeAsync, CurrentTime);
	if ((ic = openMenuIC(display(), m_menuWindow)) != 0) {
	    long filter = 0;
	    XGetICValues(ic, XNFilterEvents, &filter, NULL);
	    XSelectInput(display(), m_menuWindow, CascadeMask | filter);
	    placeMenuIC(ic, 9, 9 + eh);
	    XSetICFocus(ic);
	}
    }

    {
    int pendK = -1, pendRow = -1;	// folder change waiting for the delay
    long pendAt = 0;
    Boolean drawn = False, done = False;

    while (!done) {

	// close the levels past k, then open row's folder at k + 1
	int toK = -1, toRow = -1;
	Boolean requery = False;

	if (pendK >= 0 && !XPending(display())) {
	    long left = pendAt - nowMs();
	    if (left > 0) {
		waitForX(display(), left * 1000);
		continue;
	    }
	    toK = pendK; toRow = pendRow;
	    pendK = -1;
	    ev.type = 0;
	} else {
	    // everything goes past the input method first; what the menu
	    // doesn't use is kept for the main loop
	    XNextEvent(display(), &ev);
	    if (XFilterEvent(&ev, None)) continue;
	    if (ev.type == Expose ? !isMenuWindow(ev.xexpose.window)
		: (ev.type != MotionNotify && ev.type != ButtonPress &&
		   ev.type != ButtonRelease && ev.type != KeyPress &&
		   ev.type != KeyRelease)) {
		deferEvent(deferred, &ev);
		continue;
	    }
	}

	int k = -1, row = -1;
	if (ev.type == MotionNotify || ev.type == ButtonPress) {
	    int xr = ev.type == MotionNotify ? ev.xmotion.x_root
					     : ev.xbutton.x_root;
	    int yr = ev.type == MotionNotify ? ev.xmotion.y_root
					     : ev.xbutton.y_root;
	    for (k = depth - 1; k >= 0; --k) {
		int lx = xr - lv[k].x - bw, ly = yr - lv[k].y - bw;
		if (lx >= 0 && lx < lv[k].w && ly >= 0 && ly < lv[k].h) {
		    row = ly >= 9 ? (ly - 9) / eh : -1;
		    if (row >= lv[k].n || (row >= 0 && lv[k].kinds[row] == K_SEARCH))
			row = -1;
		    break;
		}
	    }
	}

	switch (ev.type) {

	case MotionNotify:
	    if (!drawn) break;
	    if (k < 0) {
		// off the menus: drop the highlight unless it leads somewhere
		pendK = -1;
		MenuLevel &d = lv[depth - 1];
		highlightMenuRow(d, depth - 1, d.open);
		break;
	    }
	    // reached a level past the pending change: keep what's open
	    if (pendK >= 0 && k > pendK) pendK = -1;
	    for (i = 0; i < depth; ++i) {
		if (i != k) highlightMenuRow(lv[i], i, lv[i].open);
	    }
	    if (row < 0) break;
	    highlightMenuRow(lv[k], k, row);
	    if (row == lv[k].open) {
		pendK = -1;
	    } else if (lv[k].open >= 0 || lv[k].kinds[row] == K_SUBMENU) {
		if (pendK != k || pendRow != row) {
		    pendK = k; pendRow = row;
		    pendAt = nowMs() + CASCADE_DELAY;
		}
	    } else {
		pendK = -1;
	    }
	    break;

	case ButtonPress:
	    if (!drawn) break;
	    if (k < 0) {
		done = True;		// outside: dismiss
	    } else if (row >= 0 && lv[k].kinds[row] == K_SUBMENU) {
		pendK = -1;
		if (row != lv[k].open) { toK = k; toRow = row; }
	    } else if (row >= 0) {
		pickK = k; pickRow = row;
		done = True;
	    }
	    break;

	case KeyPress: {
	    char buf[64];
	    KeySym ks = NoSymbol;
	    int len;
	    if (ic) {
		Status st;
		len = Xutf8LookupString(ic, &ev.xkey, buf, sizeof(buf) - 1,
					&ks, &st);
		if (st == XBufferOverflow || st == XLookupNone) len = 0;
		if (st == XLookupChars || st == XLookupNone) ks = NoSymbol;
	    } else {
		len = XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &ks, 0);
	    }
	    buf[len] = 0;
	    MenuLevel &d = lv[depth - 1];
	    int qn = strlen(query);
	    pendK = -1;

	    if (ks == XK_Escape) {
		if (qn) { query[0] = 0; requery = True; }
		else done = True;
	    } else if (ks == XK_BackSpace) {
		if (qn) {
		    // a whole character, which may be several bytes
		    while (qn > 1 && (query[qn - 1] & 0xc0) == 0x80) --qn;
		    query[qn - 1] = 0;
		    requery = True;
		}
	    } else if (ks == XK_Up || ks == XK_Down) {
		// step over the search row and wrap round
		int s = d.sel, step = ks == XK_Up ? -1 : 1;
		for (int tries = 0; tries < d.n; ++tries) {
		    s = s < 0 ? (step > 0 ? 0 : d.n - 1) : (s + step + d.n) % d.n;
		    if (d.kinds[s] != K_SEARCH) break;
		}
		if (depth > 1 || d.open < 0) highlightMenuRow(d, depth - 1, s);
	    } else if (ks == XK_Right || ks == XK_Return || ks == XK_KP_Enter) {
		if (d.sel >= 0 && d.kinds[d.sel] == K_SUBMENU) {
		    toK = depth - 1; toRow = d.sel;
		} else if (d.sel >= 0 && ks != XK_Right) {
		    pickK = depth - 1; pickRow = d.sel;
		    done = True;
		}
	    } else if (ks == XK_Left) {
		if (depth > 1) { toK = depth - 2; toRow = lv[depth - 2].open; }
	    } else if (search && len > 0 && (unsigned char)buf[0] >= ' ' &&
		       buf[0] != 0x7f && qn + len <= QUERY_MAX) {
		strcpy(query + qn, buf);
		requery = True;
	    }
	    break;
	}

	case Expose:
	    for (i = 0; i < depth; ++i) {
		if (ev.xexpose.window == menuLevelWindow(i)) {
		    if (ev.xexpose.count == 0) drawMenuLevel(lv[i], i);
		    if (i == 0) drawn = True;
		}
	    }
	    break;
	}

	if (requery) {
	    // back to the root alone, rebuilt for the new query
	    while (depth > 1) {
		--depth;
		XUnmapWindow(display(), menuLevelWindow(depth));
		freeMenuLevel(lv[depth]);
	    }
	    int x = r.x, y = r.y, minW = r.minW;
	    freeMenuLevel(r);
	    buildRootLevel(r, clients, nh, allowExit, search, query);
	    r.x = x; r.y = y; r.minW = minW;
	    sizeMenuLevel(r);
	    if (*query && r.n > 1) r.sel = 1;	// Return takes the first hit
	    if (r.y + r.h + 2 * bw > my) r.y = my - r.h - 2 * bw;
	    if (r.y < my0) r.y = my0;
	    XMoveResizeWindow(display(), m_menuWindow, r.x, r.y, r.w, r.h);
	    drawMenuLevel(r, 0);
	    if (ic) {
		XGlyphInfo gi;
		XftTextExtentsUtf8(display(), m_menuFont, (FcChar8 *)query,
				   strlen(query), &gi);
		placeMenuIC(ic, 9 + gi.xOff, 9 + eh);
	    }
	    continue;
	}

	if (toK >= 0) {
	    while (depth > toK + 1) {
		--depth;
		XUnmapWindow(display(), menuLevelWindow(depth));
		freeMenuLevel(lv[depth]);
	    }
	    MenuLevel &p = lv[toK];
	    p.open = -1;
	    if (p.kinds[toRow] == K_SUBMENU && depth < MENU_DEPTH) {
		WM2MenuEntry *sm = p.ents[toRow];
		MenuLevel &c = lv[depth];
		fillMenuLevel(c, sm->children, sm->child_count);
		sizeMenuLevel(c);

		// beside the folder's row, or on the left if it won't fit
		c.x = p.x + p.w + 2 * bw;
		if (c.x + c.w + 2 * bw > mx) c.x = p.x - c.w - 2 * bw;
		if (c.x < mx0) c.x = mx0;
		c.y = p.y + toRow * eh;
		if (c.y + c.h + 2 * bw > my) c.y = my - c.h - 2 * bw;
		if (c.y < my0) c.y = my0;

		Window w = menuLevelWindow(depth);
		XMoveResizeWindow(display(), w, c.x, c.y, c.w, c.h);
		XSelectInput(display(), w, CascadeMask);
		XMapRaised(display(), w);
		p.open = toRow;
		++depth;
		// from the keyboard, land on the first row of the new folder
		if (ev.type == KeyPress) c.sel = 0;
	    }
	    highlightMenuRow(p, toK, toRow);
	}
    }
    }

    m_currentTime = ev.type == ButtonPress ? ev.xbutton.time
		  : ev.type == KeyPress ? ev.xkey.time : timestamp(False);
    XUngrabPointer(display(), m_currentTime);
    XUngrabKeyboard(display(), m_currentTime);
    if (ic) {
	XUnsetICFocus(ic);
	XDestroyIC(ic);
	ic = 0;
    }
    for (i = depth - 1; i >= 0; --i) {
	XUnmapWindow(display(), menuLevelWindow(i));
    }
    // back to the queue, in their order, for the main loop
    for (i = deferred.n - 1; i >= 0; --i) {
	XPutBackEvent(display(), &deferred.ev[i]);
    }
    free(deferred.ev);

    if (pickK >= 0) {
	int kind = lv[pickK].kinds[pickRow];
	int idx = lv[pickK].idxs[pickRow];
	if (kind == K_NEW) {
	    spawn();
	} else if (kind == K_LEAF) {
	    spawn(lv[pickK].ents[pickRow]->command);
	} else if (kind == K_HIDDEN) {
	    clients.item(idx)->unhide(True);
	} else if (kind == K_CLIENT) {
	    Client *sc = clients.item(idx);
	    if (sc->isOffDesktop() && !sc->isSticky())
		switchToDesktop(sc->desktop());
	    if (CONFIG_CLICK_TO_FOCUS) sc->activate();
	    else sc->mapRaised();
	    sc->ensureVisible();
	} else if (kind == K_EXIT) {
	    m_signalled = True;
	}
    }

 out:
    for (i = 0; i < depth; ++i) freeMenuLevel(lv[i]);
    if (dark) XftColorFree(display(), vis, m_defaultColormap, &s_hlText);
    XftColorFree(display(), vis, m_defaultColormap, &s_dimText);
    XFreeGC(display(), s_hlGC);
    XFreeGC(display(), s_bgGC);
    XFreeGC(display(), s_fgGC);
}

void WindowManager::showGeometry(int x, int y)
{
    char string[20];
    sprintf(string, "%d %d\n", x, y);
    XGlyphInfo gi;
    XftTextExtentsUtf8(display(), m_menuFont,
		       (FcChar8 *)string, strlen(string), &gi);
    int width = gi.xOff + 8;
    int height = m_menuFont->ascent + m_menuFont->descent + 8;
    const Monitor &mon = pointerMonitor();	// the window being moved is there

    XMoveResizeWindow(display(), m_menuWindow,
		      mon.x + (mon.w - width) / 2, mon.y + (mon.h - height) / 2,
		      width, height);
    XClearWindow(display(), m_menuWindow);
    XMapRaised(display(), m_menuWindow);

    XftDrawStringUtf8(m_menuDraw, &m_menuXftColor, m_menuFont,
		      4, 4 + m_menuFont->ascent,
		      (FcChar8 *)string, strlen(string));
}


void WindowManager::removeGeometry()
{
    XUnmapWindow(display(), m_menuWindow);
}


Boolean Client::move(XButtonEvent *e)
{
    int x = -1, y = -1, xoff, yoff;
    Boolean done = False;

    if (m_windowManager->attemptGrab
	(root(), None, DragMask, e->time) != GrabSuccess) {
	return False;
    }

    xoff = m_border->xIndent() - e->x;
    yoff = m_border->yIndent() - e->y;

    XEvent event;
    Boolean found;
    Boolean doSomething = False;
    int edge = 0;		// -1 / +1 while held on the outer left / right edge
    struct timeval edgeSince;

    while (!done) {

	// held against the outer edge for edge_flip_delay ms: on to the next
	// desktop, and once more after each further wait while still held
	if (edge && msSince(edgeSince) >= wm2config.edge_flip_delay) {
	    if (m_windowManager->flipDesktop(this, edge)) doSomething = True;
	    gettimeofday(&edgeSince, 0);
	}

	found = False;

	while (XCheckMaskEvent(display(), DragMask | ExposureMask, &event)) {
	    found = True;
	    if (event.type != MotionNotify) break;
	}

	if (!found) {
	    waitForX(display(), 50000);
	    continue;
	}
	
	switch (event.type) {

	default:
	    fprintf(stderr, "wm2tj: unknown event type %d\n", event.type);
	    break;

	case Expose:
	    m_windowManager->eventExposure(&event.xexpose);
	    break;

	case ButtonPress:
	    // don't like this
	    XUngrabPointer(display(), event.xbutton.time);
	    doSomething = False;
	    done = True;
	    break;

	case ButtonRelease:

	    x = event.xbutton.x; y = event.xbutton.y;
	    if (!nobuttons(&event.xbutton)) doSomething = False;

//	    XUngrabPointer(display(), event.xbutton.time);
	    m_windowManager->releaseGrab(&event.xbutton);
	    done = True;
	    break;

	case MotionNotify:
	    x = event.xbutton.x; y = event.xbutton.y;
	    if (wm2config.edge_flip_delay > 0) {
		int side = m_windowManager->outerEdge(event.xmotion.x_root,
						      event.xmotion.y_root);
		if (side != edge) { edge = side; gettimeofday(&edgeSince, 0); }
	    }
	    // the panel's strip is off limits (minY), the rest is free
	    if (y + yoff < minYAt(x + xoff, y + yoff))
		y = minYAt(x + xoff, y + yoff) - yoff;
	    if (x + xoff != m_x || y + yoff != m_y) {
		windowManager()->showGeometry(x + xoff, y + yoff);
		m_border->moveTo(x + xoff, y + yoff);
		doSomething = True;
	    }
	    break;
	}
    }

    windowManager()->removeGeometry();

    if (y >= 0 && y + yoff < minYAt(x + xoff, y + yoff))
	y = minYAt(x + xoff, y + yoff) - yoff;

    // a click jitters a pixel or two; don't count that as a move
    Boolean moved = (x >= 0 && doSomething &&
		     (abs(x + xoff - m_x) > 3 || abs(y + yoff - m_y) > 3));
    if (moved) {
	m_x = x + xoff;
	m_y = y + yoff;
	dropMaximize();
    }

    if (CONFIG_CLICK_TO_FOCUS) activate();
    m_border->moveTo(m_x, m_y);
    sendConfigureNotify();
    return moved;
}


void Client::fixResizeDimensions(int &w, int &h, int &dw, int &dh)
{
    if (w < 50) w = 50;
    if (h < 50) h = 50;
    
    if (m_sizeHints.flags & PResizeInc) {
	w = m_minWidth  + (((w - m_minWidth) / m_sizeHints.width_inc) *
			   m_sizeHints.width_inc);
	h = m_minHeight + (((h - m_minHeight) / m_sizeHints.height_inc) *
			   m_sizeHints.height_inc);

	dw = (w - m_minWidth)  / m_sizeHints.width_inc;
	dh = (h - m_minHeight) / m_sizeHints.height_inc;
    } else {
	dw = w; dh = h;
    }

    if (m_sizeHints.flags & PMaxSize) {
	if (w > m_sizeHints.max_width)  w = m_sizeHints.max_width;
	if (h > m_sizeHints.max_height) h = m_sizeHints.max_height;
    }

    if (w < m_minWidth)  w = m_minWidth;
    if (h < m_minHeight) h = m_minHeight;
}


void Client::resize(XButtonEvent *e, Boolean horizontal, Boolean vertical)
{
    if (isFixedSize()) return;

    if (m_windowManager->attemptGrab
	(root(), None, DragMask, e->time) != GrabSuccess) {
	return;
    }

    if (vertical && horizontal)
	m_windowManager->installCursor(WindowManager::DownrightCursor);
    else if (vertical)
	m_windowManager->installCursor(WindowManager::DownCursor);
    else
	m_windowManager->installCursor(WindowManager::RightCursor);

    Window dummy;
    XTranslateCoordinates(display(), e->window, parent(),
			  e->x, e->y, &e->x, &e->y, &dummy);

    int xorig = e->x;
    int yorig = e->y;
    int x = xorig;
    int y = yorig;
    int w = m_w, h = m_h;
    int prevW, prevH;
    int dw, dh;

    XEvent event;
    Boolean found;
    Boolean doSomething = False;
    Boolean done = False;

    while (!done) {

	found = False;

	while (XCheckMaskEvent(display(), DragMask | ExposureMask, &event)) {
	    found = True;
	    if (event.type != MotionNotify) break;
	}

	if (!found) {
	    waitForX(display(), 50000);
	    continue;
	}
	
	switch (event.type) {

	default:
	    fprintf(stderr, "wm2tj: unknown event type %d\n", event.type);
	    break;

	case Expose:
	    m_windowManager->eventExposure(&event.xexpose);
	    break;

	case ButtonPress:
	    // don't like this
	    XUngrabPointer(display(), event.xbutton.time);
	    done = True;
	    break;

	case ButtonRelease:

	    x = event.xbutton.x; y = event.xbutton.y;

	    if (!nobuttons(&event.xbutton)) x = -1;
	    m_windowManager->releaseGrab(&event.xbutton);
	    
	    done = True;
	    break;

	case MotionNotify:
	    x = event.xbutton.x; y = event.xbutton.y;

	    if (vertical && horizontal) {
		prevH = h; h = y - m_y;
		prevW = w; w = x - m_x;
		fixResizeDimensions(w, h, dw, dh);
		if (h == prevH && w == prevW) break;
		m_border->configure(m_x, m_y, w, h, CWWidth | CWHeight, 0);
		windowManager()->showGeometry(dw, dh);
		doSomething = True;

	    } else if (vertical) {
		prevH = h; h = y - m_y;
		fixResizeDimensions(w, h, dw, dh);
		if (h == prevH) break;
		m_border->configure(m_x, m_y, w, h, CWHeight, 0);
		windowManager()->showGeometry(dw, dh);
		doSomething = True;

	    } else {
		prevW = w; w = x - m_x;
		fixResizeDimensions(w, h, dw, dh);
		if (w == prevW) break;
		m_border->configure(m_x, m_y, w, h, CWWidth, 0);
		windowManager()->showGeometry(dw, dh);
		doSomething = True;
	    }

	    break;
	}
    }

    if (doSomething) {

	windowManager()->removeGeometry();
	dropMaximize();

	if (vertical && horizontal) {
	    m_w = x - m_x;
	    m_h = y - m_y;
	    fixResizeDimensions(m_w, m_h, dw, dh);
	    m_border->configure(m_x, m_y, m_w, m_h, CWWidth|CWHeight, 0, True);
	} else if (vertical) {
	    m_h = y - m_y;
	    fixResizeDimensions(m_w, m_h, dw, dh);
	    m_border->configure(m_x, m_y, m_w, m_h, CWHeight, 0, True);
	} else {
	    m_w = x - m_x;
	    fixResizeDimensions(m_w, m_h, dw, dh);
	    m_border->configure(m_x, m_y, m_w, m_h, CWWidth, 0, True);
	}

	XMoveResizeWindow(display(), m_window,
			  m_border->xIndent(), m_border->yIndent(), m_w, m_h);

	sendConfigureNotify();
    }

    m_windowManager->installCursor(WindowManager::NormalCursor);
}


void Client::moveOrResize(XButtonEvent *e)
{
    if (e->x < m_border->xIndent() && e->y > m_h) {
	resize(e, False, True);
    } else if (e->y < m_border->yIndent() &&
	       e->x > m_w + m_border->xIndent() - m_border->yIndent()) { //hack
	resize(e, True, False);
    } else {
	move(e);
    }
}


void Border::eventButton(XButtonEvent *e)
{
    // a press caught by the frame's passive grab names the tab only as
    // subwindow; the tab sits at 0,0 in the frame so coords still match
    if (e->window == m_parent && e->subwindow == m_tab) e->window = m_tab;

    if (e->window == m_parent) {

	if (!m_client->isActive()) return;
	if (isTransient()) {
	    if (e->x >= xIndent() && e->y >= yIndent()) {
		return;
	    } else {
		m_client->move(e);
		return;
	    }
	}

	m_client->moveOrResize(e);
	return;
	
    } else if (e->window == m_tab) {
	// double-click on the tab toggles maximize.  Decided once the
	// button is up, so a quick second press that drags just moves.
	static Window lastTab = None;
	static Time lastTabTime = 0;
	if (m_client->move(e)) {
	    lastTab = None;
	} else if (e->window == lastTab &&
		   e->time - lastTabTime < (Time)CONFIG_DOUBLE_CLICK_TIME) {
	    lastTab = None;
	    m_client->toggleMaximize();
	} else {
	    lastTab = m_tab;
	    lastTabTime = e->time;
	}
	return;
    }

    if (e->window == m_resize) {
	m_client->resize(e, True, True);
	return;
    }

    if (e->window != m_button || e->type == ButtonRelease) return;

    if (windowManager()->attemptGrab(m_button, None, MenuGrabMask, e->time)
	!= GrabSuccess) {
	return;
    }

    XEvent event;
    Boolean found;
    Boolean done = False;
    unsigned long tdiff = 0L;
    int x = e->x;
    int y = e->y;
    int action = 1;
    int buttonSize = m_tabWidth - TAB_TOP_HEIGHT*2 - 4;

    XFillRectangle(display(), m_button, m_drawGC, 0, 0, buttonSize, buttonSize);

    while (!done) {

	found = False;

	if (tdiff > (unsigned long)CONFIG_DESTROY_WINDOW_DELAY && action == 1) {
	    windowManager()->installCursor(WindowManager::DeleteCursor);
	    action = 2;
	}

	while (XCheckMaskEvent(display(), MenuMask, &event)) {
	    found = True;
	    if (event.type != MotionNotify) break;
	}

	if (!found) {
	    tdiff += waitForX(display(), 50000);
	    continue;
	}

	switch (event.type) {

	default:
	    fprintf(stderr, "wm2tj: unknown event type %d\n", event.type);
	    break;

	case Expose:
	    windowManager()->eventExposure(&event.xexpose);
	    break;

	case ButtonPress:
	    break;

	case ButtonRelease:

	    if (!nobuttons(&event.xbutton)) {
		action = 0;
	    }

	    if (x < 0 || y < 0 || x >= buttonSize || y >= buttonSize) {
		action = 0;
	    }

	    windowManager()->releaseGrab(&event.xbutton);
	    done = True;
	    break;

	case MotionNotify:
	    tdiff = event.xmotion.time - e->time;
	    if (tdiff > 5000L) tdiff = 5001L; // in case of overflow!

	    x = event.xmotion.x;
	    y = event.xmotion.y;

	    if (action == 0 || action == 2) {
		if (x < 0 || y < 0 || x >= buttonSize || y >= buttonSize) {
		    windowManager()->installCursor(WindowManager::NormalCursor);
		    action = 0;
		} else {
		    windowManager()->installCursor(WindowManager::DeleteCursor);
		    action = 2;
		}
	    }

	    break;
	}
    }

    XClearWindow(display(), m_button);
    windowManager()->installCursor(WindowManager::NormalCursor);

    if (tdiff > 5000L) {	// do nothing, they dithered too long
	return;
    }

    if (action == 1) m_client->kill();
    else if (action == 2) m_client->kill();
}

