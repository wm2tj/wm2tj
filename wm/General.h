
#ifndef _GENERAL_H_
#define _GENERAL_H_

#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <X11/extensions/shape.h>

// True and False are defined in Xlib.h
typedef char Boolean;

#define NewString(x) (strcpy((char *)malloc(strlen(x)+1),(x)))

// Install a signal handler with sigaction (no SA_RESTART: select() in
// nextEvent returns EINTR so the main loop sees m_signalled at once).
// Handlers take an int; the old (void (*)(...)) cast was a g++ -fpermissive
// only construct and clang rejects it.
#define installSignal(x,y)     \
  do { \
    struct sigaction sAct; \
    (void)sigemptyset(&sAct.sa_mask); \
    sAct.sa_flags = 0; \
    sAct.sa_handler = (y); \
    (void)sigaction((x), &sAct, NULL); \
  } while (0)

#include "Config.h"

class Atoms {
public:
    static Atom wm_state;
    static Atom wm_changeState;
    static Atom wm_protocols;
    static Atom wm_delete;
    static Atom wm_takeFocus;
    static Atom wm_colormaps;
    static Atom wm2_running;

    static Atom net_supported;
    static Atom net_number_of_desktops;
    static Atom net_current_desktop;
    static Atom net_wm_desktop;
    static Atom net_client_list;
    static Atom net_client_list_stacking;
    static Atom net_active_window;
    static Atom net_supporting_wm_check;
    static Atom net_wm_name;
    static Atom utf8_string;
    static Atom net_wm_window_type;
    static Atom net_wm_window_type_dock;
    static Atom net_wm_window_type_desktop;
    static Atom net_wm_window_type_notification;
    static Atom net_wm_window_type_splash;
    static Atom net_wm_window_type_tooltip;
    static Atom net_wm_window_type_popup_menu;
    static Atom net_wm_window_type_dropdown_menu;
    static Atom net_wm_window_type_menu;
    static Atom net_wm_state;
    static Atom net_wm_state_maximized_vert;
    static Atom net_wm_state_maximized_horz;
    static Atom net_wm_state_fullscreen;
    static Atom wm2_restore_geometry;
    static Atom wm2_off_desktop;
    static Atom wm2tj_menu_reload;
    static Atom net_wm_strut;
    static Atom net_wm_strut_partial;
    static Atom net_workarea;
};

extern Boolean ignoreBadWindowErrors; // tidiness hack

#endif
