# wm2tj helper scripts: the root menu generator and the settings GUIs.
#   make install                      -> /usr/local/bin, /usr/local/share/locale
#   make install PREFIX=$HOME/.local  -> for one user (~/.local/bin)
# The optional systemd user units go to $(PREFIX)/share/systemd/user, which
# systemd --user reads for both prefixes (~/.local/share and /usr/local/share).
PREFIX    ?= /usr/local
LOCALEDIR ?= $(PREFIX)/share/locale
UNITDIR   ?= $(PREFIX)/share/systemd/user

SCRIPTS    = wm2-menu-gen wm2tj-settings wm2tj-favorites wm2-wallpaper \
	     session-logout power-dialog
UNITS      = wm2-menu-sync.path wm2-menu-sync.service
LANGS      = ja
MOS        = $(LANGS:%=po/%.mo)
# msgfmt (gettext tools) is optional: without it the committed .mo files are
# used as they are, and without any .mo the programs are simply in English.
MSGFMT    := $(shell command -v msgfmt 2>/dev/null)

all: $(MOS)

po/%.mo: po/%.po
	$(if $(MSGFMT),$(MSGFMT) -c -o $@ $<,@echo "msgfmt not found; keeping $@ as it is")

# after changing the strings: make pot, then msgmerge -U po/ja.po po/wm2tj.pot
XGETTEXT   = xgettext --keyword=_ --keyword=N_ --from-code=UTF-8 \
	    --add-comments=TRANSLATORS --package-name=wm2tj \
	    --msgid-bugs-address= -o po/wm2tj.pot
pot:
	$(XGETTEXT) -L Python wm2-menu-gen wm2tj-settings wm2tj-favorites power-dialog
	$(XGETTEXT) -j -L Shell wm2-wallpaper session-logout

install: all
	set -e; for s in $(SCRIPTS); do \
	    sed 's|@LOCALEDIR@|$(LOCALEDIR)|' $$s > $$s.tmp && \
	    install -D -m755 $$s.tmp $(DESTDIR)$(PREFIX)/bin/$$s && rm -f $$s.tmp; \
	done
	set -e; for l in $(LANGS); do \
	    [ -f po/$$l.mo ] && install -D -m644 po/$$l.mo \
		$(DESTDIR)$(LOCALEDIR)/$$l/LC_MESSAGES/wm2tj.mo || true; \
	done
	set -e; for u in $(UNITS); do \
	    sed 's|@BINDIR@|$(PREFIX)/bin|' $$u > $$u.tmp && \
	    install -D -m644 $$u.tmp $(DESTDIR)$(UNITDIR)/$$u && rm -f $$u.tmp; \
	done

clean:
	rm -f $(MOS)

.PHONY: all pot install clean
