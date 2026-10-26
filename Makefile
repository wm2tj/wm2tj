# wm2tj — top-level build.  The three parts each have their own Makefile
# (wm/ the window manager, bar/ the top bar, wm/extras/ the helper scripts);
# this one just runs them, so a distro package needs only "make" and
# "make install".
#   make                                    build wm2tj and wm2tj-bar
#   sudo make install                       -> /usr/local, with the xsession entry
#   make install PREFIX=/usr DESTDIR=pkg    for packaging (see debian/)
# Honours CC, CXX, CFLAGS, CXXFLAGS, CPPFLAGS, LDFLAGS, PKG_CONFIG, PREFIX,
# DESTDIR, XSESSIONDIR, LOCALEDIR and UNITDIR (passed on to the parts).
PREFIX ?= /usr/local
MANDIR ?= $(PREFIX)/share/man
# The version shown by --version and in "About wm2tj": the top entry of
# debian/changelog (e.g. 0.1.0~beta10), so a build from source says which
# beta it is too.  Every part gets the same one.
VERSION ?= $(shell sed -n '1s/^[^(]*(\([^)]*\)).*/\1/p' debian/changelog 2>/dev/null)
V = $(if $(VERSION),VERSION='$(VERSION)')

all:
	$(MAKE) -C wm $(V)
	$(MAKE) -C bar $(V)
	$(MAKE) -C wm/extras $(V)

install: all
	$(MAKE) -C wm install-session PREFIX='$(PREFIX)' $(V)
	$(MAKE) -C bar install PREFIX='$(PREFIX)' $(V)
	$(MAKE) -C wm/extras install PREFIX='$(PREFIX)' $(V)
	install -D -m644 man/wm2tj.1     $(DESTDIR)$(MANDIR)/man1/wm2tj.1
	install -D -m644 man/wm2tj-bar.1 $(DESTDIR)$(MANDIR)/man1/wm2tj-bar.1
	install -D -m644 man/wm2-swipe.1 $(DESTDIR)$(MANDIR)/man1/wm2-swipe.1

# (wm/extras "clean" removes only wm2-swipe; the committed po/ja.mo stays, so
# that msgfmt is optional for people building from source)
clean:
	$(MAKE) -C wm distclean
	$(MAKE) -C bar clean
	$(MAKE) -C wm/extras clean

.PHONY: all install clean
