# wm2tj-bar
#   make; make install        -> $(PREFIX)/bin, $(PREFIX)/share/{applications,locale}
# Honours CC, CFLAGS, CPPFLAGS, LDFLAGS, DESTDIR, PKG_CONFIG (distro packaging).
# msgfmt (gettext tools) is optional: without it the committed .mo is used as
# it is, and without any .mo the bar is simply in English.
PREFIX     ?= /usr/local
LOCALEDIR  ?= $(PREFIX)/share/locale
PKG_CONFIG ?= pkg-config
CC         ?= cc
CFLAGS     ?= -O2 -Wall -Wextra
PKGS        = x11 xext xft
CPPFLAGS   += $(shell $(PKG_CONFIG) --cflags $(PKGS)) -DLOCALEDIR='"$(LOCALEDIR)"'
LIBS        = $(shell $(PKG_CONFIG) --libs $(PKGS)) -lm
MSGFMT     := $(shell command -v msgfmt 2>/dev/null)

LANGS      = ja
MOS        = $(LANGS:%=po/%.mo)

all: wm2tj-bar $(MOS)

wm2tj-bar: wm2tj-bar.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ wm2tj-bar.c $(LIBS)

po/%.mo: po/%.po
	$(if $(MSGFMT),$(MSGFMT) -c -o $@ $<,@echo "msgfmt not found; keeping $@ as it is")

# after changing the strings: make pot, then msgmerge -U po/ja.po po/wm2tj-bar.pot
# (the bar and wm2tj-bar-settings share one text domain)
XGETTEXT   = xgettext --keyword=_ --keyword=N_ --from-code=UTF-8 \
	    --add-comments=TRANSLATORS --package-name=wm2tj-bar \
	    --msgid-bugs-address= -o po/wm2tj-bar.pot
pot:
	$(XGETTEXT) -L C wm2tj-bar.c
	$(XGETTEXT) -j -L Python wm2tj-bar-settings

install: all
	install -D -m755 wm2tj-bar $(DESTDIR)$(PREFIX)/bin/wm2tj-bar
	sed 's|@LOCALEDIR@|$(LOCALEDIR)|' wm2tj-bar-settings > wm2tj-bar-settings.tmp
	install -D -m755 wm2tj-bar-settings.tmp $(DESTDIR)$(PREFIX)/bin/wm2tj-bar-settings
	rm -f wm2tj-bar-settings.tmp
	install -D -m644 wm2tj-bar-settings.desktop \
	    $(DESTDIR)$(PREFIX)/share/applications/wm2tj-bar-settings.desktop
	set -e; for l in $(LANGS); do \
	    [ -f po/$$l.mo ] && install -D -m644 po/$$l.mo \
		$(DESTDIR)$(LOCALEDIR)/$$l/LC_MESSAGES/wm2tj-bar.mo || true; \
	done

clean:
	rm -f wm2tj-bar $(MOS)

.PHONY: all pot install clean
