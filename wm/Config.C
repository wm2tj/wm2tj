
#include "Config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

WM2Config wm2config;

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

static void setDefaults(void)
{
    wm2config.nice_font          = xstrdup("Noto Sans CJK JP:style=Bold:pixelsize=13");
    wm2config.nice_menu_font     = xstrdup("Hack-11");
    wm2config.nasty_font         = xstrdup("sans-10");
    wm2config.new_window_command = xstrdup("xterm");
    wm2config.new_label          = xstrdup("New");
    wm2config.submenu_prefix     = xstrdup("");
    wm2config.submenu_style      = xstrdup("cascade");
    wm2config.search_label       = xstrdup("Search...");

    wm2config.exec_using_shell        = 0;
    wm2config.everything_on_root_menu = 0;
    wm2config.click_to_focus          = 0;
    wm2config.raise_on_focus          = 0;
    wm2config.auto_raise              = 0;

    wm2config.auto_raise_delay      = 400;
    wm2config.pointer_stopped_delay = 80;
    wm2config.destroy_window_delay  = 1500L;

    // default look: glossy blue tab, white text, thin black borders
    // ("tab_gloss none" in ~/.wm2rc gives the flat tab_background)
    wm2config.tab_foreground    = xstrdup("white");
    wm2config.tab_background    = xstrdup("gray80");
    wm2config.frame_background  = xstrdup("gray95");
    wm2config.button_background = xstrdup("#1a4f9c");
    wm2config.borders           = xstrdup("black");
    wm2config.tab_gloss         = xstrdup("#2a7de1");
    wm2config.left_line         = xstrdup("auto");  // auto = same as the strip on top of the tab

    wm2config.menu_foreground = xstrdup("black");
    wm2config.menu_background = xstrdup("gray80");
    wm2config.menu_borders    = xstrdup("auto");   // auto = same as left_line (the tab colour), else borders

    wm2config.frame_thickness = 7;
    wm2config.prod_shape      = 0;
    wm2config.tab_length      = 300;
    wm2config.tab_width       = 32;
    wm2config.double_click_time = 1300;

    wm2config.num_desktops = 1;
    wm2config.dynamic_desktops = 0;

    wm2config.user_menu       = NULL;
    wm2config.user_menu_count = 0;
}

static char *skipWS(char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    return s;
}

// Reads one whitespace-separated or "quoted" token from p.
// Null-terminates the token in place and returns position just past it.
// Sets *out to the token start, or NULL if none found.
static char *readToken(char *p, char **out)
{
    p = skipWS(p);
    if (!*p || *p == '#') { *out = NULL; return p; }

    if (*p == '"') {
        ++p;
        *out = p;
        while (*p && *p != '"') ++p;
        if (*p == '"') { *p = '\0'; ++p; }
    } else {
        *out = p;
        while (*p && !isspace((unsigned char)*p)) ++p;
        if (*p) { *p = '\0'; ++p; }
    }
    return p;
}

// Returns rest of line after stripping leading WS, trailing space/newline.
// Returns NULL if nothing meaningful remains.
static char *readRest(char *p)
{
    p = skipWS(p);
    if (!*p || *p == '#') return NULL;
    char *e = p + strlen(p);
    while (e > p && (e[-1] == '\n' || e[-1] == '\r' ||
                     isspace((unsigned char)e[-1]))) {
        --e;
        *e = '\0';
    }
    return *p ? p : NULL;
}

static int parseBool(const char *v)
{
    if (!v) return 0;
    if (!strcasecmp(v, "true") || !strcasecmp(v, "yes") ||
        !strcasecmp(v, "on")   || !strcmp(v, "1")) return 1;
    return 0;
}

static void replaceStr(char **dst, const char *v)
{
    if (!v) return;
    free(*dst);
    *dst = xstrdup(v);
}

/* Append a new entry (leaf or submenu container) to the list pointed to
   by *list / *count. Returns the index of the new entry. */
static int appendEntryTo(WM2MenuEntry **list, int *count,
                         const char *label, const char *cmd)
{
    int n = *count;
    *list = (WM2MenuEntry *)realloc(*list, sizeof(WM2MenuEntry) * (n + 1));
    (*list)[n].label       = xstrdup(label);
    (*list)[n].command     = cmd ? xstrdup(cmd) : NULL;
    (*list)[n].children    = NULL;
    (*list)[n].child_count = 0;
    *count = n + 1;
    return n;
}

void wm2LoadConfig(void)
{
    setDefaults();

    const char *home = getenv("HOME");
    if (!home) return;

    char path[1024];
    snprintf(path, sizeof(path), "%s/.wm2rc", home);

    FILE *fp = fopen(path, "r");
    if (!fp) return;

    /* Parse stack for nested submenus. stack[top] points at the list
       being appended to. Only the top is modified, so deeper realloc
       cannot invalidate ancestor pointers. */
    struct { WM2MenuEntry **list; int *count; } parseStack[16];
    int parseTop = 0;
    parseStack[0].list  = &wm2config.user_menu;
    parseStack[0].count = &wm2config.user_menu_count;

    char line[2048];
    int lineno = 0;
    while (fgets(line, sizeof(line), fp)) {
        ++lineno;
        char *p = line;
        char *key;
        p = readToken(p, &key);
        if (!key) continue;

        if (!strcmp(key, "menu")) {
            char *label;
            p = readToken(p, &label);
            if (!label) {
                fprintf(stderr, "wm2tj: %s:%d: 'menu' needs a label\n", path, lineno);
                continue;
            }
            char labelCopy[256];
            strncpy(labelCopy, label, sizeof(labelCopy) - 1);
            labelCopy[sizeof(labelCopy) - 1] = '\0';

            char *cmd = readRest(p);
            if (!cmd) {
                fprintf(stderr, "wm2tj: %s:%d: 'menu \"%s\"' needs a command\n",
                        path, lineno, labelCopy);
                continue;
            }
            appendEntryTo(parseStack[parseTop].list,
                          parseStack[parseTop].count, labelCopy, cmd);
            continue;
        }

        if (!strcmp(key, "submenu")) {
            char *label;
            p = readToken(p, &label);
            if (!label) {
                fprintf(stderr, "wm2tj: %s:%d: 'submenu' needs a label\n", path, lineno);
                continue;
            }
            char labelCopy[256];
            strncpy(labelCopy, label, sizeof(labelCopy) - 1);
            labelCopy[sizeof(labelCopy) - 1] = '\0';

            char *rest = readRest(p);
            if (!rest || strcmp(rest, "{") != 0) {
                fprintf(stderr, "wm2tj: %s:%d: 'submenu \"%s\"' must end with '{'\n",
                        path, lineno, labelCopy);
                continue;
            }
            int idx = appendEntryTo(parseStack[parseTop].list,
                                    parseStack[parseTop].count,
                                    labelCopy, NULL);
            if (parseTop + 1 >= 16) {
                fprintf(stderr, "wm2tj: %s:%d: submenu nesting too deep\n",
                        path, lineno);
                continue;
            }
            WM2MenuEntry *parentArr = *parseStack[parseTop].list;
            parseTop++;
            parseStack[parseTop].list  = &parentArr[idx].children;
            parseStack[parseTop].count = &parentArr[idx].child_count;
            continue;
        }

        if (!strcmp(key, "}")) {
            if (parseTop == 0) {
                fprintf(stderr, "wm2tj: %s:%d: unexpected '}'\n", path, lineno);
                continue;
            }
            parseTop--;
            continue;
        }

        char *val;
        p = readToken(p, &val);
        if (!val) continue;

        if      (!strcmp(key, "font"))               replaceStr(&wm2config.nice_font, val);
        else if (!strcmp(key, "menufont"))           replaceStr(&wm2config.nice_menu_font, val);
        else if (!strcmp(key, "nasty_font"))         replaceStr(&wm2config.nasty_font, val);
        else if (!strcmp(key, "new_window"))         replaceStr(&wm2config.new_window_command, val);
        else if (!strcmp(key, "new_label"))          replaceStr(&wm2config.new_label, val);
        else if (!strcmp(key, "submenu_prefix"))     replaceStr(&wm2config.submenu_prefix, val);
        else if (!strcmp(key, "search_label"))       replaceStr(&wm2config.search_label, val);
        else if (!strcmp(key, "submenu_style"))      replaceStr(&wm2config.submenu_style, val);
        else if (!strcmp(key, "exec_using_shell"))   wm2config.exec_using_shell = parseBool(val);
        else if (!strcmp(key, "everything_on_root")) wm2config.everything_on_root_menu = parseBool(val);
        else if (!strcmp(key, "click_to_focus"))     wm2config.click_to_focus = parseBool(val);
        else if (!strcmp(key, "raise_on_focus"))     wm2config.raise_on_focus = parseBool(val);
        else if (!strcmp(key, "auto_raise"))         wm2config.auto_raise = parseBool(val);
        else if (!strcmp(key, "auto_raise_delay"))   wm2config.auto_raise_delay = atoi(val);
        else if (!strcmp(key, "pointer_stopped_delay")) wm2config.pointer_stopped_delay = atoi(val);
        else if (!strcmp(key, "destroy_window_delay")) wm2config.destroy_window_delay = atol(val);
        else if (!strcmp(key, "border")) {
            // frame strip above the client is border-4 px; below 4 the
            // shapes get negative sizes (BadMatch)
            int n = atoi(val);
            if (n < 4) n = 4;
            wm2config.frame_thickness = n;
        }
        else if (!strcmp(key, "tab_length"))         wm2config.tab_length = atoi(val);
        else if (!strcmp(key, "tab_width"))          wm2config.tab_width = atoi(val);
        else if (!strcmp(key, "double_click_time"))  wm2config.double_click_time = atoi(val);
        else if (!strcmp(key, "prod_shape"))         wm2config.prod_shape = parseBool(val);
        else if (!strcmp(key, "tab_foreground"))     replaceStr(&wm2config.tab_foreground, val);
        else if (!strcmp(key, "tab_background"))     replaceStr(&wm2config.tab_background, val);
        else if (!strcmp(key, "frame_background"))   replaceStr(&wm2config.frame_background, val);
        else if (!strcmp(key, "button_background"))  replaceStr(&wm2config.button_background, val);
        else if (!strcmp(key, "borders"))            replaceStr(&wm2config.borders, val);
        else if (!strcmp(key, "tab_gloss"))          replaceStr(&wm2config.tab_gloss, val);
        else if (!strcmp(key, "left_line"))          replaceStr(&wm2config.left_line, val);
        else if (!strcmp(key, "menu_foreground"))    replaceStr(&wm2config.menu_foreground, val);
        else if (!strcmp(key, "menu_background"))    replaceStr(&wm2config.menu_background, val);
        else if (!strcmp(key, "menu_borders"))       replaceStr(&wm2config.menu_borders, val);
        else if (!strcmp(key, "dynamic_desktops"))   wm2config.dynamic_desktops = parseBool(val);
        else if (!strcmp(key, "desktops")) {
            int n = atoi(val);
            if (n < 1)  n = 1;
            if (n > 16) n = 16;
            wm2config.num_desktops = n;
        }
        else {
            fprintf(stderr, "wm2tj: %s:%d: unknown key '%s'\n", path, lineno, key);
        }
    }
    fclose(fp);

    if (parseTop != 0) {
        fprintf(stderr, "wm2tj: %s: missing '}' for %d open submenu(s)\n",
                path, parseTop);
    }
}
