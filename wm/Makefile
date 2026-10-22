# wm2tj - a modified wm2 (X11 window manager)
#   make                 build ./wm2tj
#   make install         -> $(PREFIX)/bin/wm2tj           (PREFIX=/usr/local)
#   make install-session -> also wm2-session and the xsession entry
#   make -C extras install PREFIX=$$HOME/.local   the menu generator and GUIs
# Honours CXX, CXXFLAGS, CPPFLAGS, LDFLAGS, DESTDIR (distro packaging) and
# finds X11/Xft/freetype through pkg-config (no fixed include paths).
PREFIX      ?= /usr/local
BINDIR      ?= $(PREFIX)/bin
XSESSIONDIR ?= /usr/share/xsessions
PKG_CONFIG  ?= pkg-config
PKGS         = x11 xext xft
CXXFLAGS    ?= -O2
CPPFLAGS    += $(shell $(PKG_CONFIG) --cflags $(PKGS))
LIBS         = $(shell $(PKG_CONFIG) --libs $(PKGS)) -lm
# Rotated.C (core-font text rotation from the original wm2) is no longer
# compiled: tabs are drawn with Xft. Rotated.h stays for its copyright text.
OBJECTS = Border.o Buttons.o Client.o Config.o Events.o Main.o Manager.o

.SUFFIXES: .C .o

.C.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $<

wm2tj:	$(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)

install: wm2tj
	install -D -m755 wm2tj $(DESTDIR)$(BINDIR)/wm2tj

install-session: install
	install -D -m755 wm2-session $(DESTDIR)$(BINDIR)/wm2-session
	install -D -m644 wm2tj.desktop $(DESTDIR)$(XSESSIONDIR)/wm2tj.desktop

clean:
	rm -f *.o core

distclean: clean
	rm -f wm2tj *~

.PHONY: install install-session clean distclean

# Manual header deps (no auto-depend; missing these caused struct-offset
# mismatches when Config.h grew a field on 2026-05-27).
$(OBJECTS): Config.h
Border.o Buttons.o Client.o Events.o Main.o Manager.o: Manager.h Client.h Border.h General.h
