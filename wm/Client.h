
#ifndef _CLIENT_H_
#define _CLIENT_H_

#include "General.h"
#include "Manager.h"
#include "Border.h"


class Client {
public:
    Client(WindowManager *const, Window);
    void release();

    /* for call from WindowManager: */

    void activate();		/* active() */
    void refocus();		// re-give X focus to the active client
    void deactivate();		/* setactive(0) */
    void gravitate(Boolean invert);
    void installColormap();
    void unreparent();
    void withdraw(Boolean = True);
    void hide();
    void unhide(Boolean map);

    int  desktop() { return m_desktop; }
    void setDesktop(int d) { m_desktop = d; }
    Boolean isOffDesktop() { return m_offDesktop; }
    Boolean isSticky() { return m_sticky; }
    void setSticky(Boolean s) { m_sticky = s; }
    void hideForDesktop();
    void unhideForDesktop();
    void rename();
    void kill();
    void mapRaised();		// without activating
    void lower();

    Boolean move(XButtonEvent *);	// event for grab timestamp & coords; True if moved
    void resize(XButtonEvent *, Boolean, Boolean);
    void moveOrResize(XButtonEvent *);
    void ensureVisible();	// make sure x, y are on-screen
    int  minY();		// lowest y the client may have: below the top strut
    void keepOffStrut();	// move down if a dock now covers the top

    // maximize / fullscreen (EWMH _NET_WM_STATE)
    void toggleMaximize();
    void setNetState(Boolean maxH, Boolean maxV, Boolean fullscreen);
    void applyStateGeometry();
    Boolean isMaximized()  { return m_maxH || m_maxV; }
    Boolean isFullscreen() { return m_fullscreen; }
    void eventNetWmState(XClientMessageEvent *);

    void manage(Boolean mapped);
    Boolean hasWindow(Window w) {
	return ((m_window == w) || m_border->hasWindow(w));
    }

    Client *revertTo() { return m_revert; }
    void setRevertTo(Client *c) { m_revert = c; }

    Boolean isHidden()     { return (m_state == IconicState);    }
    Boolean isWithdrawn()  { return (m_state == WithdrawnState); }
    Boolean isNormal()     { return (m_state == NormalState);    }
    Boolean isTransient()  { return (m_transient != None);       }
    Window  transientFor() { return m_transient; }
    Boolean isFixedSize()  { return m_fixedSize; }

    const char *label()    { return m_label;    }
    const char *name()     { return m_name;     }
    const char *iconName() { return m_iconName; }

    void sendMessage(Atom, long);
    void sendConfigureNotify();

    void activateAndWarp();
    void focusIfAppropriate(Boolean);
    void selectOnMotion(Window, Boolean);

    /* for call from within: */

    void fatal(char *m)    { m_windowManager->fatal(m);              }
    Display *display()     { return m_windowManager->display();      }
    Window parent()        { return m_border->parent();              }
    Window window()        { return m_window;                        }
    Boolean isManaged()    { return m_managed;                       }
    Window root()          { return m_windowManager->root();         }
    Client *activeClient() { return m_windowManager->activeClient(); }
    Boolean isActive()     { return (activeClient() == this);        }

    WindowManager *windowManager() { return m_windowManager; }

    // for call from equivalent wm functions in Events.C:

    void eventButton(XButtonEvent *);
    void eventMapRequest(XMapRequestEvent *);
    void eventConfigureRequest(XConfigureRequestEvent *);
    void eventUnmap(XUnmapEvent *);
    void eventColormap(XColormapEvent *);
    void eventProperty(XPropertyEvent *);
    void eventEnter(XCrossingEvent *);
    void eventFocusIn(XFocusInEvent *);
    void eventExposure(XExposeEvent *);

protected:      // cravenly submitting to gcc's warnings
    ~Client();

private:

    Window m_window;
    Window m_transient;
    Border *m_border;

    Client *m_revert;

    int m_x;
    int m_y;
    int m_w;
    int m_h;
    int m_bw;

    XSizeHints m_sizeHints;
    Boolean m_fixedSize;
    int m_minWidth;
    int m_minHeight;
    void fixResizeDimensions(int &, int &, int &, int &);
    Boolean coordsInHole(int, int);

    int m_state;
    int m_protocol;
    Boolean m_managed;
    Boolean m_reparenting;
    Boolean m_stubborn;		// keeps popping itself to the front
    Time m_lastPopTime;

    int m_desktop;
    Boolean m_offDesktop;
    Boolean m_adoptOffDesktop;	// restart: adopt hidden, not mapped
    int m_ignoreUnmaps;		// UnmapNotifys caused by our own XUnmapWindow
    void unmapClientWindow();
    Boolean m_sticky;

    Boolean m_maxH;
    Boolean m_maxV;
    Boolean m_fullscreen;
    Boolean m_haveRestore;	// geometry before maximize/fullscreen
    int m_restoreX, m_restoreY, m_restoreW, m_restoreH;
    void readNetState();
    void writeNetState();
    void dropMaximize();	// user moved/resized by hand

    char *m_name;
    char *m_iconName;
    const char *m_label;	// alias: one of (instance,class,name,iconName)
    static const char *const m_defaultLabel;

    Colormap m_colormap;
    int m_colormapWinCount;
    Window *m_colormapWindows;
    Colormap *m_windowColormaps;

    WindowManager *const m_windowManager;

    char *getProperty(Atom);

    // accessors 
    Boolean getState(int *);
    void setState(int);

    // internal instantiation requests
    Boolean setLabel(void);	// returns True if changed
    void getColormaps(void);
    void getProtocols(void);
    void getTransient(void);

    void decorate(Boolean active);
};

#define Pdelete    1
#define PtakeFocus 2

#endif

