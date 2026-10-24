
#include "Config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>

WM2Config wm2config;

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *r = (char *)malloc(n);
    if (r) memcpy(r, s, n);
    return r;
}

static void setDefaults(WM2Config *c)
{
    c->nice_font          = xstrdup("Noto Sans CJK JP:style=Bold:pixelsize=13");
    c->nice_menu_font     = xstrdup("Hack-11");
    c->nasty_font         = xstrdup("sans-10");
    // empty: the user's terminal ($TERMINAL, x-terminal-emulator, ...),
    // see WindowManager::spawn() (2026-10-23; was "xterm" and "New")
    c->new_window_command = xstrdup("");
    c->new_label          = xstrdup("Terminal");
    c->submenu_prefix     = xstrdup("");
    c->submenu_style      = xstrdup("cascade");
    c->search_label       = xstrdup("Search...");

    c->exec_using_shell        = 0;
    c->everything_on_root_menu = 0;
    c->click_to_focus          = 0;
    c->raise_on_focus          = 0;
    c->auto_raise              = 0;

    c->auto_raise_delay      = 400;
    c->pointer_stopped_delay = 80;
    c->destroy_window_delay  = 1500L;

    // default look: glossy blue tab, white text, thin black borders
    // ("tab_gloss none" in ~/.wm2rc gives the flat tab_background)
    c->tab_foreground    = xstrdup("white");
    c->tab_background    = xstrdup("gray80");
    c->frame_background  = xstrdup("gray95");
    c->button_background = xstrdup("#1a4f9c");
    c->borders           = xstrdup("black");
    c->tab_gloss         = xstrdup("#2a7de1");
    c->left_line         = xstrdup("auto");  // auto = same as the strip on top of the tab

    c->menu_foreground = xstrdup("black");
    c->menu_background = xstrdup("gray80");
    c->menu_borders    = xstrdup("auto");   // auto = same as left_line (the tab colour), else borders

    c->frame_thickness = 7;
    c->prod_shape      = 0;
    c->tab_length      = 300;
    c->tab_width       = 32;
    c->double_click_time = 1300;

    c->num_desktops = 1;
    c->dynamic_desktops = 0;

    c->user_menu       = NULL;
    c->user_menu_count = 0;
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

// Rest of line as a shell command (new_window): a " # comment" outside
// quotes is dropped, and a value that is one "quoted" string loses its
// quotes (the old one-token form keeps working). NULL if empty.
static char *readCommand(char *p)
{
    p = readRest(p);
    if (!p) return NULL;

    char quote = 0;
    for (char *s = p; *s; ++s) {
        if (quote) { if (*s == quote) quote = 0; }
        else if (*s == '"' || *s == '\'') quote = *s;
        else if (*s == '#' && isspace((unsigned char)s[-1])) { *s = '\0'; break; }
    }
    char *e = p + strlen(p);
    while (e > p && isspace((unsigned char)e[-1])) *--e = '\0';

    size_t n = strlen(p);
    if (n >= 2 && p[0] == '"' && p[n - 1] == '"' && !memchr(p + 1, '"', n - 2)) {
        p[n - 1] = '\0';
        ++p;
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

static void loadConfig(WM2Config *c, struct stat *st)
{
    setDefaults(c);

    const char *home = getenv("HOME");
    if (!home) return;

    char path[1024];
    snprintf(path, sizeof(path), "%s/.wm2rc", home);

    FILE *fp = fopen(path, "r");
    if (!fp) return;
    if (fstat(fileno(fp), st)) memset(st, 0, sizeof(*st));

    /* Parse stack for nested submenus. stack[top] points at the list
       being appended to. Only the top is modified, so deeper realloc
       cannot invalidate ancestor pointers. */
    struct { WM2MenuEntry **list; int *count; } parseStack[16];
    int parseTop = 0;
    parseStack[0].list  = &c->user_menu;
    parseStack[0].count = &c->user_menu_count;

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

        // new_window takes the whole rest of the line, arguments included
        // (readToken kept only "tilix" of "tilix --session=..."; 2026-10-23)
        if (!strcmp(key, "new_window")) {
            char *cmd = readCommand(p);
            if (cmd) replaceStr(&c->new_window_command, cmd);
            continue;
        }

        char *val;
        p = readToken(p, &val);
        if (!val) continue;

        if      (!strcmp(key, "font"))               replaceStr(&c->nice_font, val);
        else if (!strcmp(key, "menufont"))           replaceStr(&c->nice_menu_font, val);
        else if (!strcmp(key, "nasty_font"))         replaceStr(&c->nasty_font, val);
        else if (!strcmp(key, "new_label"))          replaceStr(&c->new_label, val);
        else if (!strcmp(key, "submenu_prefix"))     replaceStr(&c->submenu_prefix, val);
        else if (!strcmp(key, "search_label"))       replaceStr(&c->search_label, val);
        else if (!strcmp(key, "submenu_style"))      replaceStr(&c->submenu_style, val);
        else if (!strcmp(key, "exec_using_shell"))   c->exec_using_shell = parseBool(val);
        else if (!strcmp(key, "everything_on_root")) c->everything_on_root_menu = parseBool(val);
        else if (!strcmp(key, "click_to_focus"))     c->click_to_focus = parseBool(val);
        else if (!strcmp(key, "raise_on_focus"))     c->raise_on_focus = parseBool(val);
        else if (!strcmp(key, "auto_raise"))         c->auto_raise = parseBool(val);
        else if (!strcmp(key, "auto_raise_delay"))   c->auto_raise_delay = atoi(val);
        else if (!strcmp(key, "pointer_stopped_delay")) c->pointer_stopped_delay = atoi(val);
        else if (!strcmp(key, "destroy_window_delay")) c->destroy_window_delay = atol(val);
        else if (!strcmp(key, "border")) {
            // frame strip above the client is border-4 px; below 4 the
            // shapes get negative sizes (BadMatch)
            int n = atoi(val);
            if (n < 4) n = 4;
            c->frame_thickness = n;
        }
        else if (!strcmp(key, "tab_length"))         c->tab_length = atoi(val);
        else if (!strcmp(key, "tab_width"))          c->tab_width = atoi(val);
        else if (!strcmp(key, "double_click_time"))  c->double_click_time = atoi(val);
        else if (!strcmp(key, "prod_shape"))         c->prod_shape = parseBool(val);
        else if (!strcmp(key, "tab_foreground"))     replaceStr(&c->tab_foreground, val);
        else if (!strcmp(key, "tab_background"))     replaceStr(&c->tab_background, val);
        else if (!strcmp(key, "frame_background"))   replaceStr(&c->frame_background, val);
        else if (!strcmp(key, "button_background"))  replaceStr(&c->button_background, val);
        else if (!strcmp(key, "borders"))            replaceStr(&c->borders, val);
        else if (!strcmp(key, "tab_gloss"))          replaceStr(&c->tab_gloss, val);
        else if (!strcmp(key, "left_line"))          replaceStr(&c->left_line, val);
        else if (!strcmp(key, "menu_foreground"))    replaceStr(&c->menu_foreground, val);
        else if (!strcmp(key, "menu_background"))    replaceStr(&c->menu_background, val);
        else if (!strcmp(key, "menu_borders"))       replaceStr(&c->menu_borders, val);
        else if (!strcmp(key, "dynamic_desktops"))   c->dynamic_desktops = parseBool(val);
        else if (!strcmp(key, "desktops")) {
            int n = atoi(val);
            if (n < 1)  n = 1;
            if (n > 16) n = 16;
            c->num_desktops = n;
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

// ~/.wm2rc as last read: the menu is read again when this changes
static struct stat s_loaded;

static int sameFile(const struct stat *a, const struct stat *b)
{
    return a->st_ino == b->st_ino && a->st_size == b->st_size &&
	a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
	a->st_mtim.tv_nsec == b->st_mtim.tv_nsec;
}

static int statRc(struct stat *st)
{
    memset(st, 0, sizeof(*st));
    const char *home = getenv("HOME");
    if (!home) return -1;
    char path[1024];
    snprintf(path, sizeof(path), "%s/.wm2rc", home);
    return stat(path, st);
}

void wm2LoadConfig(void)
{
    memset(&s_loaded, 0, sizeof(s_loaded));
    loadConfig(&wm2config, &s_loaded);
}

static void freeMenu(WM2MenuEntry *m, int n)
{
    for (int i = 0; i < n; ++i) {
	free(m[i].label);
	free(m[i].command);
	freeMenu(m[i].children, m[i].child_count);
    }
    free(m);
}

static void takeStr(char **dst, char **src)
{
    free(*dst);
    *dst = *src;
    *src = NULL;
}

// Menu part of a newer ~/.wm2rc (wm2-menu-gen rebuilt it after apps were
// installed or the favourites changed): the entries and the settings that
// are only read when the menu opens.  The rest (fonts, colours, frames)
// is set up at start and still needs a restart (SIGHUP).  Restarting for
// the menu re-framed every window, so the screen flickered and windows
// jumped for each app installed (2026-10-24).
int wm2ReloadMenu(void)
{
    struct stat now;
    statRc(&now);
    if (sameFile(&now, &s_loaded)) return 0;

    WM2Config c;
    struct stat st;
    memset(&c, 0, sizeof(c));
    memset(&st, 0, sizeof(st));
    loadConfig(&c, &st);

    freeMenu(wm2config.user_menu, wm2config.user_menu_count);
    wm2config.user_menu       = c.user_menu;
    wm2config.user_menu_count = c.user_menu_count;
    takeStr(&wm2config.new_window_command, &c.new_window_command);
    takeStr(&wm2config.new_label,          &c.new_label);
    takeStr(&wm2config.submenu_prefix,     &c.submenu_prefix);
    takeStr(&wm2config.search_label,       &c.search_label);
    takeStr(&wm2config.submenu_style,      &c.submenu_style);
    wm2config.everything_on_root_menu = c.everything_on_root_menu;
    wm2config.exec_using_shell        = c.exec_using_shell;

    char **rest[] = {
	&c.nice_font, &c.nice_menu_font, &c.nasty_font,
	&c.tab_foreground, &c.tab_background, &c.frame_background,
	&c.button_background, &c.borders, &c.tab_gloss, &c.left_line,
	&c.menu_foreground, &c.menu_background, &c.menu_borders,
    };
    for (unsigned i = 0; i < sizeof(rest) / sizeof(rest[0]); ++i) free(*rest[i]);

    // read while still being written: keep the old stamp, so the next
    // menu reads the finished file
    statRc(&now);
    if (sameFile(&now, &st)) s_loaded = st;
    return 1;
}
