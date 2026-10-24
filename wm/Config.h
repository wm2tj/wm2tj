
#ifndef _CONFIG_H_
#define _CONFIG_H_

struct WM2MenuEntry {
    char *label;
    char *command;             /* NULL for submenu containers */
    WM2MenuEntry *children;    /* NULL for leaf entries */
    int           child_count;
};

struct WM2Config {
    char *nice_font;
    char *nice_menu_font;
    char *nasty_font;
    char *new_window_command;
    char *new_label;
    char *submenu_prefix;   // put before submenu names on the root menu (2026-10-21)
    char *search_label;     // placeholder of the search row on the root menu, "none" for no search
    char *submenu_style;    // "cascade" opens beside the menu, "replace" swaps it (2026-10-21)

    int   exec_using_shell;
    int   everything_on_root_menu;
    int   click_to_focus;
    int   raise_on_focus;
    int   auto_raise;

    int   auto_raise_delay;
    int   pointer_stopped_delay;
    long  destroy_window_delay;

    char *tab_foreground;
    char *tab_background;
    char *frame_background;
    char *button_background;
    char *borders;
    char *tab_gloss;             /* base colour of a glossy tab, NULL = flat */
    char *left_line;             /* line down the left frame: auto, none or a colour */

    char *menu_foreground;
    char *menu_background;
    char *menu_borders;

    int   frame_thickness;
    int   prod_shape;
    int   tab_length;            /* 0 = fit to title */
    int   tab_width;             /* min tab thickness (px), default 32; 0 = font height + 4 */
    int   double_click_time;     /* ms, tab double-click = maximize */

    int   num_desktops;
    int   dynamic_desktops;      /* GNOME-like: keep one empty desktop at the end */

    WM2MenuEntry *user_menu;
    int           user_menu_count;
};

extern WM2Config wm2config;

void wm2LoadConfig(void);
int  wm2ReloadMenu(void);	// at menu open: 1 if ~/.wm2rc changed and the menu was read again

#define CONFIG_NICE_FONT              (wm2config.nice_font)
#define CONFIG_NICE_MENU_FONT         (wm2config.nice_menu_font)
#define CONFIG_NASTY_FONT             (wm2config.nasty_font)

#define CONFIG_EXEC_USING_SHELL       (wm2config.exec_using_shell)
#define CONFIG_NEW_WINDOW_COMMAND     (wm2config.new_window_command)
#define CONFIG_NEW_LABEL              (wm2config.new_label)
#define CONFIG_SUBMENU_PREFIX         (wm2config.submenu_prefix)
#define CONFIG_SEARCH_LABEL           (wm2config.search_label)
#define CONFIG_SUBMENU_STYLE          (wm2config.submenu_style)
#define CONFIG_EVERYTHING_ON_ROOT_MENU (wm2config.everything_on_root_menu)

#define CONFIG_CLICK_TO_FOCUS         (wm2config.click_to_focus)
#define CONFIG_RAISE_ON_FOCUS         (wm2config.raise_on_focus)
#define CONFIG_AUTO_RAISE             (wm2config.auto_raise)

#define CONFIG_AUTO_RAISE_DELAY       (wm2config.auto_raise_delay)
#define CONFIG_POINTER_STOPPED_DELAY  (wm2config.pointer_stopped_delay)
#define CONFIG_DESTROY_WINDOW_DELAY   (wm2config.destroy_window_delay)

#define CONFIG_TAB_FOREGROUND         (wm2config.tab_foreground)
#define CONFIG_TAB_BACKGROUND         (wm2config.tab_background)
#define CONFIG_FRAME_BACKGROUND       (wm2config.frame_background)
#define CONFIG_BUTTON_BACKGROUND      (wm2config.button_background)
#define CONFIG_BORDERS                (wm2config.borders)
#define CONFIG_TAB_GLOSS              (wm2config.tab_gloss)
#define CONFIG_LEFT_LINE              (wm2config.left_line)

#define CONFIG_MENU_FOREGROUND        (wm2config.menu_foreground)
#define CONFIG_MENU_BACKGROUND        (wm2config.menu_background)
#define CONFIG_MENU_BORDERS           (wm2config.menu_borders)

#define CONFIG_FRAME_THICKNESS        (wm2config.frame_thickness)
#define CONFIG_PROD_SHAPE             (wm2config.prod_shape)
#define CONFIG_TAB_LENGTH             (wm2config.tab_length)
#define CONFIG_TAB_WIDTH              (wm2config.tab_width)
#define CONFIG_DOUBLE_CLICK_TIME      (wm2config.double_click_time)

#endif
