
#include "Border.h"
#include "Client.h"
#include "Manager.h"
#include "Rotated.h"
#include <strings.h>
#include <math.h>

// These are degenerate initialisations, don't change them
int Border::m_tabWidth = -1;
int Border::m_labelX = 2;
XftFont *Border::m_tabFont = 0;
XftColor Border::m_tabXftColor;
static Display *m_tabDisplay = 0;
GC Border::m_drawGC = 0;

unsigned long Border::m_foregroundPixel;
unsigned long Border::m_backgroundPixel;
unsigned long Border::m_frameBackgroundPixel;
unsigned long Border::m_buttonBackgroundPixel;
unsigned long Border::m_borderPixel;
int Border::m_noBorder = 0;
int Border::m_gloss = 0;
int Border::m_edgeOn = 0;
unsigned long Border::m_edgePixel;
double Border::m_glossBase[3], Border::m_glossFg[3];
unsigned long Border::m_glossStripPixel;

static int borderCounter = 0;


declareList(RectangleList, XRectangle);
implementList(RectangleList, XRectangle);

class BorderRectangleList : public RectangleList
{
public:
    BorderRectangleList() { }
    ~BorderRectangleList() { }

    void appendRect(int x, int y, int w, int h);
    void sortYX();
};

// XShapeCombineRectangles(YXSorted) needs y-then-x order; with a thin
// frame (border < 7) the button and frame-edge rects swap places.
void BorderRectangleList::sortYX()
{
    for (long i = 1; i < count(); ++i) {
	XRectangle r = item(i);
	long j = i;
	while (j > 0 && (item(j-1).y > r.y ||
			 (item(j-1).y == r.y && item(j-1).x > r.x))) {
	    item(j) = item(j-1);
	    --j;
	}
	item(j) = r;
    }
}

void BorderRectangleList::appendRect(int x, int y, int w, int h)
{
    XRectangle r;
    r.x = x; r.y = y; r.width = w; r.height = h;
    append(r);
}


// the look shared by every frame: fonts, colours, GC, gloss.  Done once,
// on the first frame or when the root menu wants the tab colour
void Border::initStatics(WindowManager *wm)
{
    if (m_tabFont) return;

    m_tabDisplay = wm->display();
    int scr = DefaultScreen(wm->display());
    if (!(m_tabFont = XftFontOpenName(wm->display(), scr, CONFIG_NICE_FONT)) &&
	!(m_tabFont = XftFontOpenName(wm->display(), scr, CONFIG_NASTY_FONT))) {
	wm->fatal
	    ("couldn't load tab font, bailing out");
    }
    if (!XftColorAllocName(wm->display(), DefaultVisual(wm->display(), scr),
			   DefaultColormap(wm->display(), scr),
			   CONFIG_TAB_FOREGROUND, &m_tabXftColor)) {
	XftColorAllocName(wm->display(), DefaultVisual(wm->display(), scr),
			  DefaultColormap(wm->display(), scr), "black",
			  &m_tabXftColor);
    }

    m_tabWidth = m_tabFont->ascent + m_tabFont->descent + 4;
    if (m_tabWidth < TAB_TOP_HEIGHT * 2 + 8) {
	m_tabWidth = TAB_TOP_HEIGHT * 2 + 8;
    }
    if (m_tabWidth < CONFIG_TAB_WIDTH) m_tabWidth = CONFIG_TAB_WIDTH;
    // label sits centred across the tab (2px in at the automatic width)
    m_labelX = (m_tabWidth - m_tabFont->ascent - m_tabFont->descent) / 2;

    m_foregroundPixel = wm->allocateColour
	(CONFIG_TAB_FOREGROUND, "tab foreground");
    m_backgroundPixel = wm->allocateColour
	(CONFIG_TAB_BACKGROUND, "tab background");
    m_frameBackgroundPixel = wm->allocateColour
	(CONFIG_FRAME_BACKGROUND, "frame background");
    m_buttonBackgroundPixel = wm->allocateColour
	(CONFIG_BUTTON_BACKGROUND, "button background");
    // borders "none": the clip shape is made the same as the bounding
    // one (see noBorderClip), so the 1px outline is never drawn
    m_noBorder = CONFIG_BORDERS && !strcasecmp(CONFIG_BORDERS, "none");
    m_borderPixel = m_noBorder ? m_frameBackgroundPixel :
	wm->allocateColour(CONFIG_BORDERS, "border");

    XGCValues values;
    values.foreground = m_foregroundPixel;
    values.background = m_backgroundPixel;
    values.function = GXcopy;
    values.line_width = 0;
    values.subwindow_mode = IncludeInferiors;
    values.graphics_exposures = False;	// no NoExpose from XCopyArea

    m_drawGC = XCreateGC(wm->display(), wm->root(),
			 GCForeground | GCBackground | GCFunction |
			 GCLineWidth | GCSubwindowMode | GCGraphicsExposures,
			 &values);

    if (!m_drawGC) {
	wm->fatal("couldn't allocate border GC");
    }

    Visual *vis = DefaultVisual(wm->display(), scr);
    XColor b, f;
    Colormap cmap = DefaultColormap(wm->display(), scr);
    if (CONFIG_TAB_GLOSS && vis->c_class == TrueColor &&
	XParseColor(wm->display(), cmap, CONFIG_TAB_GLOSS, &b) &&
	XParseColor(wm->display(), cmap, CONFIG_TAB_FOREGROUND, &f)) {
	m_gloss = 1;
	m_glossBase[0] = b.red / 65535.0;
	m_glossBase[1] = b.green / 65535.0;
	m_glossBase[2] = b.blue / 65535.0;
	m_glossFg[0] = f.red / 65535.0;
	m_glossFg[1] = f.green / 65535.0;
	m_glossFg[2] = f.blue / 65535.0;
	double c[3];
	glossColour(c, m_tabWidth / 2, 1, m_tabWidth + 2, 300);
	m_glossStripPixel = packPixel(vis, c);
    }

    // left_line: "auto" follows the strip along the top of the tab
    // (glossy tab only), "none" turns it off, anything else is a colour
    char *ll = CONFIG_LEFT_LINE;
    if (!ll || !strcasecmp(ll, "none")) {
	m_edgeOn = 0;
    } else if (!strcasecmp(ll, "auto")) {
	m_edgeOn = m_gloss;
	m_edgePixel = m_glossStripPixel;
    } else {
	m_edgeOn = 1;
	m_edgePixel = wm->allocateColour(ll, "left line");
    }
}

// left_line colour, for the root menu's outer frame ("menu_borders auto")
int Border::edgePixel(WindowManager *wm, unsigned long *p)
{
    initStatics(wm);
    if (m_edgeOn) *p = m_edgePixel;
    return m_edgeOn;
}


Border::Border(Client *const c, Window child) :
    m_client(c), m_parent(0), m_tab(0),
    m_child(child), m_button(0), m_resize(0), m_edge(None), m_label(0),
    m_tabHeight(-1), m_prevW(-1), m_prevH(-1),
    m_labelPixmap(None), m_labelPixW(0), m_labelPixH(0),
    m_glossPixmap(None), m_glossPixH(0)
{
    m_parent = root();

    initStatics(windowManager());

    ++borderCounter;
}


Border::~Border()
{
    if (m_parent != root()) {
	if (!m_parent) fprintf(stderr,"wm2tj: zero parent in Border::~Border\n");
	else {
	    XDestroyWindow(display(), m_tab);
	    if (m_edge) XDestroyWindow(display(), m_edge);
	    XDestroyWindow(display(), m_button);
	    XDestroyWindow(display(), m_parent);

	    // bad window if its parent has already gone:
	    XDestroyWindow(display(), m_resize);
	}
    }

    if (m_label) free(m_label);
    if (m_labelPixmap) XFreePixmap(display(), m_labelPixmap);
    if (m_glossPixmap) XFreePixmap(display(), m_glossPixmap);

    if (--borderCounter == 0) {
	XFreeGC(display(), m_drawGC);
    }
}


void Border::fatal(char *s)
{
    windowManager()->fatal(s);
}


Display *Border::display()
{
    return m_client->display();
}


WindowManager *Border::windowManager()
{
    return m_client->windowManager();
}


Window Border::root()
{
    return m_client->root();
}


// Glossy tab (tab_gloss): colours are computed, no image files.
// Along the tab the base colour runs from a pale sky tone at the top
// to a deep shade at the bottom; across it a soft specular streak and
// darker edges make it read as a rounded, shiny bar.
static void mix(double *c, double tr, double tg, double tb, double t)
{
    if (t < 0) t = 0; else if (t > 1) t = 1;
    c[0] += (tr - c[0]) * t;
    c[1] += (tg - c[1]) * t;
    c[2] += (tb - c[2]) * t;
}

void Border::glossColour(double *c, int x, int y, int w, int h)
{
    c[0] = m_glossBase[0]; c[1] = m_glossBase[1]; c[2] = m_glossBase[2];

    // across: deep on the left, light on the right, with a narrow
    // specular streak near the right edge
    double s = (double)x / (w > 1 ? w - 1 : 1);
    mix(c, 0, 0.02, 0.15, 0.50 * pow(1 - s, 1.4));
    mix(c, 1, 1, 1, 0.45 * s * s);
    double g = (s - 0.72) / 0.10;
    mix(c, 1, 1, 1, 0.45 * exp(-g * g));

    // along: a little brighter at the top, a little deeper at the bottom
    double t = (double)y / (h > 1 ? h - 1 : 1);
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

unsigned long Border::packPixel(Visual *v, const double *c)
{
    unsigned long masks[3] = { v->red_mask, v->green_mask, v->blue_mask };
    unsigned long p = 0;
    for (int i = 0; i < 3; ++i) {
	int bits, sh = maskShift(masks[i], &bits);
	unsigned long max = (1UL << bits) - 1;
	p |= ((unsigned long)(c[i] * max + 0.5) & max) << sh;
    }
    return p;
}

void Border::drawGlossTab()
{
    Display *d = display();
    int scr = DefaultScreen(d);
    Visual *vis = DefaultVisual(d, scr);
    int depth = DefaultDepth(d, scr);
    int tw = m_tabWidth + 2;
    int th = m_tabHeight + m_tabWidth + 2;

    if (m_glossPixmap != None && m_glossPixH != th) {
	XFreePixmap(d, m_glossPixmap);
	m_glossPixmap = None;
    }

    if (m_glossPixmap == None) {
	// label coverage: render white-on-black, use green as alpha
	int lw = m_label ? textWidth(m_label) : 0;
	int lh = m_tabFont->ascent + m_tabFont->descent;
	unsigned char *alpha = 0;
	if (lw > 0 && lh > 0) {
	    Pixmap tmp = XCreatePixmap(d, m_tab, lw, lh, depth);
	    XSetForeground(d, m_drawGC, BlackPixel(d, scr));
	    XFillRectangle(d, tmp, m_drawGC, 0, 0, lw, lh);
	    XftColor white;
	    XRenderColor rc = { 0xffff, 0xffff, 0xffff, 0xffff };
	    XftColorAllocValue(d, vis, DefaultColormap(d, scr), &rc, &white);
	    XftDraw *xd = XftDrawCreate(d, tmp, vis, DefaultColormap(d, scr));
	    XftDrawStringUtf8(xd, &white, m_tabFont, 0, m_tabFont->ascent,
			      (const FcChar8 *)m_label, strlen(m_label));
	    XftDrawDestroy(xd);
	    XftColorFree(d, vis, DefaultColormap(d, scr), &white);
	    XImage *src = XGetImage(d, tmp, 0, 0, lw, lh, AllPlanes, ZPixmap);
	    XFreePixmap(d, tmp);
	    if (src) {
		int bits, sh = maskShift(vis->green_mask, &bits);
		unsigned long max = (1UL << bits) - 1;
		alpha = (unsigned char *)malloc(lw * lh);
		for (int y = 0; y < lh; ++y)
		    for (int x = 0; x < lw; ++x)
			alpha[y * lw + x] = (unsigned char)
			    (((XGetPixel(src, x, y) >> sh) & max) * 255 / max);
		XDestroyImage(src);
	    }
	}

	// alpha at tab pixel (x, y): the label is rotated 90deg CCW,
	// its baseline end at the tab's bottom, 2px in from the left
	int y0 = m_tabHeight - lw;
	#define LABEL_A(x, y) ( \
	    (alpha && (x) - m_labelX >= 0 && (x) - m_labelX < lh && \
	     lw - 1 - ((y) - y0) >= 0 && lw - 1 - ((y) - y0) < lw) ? \
	    alpha[((x) - m_labelX) * lw + lw - 1 - ((y) - y0)] / 255.0 : 0.0)

	XImage *img = XCreateImage(d, vis, depth, ZPixmap, 0, 0, tw, th, 32, 0);
	img->data = (char *)malloc(img->bytes_per_line * th);
	for (int y = 0; y < th; ++y) {
	    for (int x = 0; x < tw; ++x) {
		double c[3];
		glossColour(c, x, y, tw, th);
		mix(c, 0, 0, 0.12, 0.6 * LABEL_A(x - 1, y - 1));	// shadow
		mix(c, m_glossFg[0], m_glossFg[1], m_glossFg[2], LABEL_A(x, y));
		XPutPixel(img, x, y, packPixel(vis, c));
	    }
	}
	#undef LABEL_A
	free(alpha);

	m_glossPixmap = XCreatePixmap(d, m_tab, tw, th, depth);
	XPutImage(d, m_glossPixmap, m_drawGC, img, 0, 0, 0, 0, tw, th);
	XDestroyImage(img);
	m_glossPixH = th;
    }

    XClearWindow(d, m_tab);
    XCopyArea(d, m_glossPixmap, m_tab, m_drawGC, 0, 0, tw, th, 0, 0);
}


void Border::expose(XExposeEvent *e)
{
    if (e->window != m_tab) return;
    drawLabel();
}


int Border::textWidth(const char *s)
{
    XGlyphInfo gi;
    XftTextExtentsUtf8(m_tabDisplay, m_tabFont,
		       (const FcChar8 *)s, strlen(s), &gi);
    return gi.xOff;
}


// Render the label horizontally with Xft (antialiased, any script),
// rotate it 90deg counter-clockwise once, and keep it as a pixmap so
// exposes are a plain XCopyArea.
void Border::drawLabel()
{
    if (m_gloss) { drawGlossTab(); return; }
    if (!m_label) return;
    Display *d = display();
    int scr = DefaultScreen(d);

    if (m_labelPixmap == None) {
	int w = textWidth(m_label);
	int h = m_tabFont->ascent + m_tabFont->descent;
	if (w <= 0 || h <= 0) return;
	int depth = DefaultDepth(d, scr);

	Pixmap tmp = XCreatePixmap(d, m_tab, w, h, depth);
	XSetForeground(d, m_drawGC, m_backgroundPixel);
	XFillRectangle(d, tmp, m_drawGC, 0, 0, w, h);
	XSetForeground(d, m_drawGC, m_foregroundPixel);

	XftDraw *xd = XftDrawCreate(d, tmp, DefaultVisual(d, scr),
				    DefaultColormap(d, scr));
	XftDrawStringUtf8(xd, &m_tabXftColor, m_tabFont, 0, m_tabFont->ascent,
			  (const FcChar8 *)m_label, strlen(m_label));
	XftDrawDestroy(xd);

	XImage *src = XGetImage(d, tmp, 0, 0, w, h, AllPlanes, ZPixmap);
	XFreePixmap(d, tmp);
	if (!src) return;

	XImage *dst = XCreateImage(d, DefaultVisual(d, scr), depth, ZPixmap,
				   0, 0, h, w, 32, 0);
	dst->data = (char *)malloc(dst->bytes_per_line * w);
	for (int y = 0; y < h; ++y)
	    for (int x = 0; x < w; ++x)
		XPutPixel(dst, y, w - 1 - x, XGetPixel(src, x, y));

	m_labelPixmap = XCreatePixmap(d, m_tab, h, w, depth);
	XPutImage(d, m_labelPixmap, m_drawGC, dst, 0, 0, 0, 0, h, w);
	XDestroyImage(src);
	XDestroyImage(dst);
	m_labelPixW = h;
	m_labelPixH = w;
    }

    XClearWindow(d, m_tab);
    int ph = m_labelPixH;
    int y = m_tabHeight - 1 - ph;
    int sy = 0;
    if (y < 0) { sy = -y; ph += y; y = 0; }
    XCopyArea(d, m_labelPixmap, m_tab, m_drawGC, 0, sy,
	      m_labelPixW, ph, m_labelX, y);
}


Boolean Border::isTransient(void)
{
    return m_client->isTransient();
}


Boolean Border::isFixedSize(void)
{
    return m_client->isFixedSize();
}


// Fixed-length tab (BeOS style): the title is truncated to fit and the
// tab is padded to CONFIG_TAB_LENGTH, so it never grows with the title.
void Border::fixTabHeight(int maxHeight)
{
    int fixed = CONFIG_TAB_LENGTH;
    if (fixed <= 0) { fixTabHeightAuto(maxHeight); return; }

    int limit = maxHeight - m_tabWidth;	// room left beside the diagonal
    if (fixed < limit) limit = fixed;
    fixTabHeightAuto(limit + m_tabWidth);
    m_tabHeight = limit;
}


void Border::fixTabHeightAuto(int maxHeight)
{
    m_tabHeight = 0x7fff;
    maxHeight -= m_tabWidth;	// for diagonal

    if (m_labelPixmap) { XFreePixmap(display(), m_labelPixmap); m_labelPixmap = None; }
    if (m_glossPixmap) { XFreePixmap(display(), m_glossPixmap); m_glossPixmap = None; }

    if (m_label) free(m_label);
    m_label = NewString(m_client->label());
    
    if (m_label) {
	m_tabHeight =
	    textWidth(m_label) + 6 + m_tabWidth;
    }

    if (m_tabHeight <= maxHeight) return;

    if (m_label) free(m_label);
    m_label = m_client->iconName() ?
	NewString(m_client->iconName()) : NewString("incognito");

    int len = strlen(m_label);
    m_tabHeight = textWidth(m_label) + 6 + m_tabWidth;
    if (m_tabHeight <= maxHeight) return;

    char *newLabel = (char *)malloc(len + 3);

    do {
	// back up to a UTF-8 character boundary
	while (len > 1 && ((unsigned char)m_label[len - 1] & 0xC0) == 0x80) --len;
	strncpy(newLabel, m_label, len - 1);
	strcpy(newLabel + len - 1, "...");
	m_tabHeight = textWidth(newLabel) + 6 + m_tabWidth;
	--len;

    } while (m_tabHeight > maxHeight && len > 2);

    free(m_label);
    m_label = newLabel;

    if (m_tabHeight > maxHeight) m_tabHeight = maxHeight;
}


void Border::shapeTransientParent(int w, int h)
{
    XRectangle r;

    r.x = xIndent() - 1; r.y = yIndent() - 1;
    r.width = w + 2; r.height = h + 2;
    if (m_noBorder) {
	r.x ++; r.y ++; r.width -= 2; r.height -= 2;
    }

    XShapeCombineRectangles
	(display(), m_parent, ShapeBounding, 0, 0, &r, 1, ShapeSet, YXBanded);

    r.x = xIndent(); r.y = yIndent();
    r.width = w; r.height = h;

    XShapeCombineRectangles
	(display(), m_parent, ShapeClip, 0, 0, &r, 1, ShapeSet, YXBanded);
}


void Border::setTransientFrameVisibility(Boolean visible, int w, int h)
{
    int i;
    BorderRectangleList rl;

    rl.appendRect(0, 0, w + 1, yIndent() - 1);
    for (i = 1; i < yIndent(); ++i) {
	rl.appendRect(w + 1, i - 1, i + 1, 1);
    }
    rl.appendRect(0, yIndent() - 1, xIndent() - 1, h - yIndent() + 2);
    for (i = 1; i < yIndent(); ++i) {
	rl.appendRect(i - 1, h, 1, i + 2);
    }

    XShapeCombineRectangles
	(display(), m_parent, ShapeBounding,
	 0, 0, rl.array(0, rl.count()), rl.count(),
	 visible ? ShapeUnion : ShapeSubtract, YXSorted);

    rl.remove_all();

    rl.appendRect(1, 1, w, yIndent() - 2);
    for (i = 2; i < yIndent(); ++i) {
	rl.appendRect(w + 1, i - 1, i, 1);
    }
    rl.appendRect(1, yIndent() - 1, xIndent() - 2, h - yIndent() + 1);
    for (i = 2; i < yIndent(); ++i) {
	rl.appendRect(i - 1, h, 1, i + 1);
    }

    XShapeCombineRectangles
	(display(), m_parent, ShapeClip,
	 0, 0, rl.array(0, rl.count()), rl.count(),
	 visible ? ShapeUnion : ShapeSubtract, YXSorted);
    noBorderClip();
}   


void Border::shapeParent(int w, int h)
{
    int i;
    int mainRect;
    BorderRectangleList rl;

    if (isTransient()) {
	shapeTransientParent(w, h);
	return;
    }

    // Bounding rectangles -- clipping will be the same except for
    // child window border

    // top of tab
    rl.appendRect(0, 0, w + m_tabWidth + 1, TAB_TOP_HEIGHT + 2);

    // struts in tab, left...
    rl.appendRect(0, TAB_TOP_HEIGHT + 1,
		  TAB_TOP_HEIGHT + 2, m_tabWidth - TAB_TOP_HEIGHT*2 - 1);

    // ...and right
    rl.appendRect(m_tabWidth - TAB_TOP_HEIGHT, TAB_TOP_HEIGHT + 1,
		  TAB_TOP_HEIGHT + 2, m_tabWidth - TAB_TOP_HEIGHT*2 - 1);

    mainRect = rl.count();
    if (m_noBorder) rl.appendRect(xIndent(), yIndent(), w, h);
    else rl.appendRect(xIndent() - 1, yIndent() - 1, w + 2, h + 2);

    // main tab
    rl.appendRect(0, m_tabWidth - TAB_TOP_HEIGHT, m_tabWidth + 2,
		  m_tabHeight - m_tabWidth + TAB_TOP_HEIGHT);

    // diagonal
    for (i = 1; i < m_tabWidth - 1; ++i) {
	rl.appendRect(i, m_tabHeight + i - 1, m_tabWidth - i + 2, 1);
    }

    XShapeCombineRectangles
	(display(), m_parent, ShapeBounding,
	 0, 0, rl.array(0, rl.count()), rl.count(), ShapeSet, YXSorted);

    if (!m_noBorder) {
	rl.item(mainRect).x ++;
	rl.item(mainRect).y ++;
	rl.item(mainRect).width -= 2;
	rl.item(mainRect).height -= 2;
    }

    XShapeCombineRectangles
	(display(), m_parent, ShapeClip,
	 0, 0, rl.array(0, rl.count()), rl.count(), ShapeSet, YXSorted);
    noBorderClip();
}


void Border::shapeTab(int w, int)
{
    int i;
    BorderRectangleList rl;

    if (isTransient()) {
	return;
    }

    // Bounding rectangles

    rl.appendRect(0, 0, w + m_tabWidth + 1, TAB_TOP_HEIGHT + 2);
    rl.appendRect(0, TAB_TOP_HEIGHT + 1, TAB_TOP_HEIGHT + 2,
		  m_tabWidth - TAB_TOP_HEIGHT*2 - 1);

    rl.appendRect(m_tabWidth - TAB_TOP_HEIGHT, TAB_TOP_HEIGHT + 1,
		  TAB_TOP_HEIGHT + 2, m_tabWidth - TAB_TOP_HEIGHT*2 - 1);

    rl.appendRect(0, m_tabWidth - TAB_TOP_HEIGHT, m_tabWidth + 2,
		  m_tabHeight - m_tabWidth + TAB_TOP_HEIGHT);

    for (i = 1; i < m_tabWidth - 1; ++i) {
	rl.appendRect(i, m_tabHeight + i - 1, m_tabWidth - i + 2, 1);
    }

    XShapeCombineRectangles
	(display(), m_tab, ShapeBounding,
	 0, 0, rl.array(0, rl.count()), rl.count(), ShapeSet, YXSorted);

    rl.remove_all();

    // Clipping rectangles

    rl.appendRect(1, 1, w + m_tabWidth - 1, TAB_TOP_HEIGHT);

    rl.appendRect(1, TAB_TOP_HEIGHT + 1, TAB_TOP_HEIGHT,
		  m_tabWidth + TAB_TOP_HEIGHT*2 - 1);

    rl.appendRect(m_tabWidth - TAB_TOP_HEIGHT + 1, TAB_TOP_HEIGHT + 1,
		  TAB_TOP_HEIGHT, m_tabWidth + TAB_TOP_HEIGHT*2 - 1);

    rl.appendRect(1, m_tabWidth - TAB_TOP_HEIGHT + 1, m_tabWidth,
		  m_tabHeight - m_tabWidth + TAB_TOP_HEIGHT - 1);

    for (i = 1; i < m_tabWidth - 2; ++i) {
	rl.appendRect(i + 1, m_tabHeight + i - 1, m_tabWidth - i, 1);
    }

    XShapeCombineRectangles
	(display(), m_tab, ShapeClip,
	 0, 0, rl.array(0, rl.count()), rl.count(), ShapeSet, YXSorted);
    noBorderClip();
}


void Border::resizeTab(int h)
{
    int i;
    XRectangle r;
    BorderRectangleList rl;
    int shorter, longer, operation;

    if (isTransient()) {
	return;
    }

    int prevTabHeight = m_tabHeight;
    fixTabHeight(h);	// also reloads the label from the client
    if (m_tabHeight == prevTabHeight) {
	// same height: X sends no Expose, so paint the new label here
	drawLabel();
	return;
    }

    XWindowChanges wc;
    wc.height = m_tabHeight + 2 + m_tabWidth;
    XConfigureWindow(display(), m_tab, CWHeight, &wc);

    if (m_tabHeight > prevTabHeight) {

	shorter = prevTabHeight;
	longer = m_tabHeight;
	operation = ShapeUnion;

    } else {

	shorter = m_tabHeight;
	longer = prevTabHeight + m_tabWidth;
	operation = ShapeSubtract;
    }

    r.x = 0; r.y = shorter /*- 2*/;
    r.width = m_tabWidth + 2; r.height = longer - shorter;

    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
			    0, 0, &r, 1, operation, YXBanded);

    XShapeCombineRectangles(display(), m_parent, ShapeClip,
			    0, 0, &r, 1, operation, YXBanded);

    XShapeCombineRectangles(display(), m_tab, ShapeBounding,
			    0, 0, &r, 1, operation, YXBanded);

    r.x ++; r.width -= 2;

    XShapeCombineRectangles(display(), m_tab, ShapeClip,
			    0, 0, &r, 1, operation, YXBanded);

    if (m_client->isActive()) {
	// restore a bit of the frame edge
	r.x = m_tabWidth + 1; r.y = shorter;
	r.width = FRAME_WIDTH - 1; r.height = longer - shorter;
	XShapeCombineRectangles(display(), m_parent, ShapeBounding,
				0, 0, &r, 1, ShapeUnion, YXBanded);
    }

    for (i = 1; i < m_tabWidth - 1; ++i) {
	rl.appendRect(i, m_tabHeight + i - 1, m_tabWidth - i + 2, 1);
    }
	
    XShapeCombineRectangles
	(display(), m_parent, ShapeBounding,
	 0, 0, rl.array(0, rl.count()), rl.count(), ShapeUnion, YXBanded);

    XShapeCombineRectangles
	(display(), m_parent, ShapeClip,
	 0, 0, rl.array(0, rl.count()), rl.count(), ShapeUnion, YXBanded);

    XShapeCombineRectangles
	(display(), m_tab, ShapeBounding,
	 0, 0, rl.array(0, rl.count()), rl.count(), ShapeUnion, YXBanded);

    if (rl.count() >= 2) {
	for (i = 0; i < rl.count() - 1; ++i) {
	    rl.item(i).x ++; rl.item(i).width -= 2;
	}

	XShapeCombineRectangles
	    (display(), m_tab, ShapeClip,
	     0, 0, rl.array(0, rl.count() - 1), rl.count() - 1,
	     ShapeUnion, YXBanded);
    }
    noBorderClip();
    drawLabel();	// a shrinking tab gets no Expose either
}


void Border::shapeResize()
{
    int i;
    BorderRectangleList rl;

    for (i = 0; i < FRAME_WIDTH*2; ++i) {
	rl.appendRect(FRAME_WIDTH*2 - i - 1, i, i + 1, 1);
    }

    XShapeCombineRectangles
	(display(), m_resize, ShapeBounding, 0, 0,
	 rl.array(0, rl.count()), rl.count(), ShapeSet, YXBanded);

    rl.remove_all();

    for (i = 1; i < FRAME_WIDTH*2; ++i) {
	rl.appendRect(FRAME_WIDTH*2 - i, i, i, 1);
    }

    XShapeCombineRectangles
	(display(), m_resize, ShapeClip, 0, 0,
	 rl.array(0, rl.count()), rl.count(), ShapeSet, YXBanded);

    rl.remove_all();

    for (i = 0; i < FRAME_WIDTH*2 - 3; ++i) {
	rl.appendRect(FRAME_WIDTH*2 - i - 1, i + 3, 1, 1);
    }

    XShapeCombineRectangles
	(display(), m_resize, ShapeClip, 0, 0,
	 rl.array(0, rl.count()), rl.count(), ShapeSubtract, YXBanded);

    windowManager()->installCursorOnWindow
	(WindowManager::DownrightCursor, m_resize);
}


// borders "none": the outline is the part of the bounding shape outside
// the clip shape, so copying bounding onto clip leaves no outline
void Border::noBorderClip()
{
    if (!m_noBorder || m_parent == root()) return;
    XShapeCombineShape(display(), m_parent, ShapeClip, 0, 0,
		       m_parent, ShapeBounding, ShapeSet);
    if (m_tab && !isTransient())
	XShapeCombineShape(display(), m_tab, ShapeClip, 0, 0,
			   m_tab, ShapeBounding, ShapeSet);
}


// Keep the left edge line showing like the top strip, even when the
// rest of the frame is hidden for an inactive window.
void Border::shapeEdge(int h)
{
    if (!m_edge) return;

    XRectangle r;
    r.x = m_tabWidth + 2;
    r.y = TAB_TOP_HEIGHT + 1;
    r.width = TAB_TOP_HEIGHT;
    r.height = h + yIndent() - TAB_TOP_HEIGHT - 2;

    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
			    0, 0, &r, 1, ShapeUnion, YXBanded);
    XShapeCombineRectangles(display(), m_parent, ShapeClip,
			    0, 0, &r, 1, ShapeUnion, YXBanded);
}


void Border::setFrameVisibility(Boolean visible, int w, int h)
{
    BorderRectangleList rl;

    if (CONFIG_PROD_SHAPE) {
	shapeParent(w, h);
	shapeTab(w, h);
    }

    if (isTransient()) {
	setTransientFrameVisibility(visible, w, h);
	return;
    }

    // Bounding rectangles

    rl.appendRect(m_tabWidth + w + 1, 0, FRAME_WIDTH + 1, FRAME_WIDTH);

    rl.appendRect(m_tabWidth + 2, TAB_TOP_HEIGHT + 2, w,
		  FRAME_WIDTH - TAB_TOP_HEIGHT - 2);

    // for button
    int ww = m_tabWidth - TAB_TOP_HEIGHT*2 - 4;
    rl.appendRect((m_tabWidth + 2 - ww) / 2, (m_tabWidth+2 - ww) / 2, ww, ww);

    // frame edge beside the tab, then below it down to the bottom
    int edgeH = m_tabHeight + m_tabWidth - FRAME_WIDTH - 2;
    rl.appendRect(m_tabWidth + 2, FRAME_WIDTH, FRAME_WIDTH - 2, edgeH);
    rl.appendRect(m_tabWidth + 1, FRAME_WIDTH + edgeH,
		  FRAME_WIDTH - 1, h - edgeH + 2);
    rl.sortYX();

    XShapeCombineRectangles(display(), m_parent, ShapeBounding,
			    0, 0, rl.array(0, rl.count()), rl.count(),
			    visible ? ShapeUnion : ShapeSubtract, YXSorted);
    rl.remove_all();

    // Clip rectangles

    rl.appendRect(m_tabWidth + w + 1, 1, FRAME_WIDTH, FRAME_WIDTH - 1);

    rl.appendRect(m_tabWidth + 2, TAB_TOP_HEIGHT + 2, w,
		  FRAME_WIDTH - TAB_TOP_HEIGHT - 2);

    // for button
    ww = m_tabWidth - TAB_TOP_HEIGHT*2 - 6;
    rl.appendRect((m_tabWidth + 2 - ww) / 2, (m_tabWidth+2 - ww) / 2, ww, ww);

    rl.appendRect(m_tabWidth + 2, FRAME_WIDTH,
		  FRAME_WIDTH - 2, h - FRAME_WIDTH);
    rl.appendRect(m_tabWidth + 2, h, FRAME_WIDTH - 2, FRAME_WIDTH + 1);
    rl.sortYX();

    XShapeCombineRectangles(display(), m_parent, ShapeClip,
			    0, 0, rl.array(0, rl.count()), rl.count(),
			    visible ? ShapeUnion : ShapeSubtract, YXSorted);
    rl.remove_all();

    shapeEdge(h);
    noBorderClip();

    if (visible && !isFixedSize()) {
	XMapRaised(display(), m_resize);
    } else {
	XUnmapWindow(display(), m_resize);
    }
}


void Border::configure(int x, int y, int w, int h,
		       unsigned long mask, int detail,
		       Boolean force) // must reshape everything
{
    if (!m_parent || m_parent == root()) {

	// create windows, then shape them afterwards

	// An ARGB (depth 32) client keeps its alpha only if the frame is
	// ARGB too, otherwise the compositor sees an opaque 24-bit frame.
	// Tab and button stay on the default visual so the root GCs and
	// Xft drawing still match them.
	XWindowAttributes ca;
	Boolean argb = XGetWindowAttributes(display(), m_child, &ca) &&
	    ca.depth == 32 && ca.visual != DefaultVisual(display(), DefaultScreen(display()));

	if (argb) {
	    static Visual *argbVisual = 0;
	    static Colormap argbColormap = None;
	    if (argbVisual != ca.visual) {
		if (argbColormap) XFreeColormap(display(), argbColormap);
		argbColormap = XCreateColormap
		    (display(), root(), ca.visual, AllocNone);
		argbVisual = ca.visual;
	    }
	    XSetWindowAttributes pa;
	    pa.colormap = argbColormap;
	    pa.border_pixel = m_borderPixel | 0xff000000UL;
	    pa.background_pixel = m_frameBackgroundPixel | 0xff000000UL;
	    m_parent = XCreateWindow
		(display(), root(), 1, 1, 1, 1, 0, 32, InputOutput,
		 ca.visual, CWColormap | CWBorderPixel | CWBackPixel, &pa);
	} else {
	    m_parent = XCreateSimpleWindow
		(display(), root(), 1, 1, 1, 1, 0,
		 m_borderPixel, m_frameBackgroundPixel);
	}

	XSetWindowAttributes ta;
	unsigned long tmask = CWBorderPixel | CWBackPixel;
	ta.colormap = DefaultColormap(display(), DefaultScreen(display()));
	if (argb) tmask |= CWColormap;
	int dDepth = DefaultDepth(display(), DefaultScreen(display()));
	Visual *dVisual = DefaultVisual(display(), DefaultScreen(display()));

	ta.border_pixel = m_borderPixel;
	ta.background_pixel = m_gloss ? m_glossStripPixel : m_backgroundPixel;
	m_tab = XCreateWindow
	    (display(), m_parent, 1, 1, 1, 1, 0, dDepth, InputOutput,
	     dVisual, tmask, &ta);

	// same colour as the strip along the top of the tab; created
	// after m_tab so it sits over the tab's border at the corner
	if (m_edgeOn && !isTransient()) {
	    ta.background_pixel = m_edgePixel;
	    m_edge = XCreateWindow
		(display(), m_parent, 1, 1, 1, 1, 0, dDepth, InputOutput,
		 dVisual, tmask, &ta);
	}

	ta.background_pixel = m_buttonBackgroundPixel;
	m_button = XCreateWindow
	    (display(), m_parent, 1, 1, 1, 1, 0, dDepth, InputOutput,
	     dVisual, tmask, &ta);

	m_resize = XCreateWindow
	    (display(), m_child, 1, 1, FRAME_WIDTH*2, FRAME_WIDTH*2, 0,
	     CopyFromParent, InputOutput, CopyFromParent, 0L, 0);

	shapeResize();

	XSelectInput(display(), m_parent,
		     SubstructureRedirectMask | SubstructureNotifyMask |
		     ButtonPressMask | ButtonReleaseMask);

	if (!isTransient()) {
	    XSelectInput(display(), m_tab,
			 ExposureMask | ButtonPressMask | ButtonReleaseMask |
			 EnterWindowMask/* | LeaveWindowMask*/);
	}

	XSelectInput(display(), m_button,
		     ButtonPressMask | ButtonReleaseMask/* | LeaveWindowMask*/);
	XSelectInput(display(), m_resize, ButtonPressMask | ButtonReleaseMask);
	mask |= CWX | CWY | CWWidth | CWHeight | CWBorderWidth;
    }

    XWindowChanges wc;
    wc.x = x - xIndent();
    wc.y = y - yIndent();
    wc.width  = w + xIndent() + 1;
    wc.height = h + yIndent() + 1;
    wc.border_width = 0;
    wc.sibling = None;
    wc.stack_mode = detail;
    XConfigureWindow(display(), m_parent, mask, &wc);

    unsigned long rmask = 0L;
    if (mask & CWWidth)  rmask |= CWX;
    if (mask & CWHeight) rmask |= CWY;
    wc.x = w - FRAME_WIDTH*2;
    wc.y = h - FRAME_WIDTH*2;
    XConfigureWindow(display(), m_resize, rmask, &wc);
    
    if (force ||
	(m_prevW < 0 || m_prevH < 0) ||
	((mask & (CWWidth | CWHeight)) && (w != m_prevW || h != m_prevH))) {

	int prevTabHeight = m_tabHeight;
	if (isTransient()) m_tabHeight = 10; // arbitrary
	else fixTabHeight(h);

	shapeParent(w, h);
	setFrameVisibility(m_client->isActive(), w, h);

	if (force ||
	    prevTabHeight != m_tabHeight || m_prevW < 0 || m_prevH < 0) {

	    wc.x = 0;
	    wc.y = 0;
	    wc.width = w + xIndent();
	    wc.height = m_tabHeight + 2 + m_tabWidth;
	    XConfigureWindow(display(), m_tab, mask, &wc);
	    shapeTab(w, h);
	}
	drawLabel();	// fixTabHeight reloaded the label; no Expose if the tab kept its size

	m_prevW = w;
	m_prevH = h;

    } else {

	resizeTab(h);
    }

    wc.x = TAB_TOP_HEIGHT + 2;
    wc.y = wc.x;
    wc.width = wc.height = m_tabWidth - TAB_TOP_HEIGHT*2 - 4;
    XConfigureWindow(display(), m_button, mask, &wc);

    if (m_edge) {
	XMoveResizeWindow(display(), m_edge, m_tabWidth + 2,
			  TAB_TOP_HEIGHT + 1, TAB_TOP_HEIGHT,
			  h + yIndent() - TAB_TOP_HEIGHT - 2);
    }
}


void Border::moveTo(int x, int y)
{
    XWindowChanges wc;
    wc.x = x - xIndent();
    wc.y = y - yIndent();
    XConfigureWindow(display(), m_parent, CWX | CWY, &wc);
}


void Border::map()
{
    if (m_parent == root()) {
	fprintf(stderr, "wm2tj: bad parent in Border::map()\n");
    } else {
	XMapWindow(display(), m_parent);

	if (!isTransient()) {
	    XMapWindow(display(), m_tab);
	    if (m_edge) XMapWindow(display(), m_edge);
	    XMapWindow(display(), m_button);
	    if (!isFixedSize()) XMapWindow(display(), m_resize);
	}
    }
}


void Border::mapRaised()
{
    if (m_parent == root()) {
	fprintf(stderr, "wm2tj: bad parent in Border::mapRaised()\n");
    } else {
	XMapRaised(display(), m_parent);

	if (!isTransient()) {
	    XMapWindow(display(), m_tab);
	    if (m_edge) XMapWindow(display(), m_edge);
	    XMapRaised(display(), m_button);
	    if (!isFixedSize()) XMapRaised(display(), m_resize);
	}
    }
}


void Border::lower()
{
    XLowerWindow(display(), m_parent);
}


void Border::unmap()
{
    if (m_parent == root()) {
	fprintf(stderr, "wm2tj: bad parent in Border::unmap()\n");
    } else {
	XUnmapWindow(display(), m_parent);

	if (!isTransient()) {
	    XUnmapWindow(display(), m_tab);
	    if (m_edge) XUnmapWindow(display(), m_edge);
	    XUnmapWindow(display(), m_button);
//	    XUnmapWindow(display(), m_resize); // no, will unmap with parent
	}
    }
}


void Border::decorate(Boolean active, int w, int h)
{
    setFrameVisibility(active, w, h);
}


void Border::reparent()
{
    XReparentWindow(display(), m_child, m_parent, xIndent(), yIndent());
}

