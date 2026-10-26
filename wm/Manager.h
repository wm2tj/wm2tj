
#ifndef _MANAGER_H_
#define _MANAGER_H_

#include "General.h"
#include "listmacro2.h"
#include <X11/Xft/Xft.h>

class Client;
declarePList(ClientList, Client);


#define MENU_DEPTH 8		// submenu levels
#define MENU_BORDER_WIDTH 3	// outer frame of the root menu

class WindowManager {
public:
    WindowManager();
    ~WindowManager();

    [[noreturn]] void fatal(const char *);

    // for call from Client and within:

    Client *windowToClient(Window, Boolean create = False);
    Client *activeClient() { return m_activeClient; }
    Boolean raiseTransients(Client *); // true if raised any
    Time timestamp(Boolean reset);
    void clearFocus();

    void setActiveClient(Client *const c) { m_activeClient = c; updateActiveWindow(); }

    void addToHiddenList(Client *);
    void removeFromHiddenList(Client *);
    void skipInRevert(Client *, Client *);

    Display *display() { return m_display; }
    Window root() { return m_root; }

    enum RootCursor {
	NormalCursor, DeleteCursor, DownCursor, RightCursor, DownrightCursor
    };

    void installCursor(RootCursor);
    void installCursorOnWindow(RootCursor, Window);
    void installColormap(Colormap);
    unsigned long allocateColour(const char *, const char *);

    void considerFocusChange(Client *, Window, Time timestamp);
    void stopConsideringFocus();

    int  currentDesktop() { return m_currentDesktop; }
    int  numDesktops()    { return m_numDesktops;    }
    void switchToDesktop(int);
    void updateNetCurrentDesktop();
    void publishDesktops();
    void updateDynamicDesktops();
    Boolean flipDesktop(Client *, int dir);	// take a dragged window to the next desktop

    // EWMH / panel support
    void updateClientList();
    void updateActiveWindow();
    Boolean isUnmanagedType(Window, Boolean *isDock);
    void mapUnmanaged(Window, Boolean isDock);
    void forgetDock(Window);
    void raiseDocks();
    int  strutTop()    { return m_strutTop; }
    int  strutBottom() { return m_strutBottom; }

    // Monitors (XRandR), each with the strips its docks take at the top
    // and bottom.  Maximize, fullscreen and the off-limits strip are per
    // monitor; without XRandR the whole screen is one monitor.
    struct Monitor { int x, y, w, h, top, bottom, left, right; };
    const Monitor &monitorFor(int x, int y, int w, int h);
    const Monitor &monitorAt(int x, int y) { return monitorFor(x, y, 1, 1); }
    const Monitor &pointerMonitor();
    int  topLimit(int cx, int top);	// lowest frame top allowed at x = cx
    int  outerEdge(int x, int y);	// -1 / +1 on the outer left / right edge, else 0

    // shouldn't really be public
    int attemptGrab(Window, Window, int, int);
    void releaseGrab(XButtonEvent *);
    void eventExposure(XExposeEvent *);	// for exposures during client grab
    void showGeometry(int, int);
    void removeGeometry();

private:
    int loop();
    void release();

    Display *m_display;
    int m_screenNumber;

    Window m_root;

    Colormap m_defaultColormap;
    int m_minimumColormaps;

    Cursor m_cursor;
    Cursor m_xCursor;
    Cursor m_vCursor;
    Cursor m_hCursor;
    Cursor m_vhCursor;
    
    char *m_terminal;
    char *m_shell;

    ClientList m_clients;
    ClientList m_hiddenClients;
    Client *m_activeClient;

    int m_shapeEvent;
    int m_currentTime;

    int m_currentDesktop;
    int m_numDesktops;

    Window m_wmCheck;
    Window m_docks[32];
    int    m_dockCount;
    int    m_strutTop;
    int    m_strutBottom;
#define MAX_MONITORS 16
    Monitor m_monitors[MAX_MONITORS];
    int    m_monitorCount;
    int    m_rrEvent;		// XRandR event base, -1 without XRandR
    void   updateMonitors();
    Boolean isDock(Window);
    void   recomputeStruts();
    void   publishWorkArea();

    Boolean m_looping;
    int m_returnCode;

    static Boolean m_initialising;
    static int errorHandler(Display *, XErrorEvent *);
    static void sigHandler(int);
    static volatile sig_atomic_t m_signalled;
    static void hupHandler(int);
public:
    static int m_restart;		// set by SIGHUP: re-exec after release()
private:

    void initialiseScreen();
    void scanInitialWindows();

    GC m_menuGC;
    Window m_menuWindow;
    Window m_subMenuWindow[MENU_DEPTH - 1];	// cascading submenus
    XftDraw *m_subMenuDraw[MENU_DEPTH - 1];
    XftFont *m_menuFont;
    XftDraw *m_menuDraw;
    XftColor m_menuXftColor;
    unsigned long m_menuForegroundPixel;
    unsigned long m_menuBackgroundPixel;
    unsigned long m_menuBorderPixel;
    static const char *const m_menuCreateLabel;
    void menu(XButtonEvent *);
    int  runMenuLoop(int &anchor_x, int &anchor_y,
                     const char **labels, int n);
    void cascadeMenu(XButtonEvent *, ClientList &, int, Boolean);
    Window menuLevelWindow(int k);
    Boolean isMenuWindow(Window);
    void sizeMenuLevel(struct MenuLevel &);
    void drawMenuLevel(struct MenuLevel &, int k);
    void highlightMenuRow(struct MenuLevel &, int k, int row);
    void drawMenuRow(struct MenuLevel &, int k, int row);
    void spawn();
    void spawn(const char *command);
    void circulate(Boolean activeFirst);

    Boolean m_focusChanging;	// checking times for focus change
    Client *m_focusCandidate;
    Window  m_focusCandidateWindow;
    Time    m_focusTimestamp;	// time of last crossing event
    Boolean m_focusPointerMoved;
    Boolean m_focusPointerNowStill;
    void checkDelaysForFocus();

    void nextEvent(XEvent *);	// return

    void eventButton(XButtonEvent *);
    void eventMapRequest(XMapRequestEvent *);
    void eventConfigureRequest(XConfigureRequestEvent *);
    void eventUnmap(XUnmapEvent *);
    void eventCreate(XCreateWindowEvent *);
    void eventDestroy(XDestroyWindowEvent *);
    void eventClient(XClientMessageEvent *);
    void eventColormap(XColormapEvent *);
    void eventProperty(XPropertyEvent *);
    void eventEnter(XCrossingEvent *);
    void eventReparent(XReparentEvent *);
    void eventFocusIn(XFocusInEvent *);
};

#endif

