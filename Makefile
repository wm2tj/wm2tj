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

all:
	$(MAKE) -C wm
	$(MAKE) -C bar
	$(MAKE) -C wm/extras

install: all
	$(MAKE) -C wm install-session PREFIX='$(PREFIX)'
	$(MAKE) -C bar install PREFIX='$(PREFIX)'
	$(MAKE) -C wm/extras install PREFIX='$(PREFIX)'
	install -D -m644 man/wm2tj.1     $(DESTDIR)$(MANDIR)/man1/wm2tj.1
	install -D -m644 man/wm2tj-bar.1 $(DESTDIR)$(MANDIR)/man1/wm2tj-bar.1

# wm/extras is left alone: its "clean" would delete the committed po/ja.mo,
# which is kept so that msgfmt is optional for people building from source.
clean:
	$(MAKE) -C wm distclean
	$(MAKE) -C bar clean

.PHONY: all install clean
