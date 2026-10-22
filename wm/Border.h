
#ifndef _BORDER_H_
#define _BORDER_H_

#include "General.h"
#include <X11/Xft/Xft.h>
#include "Rotated.h"

class Client;
class WindowManager;

// These distances exclude the 1-pixel borders.  You could probably
// change these a certain amount before breaking the shoddy code in
// the rest of this file.

#define TAB_TOP_HEIGHT 2
#define FRAME_WIDTH CONFIG_FRAME_THICKNESS
#define TRANSIENT_FRAME_WIDTH 4
// NB frameTopHeight = frameHeight-tabTopHeight

class Border {			// friend of client
public:
    Border(Client *const, Window child);
    ~Border();

    void map();
    void unmap();
    void lower();
    void mapRaised();
    void decorate(Boolean active, int w, int h);
    void reparent();
    void configure(int x, int y, int w, int h, unsigned long mask, int detail,
		   Boolean force = False);
    void moveTo(int x, int y);

    WindowManager *windowManager(); // calls into Client
    static int edgePixel(WindowManager *, unsigned long *);
    Boolean isTransient();	// calls into Client
    Boolean isFixedSize();	// calls into Client
    Window parent() { return m_parent; }
    Boolean isTabEvent(XButtonEvent *e) {	// directly or via frame grab
	return e->window == m_tab ||
	    (e->window == m_parent && e->subwindow == m_tab);
    }
    Boolean hasWindow(Window w) {
	return (w != root() && (w == m_parent || w == m_tab ||
				w == m_button || w == m_resize));
    }

    Display *display();
    Window root();

    void expose(XExposeEvent *);
    void eventButton(XButtonEvent *); // in Buttons.C

    int yIndent() {
	return isTransient() ? TRANSIENT_FRAME_WIDTH + 1 : FRAME_WIDTH + 1;
    }
    int xIndent() {
	return isTransient() ? TRANSIENT_FRAME_WIDTH + 1 :
	    m_tabWidth + FRAME_WIDTH + 1;
    }

    Boolean coordsInHole(int, int); // in Events.C of all places
    
private:
    Client *m_client;

    Window m_parent;
    Window m_tab;
    Window m_child;
    Window m_button;
    Window m_resize;
    Window m_edge;		// line down the left frame (left_line)

    void fatal(char *);

    char *m_label;

    void fixTabHeight(int);

    void fixTabHeightAuto(int);
    void drawLabel();
    void drawGlossTab();
    static void initStatics(WindowManager *);
    static void glossColour(double *, int, int, int, int);
    static unsigned long packPixel(Visual *, const double *);

    void setFrameVisibility(Boolean, int, int);
    void setTransientFrameVisibility(Boolean, int, int);
    void shapeParent(int, int);
    void shapeTransientParent(int, int);
    void shapeTab(int, int);
    void resizeTab(int);	// for rename without changing window size
    void shapeResize();
    void shapeEdge(int);

    int m_prevW;
    int m_prevH;

private:
    int        m_tabHeight;	// depends on the label
    static int m_tabWidth;	// depends on the label font
    static int m_edgeOn;	// left_line not "none"
    static unsigned long m_edgePixel;
    static int m_labelX;	// label offset across the tab (centred)
    static XftFont *m_tabFont;
    static XftColor m_tabXftColor;
    Pixmap     m_labelPixmap;	// label rendered then rotated 90deg CCW
    int        m_labelPixW, m_labelPixH;
    Pixmap     m_glossPixmap;	// whole glossy tab, label included
    int        m_glossPixH;
    static int m_gloss;		// tab_gloss set and usable
    static double m_glossBase[3], m_glossFg[3];
    static unsigned long m_glossStripPixel;
    static int textWidth(const char *);
    static GC m_drawGC;
    static unsigned long m_foregroundPixel;
    static unsigned long m_backgroundPixel;
    static unsigned long m_frameBackgroundPixel;
    static unsigned long m_buttonBackgroundPixel;
    static unsigned long m_borderPixel;
    static int m_noBorder;	// borders "none": no 1px outline
    void noBorderClip();
};


#endif

