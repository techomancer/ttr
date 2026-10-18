# To The Roots - portable Makefile.
#
# Plain make only: no pattern rules, no conditionals, no functions, no
# automatic variables except $@. Works with GNU make, BSD make and IRIX smake.
#
# Defaults are for Linux with gcc. Override on the command line, e.g.
#
#   make CXX=clang++
#   make CXX=CC CXXFLAGS="-n32 -O2 -LANG:exceptions=OFF" WARNFLAGS= \
#        CPPFLAGS="-Isrc -DTTR_HAVE_XPM -DTTR_HAVE_SVG" DBUS_CFLAGS= \
#        LIBS="-lXm -lXt -lXpm -lX11 -lm"
#                                                             (IRIX, MIPSpro)
#
# TTR_HAVE_PNG, TTR_HAVE_XPM and TTR_HAVE_SVG enable icon files in those
# formats; drop the define (and library) to build without one. SVG uses the
# nanosvg submodule: git submodule update --init
#
# Every object has an explicit rule. When you add a source file, add its
# object to OBJS and give it a rule listing the headers it includes.

CXX = g++
CXXFLAGS = -std=c++98 -O2 -g -fno-exceptions -fno-rtti
WARNFLAGS = -pedantic -Wall -Wextra
CPPFLAGS = -Isrc -DTTR_HAVE_PNG -DTTR_HAVE_XPM -DTTR_HAVE_SVG -DTTR_HAVE_DBUS
# libdbus-1 for desktop notifications. Its include directories differ per
# distribution: make DBUS_CFLAGS="`pkg-config --cflags dbus-1`" if these
# are wrong, or drop -DTTR_HAVE_DBUS and -ldbus-1 to build without it.
DBUS_CFLAGS = -I/usr/include/dbus-1.0 -I/usr/lib64/dbus-1.0/include \
	-I/usr/lib/dbus-1.0/include -I/usr/lib/x86_64-linux-gnu/dbus-1.0/include
LDFLAGS =
LIBS = -lXm -lXt -lXpm -lpng -ldbus-1 -lX11 -lm

# make install: the program and the session launcher go to BINDIR, the
# session file to XSESSIONDIR, where display managers look for sessions,
# and the color themes to DATADIR/schemes. DESTDIR is prepended to all of
# them, for packaging.
PREFIX = /usr
BINDIR = $(PREFIX)/bin
XSESSIONDIR = $(PREFIX)/share/xsessions
DATADIR = $(PREFIX)/share/ttr
DESTDIR =

B = build
COMPILE = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(WARNFLAGS)

OBJS = $(B)/main.o \
	$(B)/core_apps.o \
	$(B)/core_audio.o \
	$(B)/core_color.o \
	$(B)/core_frame.o \
	$(B)/core_geom.o \
	$(B)/core_icon.o \
	$(B)/core_launch.o \
	$(B)/core_net.o \
	$(B)/core_painter.o \
	$(B)/core_scale.o \
	$(B)/core_settings.o \
	$(B)/core_shortcuts.o \
	$(B)/core_startup.o \
	$(B)/core_storage.o \
	$(B)/core_str.o \
	$(B)/core_switcher.o \
	$(B)/core_text.o \
	$(B)/core_theme.o \
	$(B)/core_xdgautostart.o \
	$(B)/shell_colorpicker.o \
	$(B)/shell_colorspage.o \
	$(B)/shell_netstatus.o \
	$(B)/shell_notifier.o \
	$(B)/shell_notifysrc.o \
	$(B)/shell_pickers.o \
	$(B)/shell_quicklaunch.o \
	$(B)/shell_runcmd.o \
	$(B)/shell_settings.o \
	$(B)/shell_shell.o \
	$(B)/shell_shortcutsdlg.o \
	$(B)/shell_startmenu.o \
	$(B)/shell_startuppage.o \
	$(B)/shell_storagemenu.o \
	$(B)/shell_taskbar.o \
	$(B)/shell_tray.o \
	$(B)/shell_volume.o \
	$(B)/shell_winmenu.o \
	$(B)/x11_config.o \
	$(B)/x11_hints.o \
	$(B)/x11_iconload.o \
	$(B)/x11_keys.o \
	$(B)/x11_svg.o \
	$(B)/x11_wm.o \
	$(B)/x11_xpainter.o

all: $(B)/ttr

$(B)/ttr: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(B)/.dir:
	mkdir -p $(B)
	touch $@

# Header groups, to keep the rules below short.
CORE_H = src/core/port.h src/core/vector.h src/core/str.h src/core/geom.h \
	src/core/color.h src/core/painter.h src/core/frame.h src/core/wmlink.h \
	src/core/text.h src/core/icon.h src/core/apps.h src/core/switcher.h \
	src/core/settings.h src/core/shortcuts.h src/core/launch.h \
	src/core/startup.h src/core/xdgautostart.h src/core/scale.h \
	src/core/theme.h \
	src/core/audio.h src/core/net.h src/core/storage.h src/core/notify.h
X11_H = src/x11/wm.h src/x11/hints.h src/x11/config.h src/x11/xpainter.h \
	src/x11/iconload.h src/x11/svg.h src/x11/keys.h \
	$(CORE_H)
SHELL_H = src/shell/shell.h src/shell/startmenu.h src/shell/taskbar.h \
	src/shell/tray.h src/shell/winmenu.h src/shell/quicklaunch.h \
	src/shell/settings.h src/shell/shortcutsdlg.h src/shell/runcmd.h \
	src/shell/volume.h src/shell/netstatus.h src/shell/trayitem.h \
	src/shell/storagemenu.h src/shell/popup.h src/shell/notifier.h \
	src/shell/notifysrc.h src/shell/pickers.h src/shell/startuppage.h \
	src/shell/colorpicker.h src/shell/colorspage.h

$(B)/main.o: $(B)/.dir src/main.cpp $(X11_H) $(SHELL_H)
	$(COMPILE) -c src/main.cpp -o $@

$(B)/core_apps.o: $(B)/.dir src/core/apps.cpp $(CORE_H)
	$(COMPILE) -c src/core/apps.cpp -o $@

$(B)/core_audio.o: $(B)/.dir src/core/audio.cpp $(CORE_H)
	$(COMPILE) -c src/core/audio.cpp -o $@

$(B)/core_color.o: $(B)/.dir src/core/color.cpp $(CORE_H)
	$(COMPILE) -c src/core/color.cpp -o $@

$(B)/core_frame.o: $(B)/.dir src/core/frame.cpp $(CORE_H)
	$(COMPILE) -c src/core/frame.cpp -o $@

$(B)/core_geom.o: $(B)/.dir src/core/geom.cpp $(CORE_H)
	$(COMPILE) -c src/core/geom.cpp -o $@

$(B)/core_icon.o: $(B)/.dir src/core/icon.cpp $(CORE_H)
	$(COMPILE) -c src/core/icon.cpp -o $@

$(B)/core_launch.o: $(B)/.dir src/core/launch.cpp $(CORE_H)
	$(COMPILE) -c src/core/launch.cpp -o $@

$(B)/core_net.o: $(B)/.dir src/core/net.cpp $(CORE_H)
	$(COMPILE) -c src/core/net.cpp -o $@

$(B)/core_painter.o: $(B)/.dir src/core/painter.cpp $(CORE_H)
	$(COMPILE) -c src/core/painter.cpp -o $@

$(B)/core_scale.o: $(B)/.dir src/core/scale.cpp $(CORE_H)
	$(COMPILE) -c src/core/scale.cpp -o $@

$(B)/core_settings.o: $(B)/.dir src/core/settings.cpp $(CORE_H)
	$(COMPILE) -c src/core/settings.cpp -o $@

$(B)/core_shortcuts.o: $(B)/.dir src/core/shortcuts.cpp $(CORE_H)
	$(COMPILE) -c src/core/shortcuts.cpp -o $@

$(B)/core_startup.o: $(B)/.dir src/core/startup.cpp $(CORE_H)
	$(COMPILE) -c src/core/startup.cpp -o $@

$(B)/core_storage.o: $(B)/.dir src/core/storage.cpp $(CORE_H)
	$(COMPILE) -c src/core/storage.cpp -o $@

$(B)/core_str.o: $(B)/.dir src/core/str.cpp $(CORE_H)
	$(COMPILE) -c src/core/str.cpp -o $@

$(B)/core_switcher.o: $(B)/.dir src/core/switcher.cpp $(CORE_H)
	$(COMPILE) -c src/core/switcher.cpp -o $@

$(B)/core_text.o: $(B)/.dir src/core/text.cpp $(CORE_H)
	$(COMPILE) -c src/core/text.cpp -o $@

# Where make install puts the themes; built-in, so ttr finds them there.
$(B)/core_theme.o: $(B)/.dir src/core/theme.cpp $(CORE_H)
	$(COMPILE) -DTTR_DATADIR=\"$(DATADIR)\" -c src/core/theme.cpp -o $@

$(B)/core_xdgautostart.o: $(B)/.dir src/core/xdgautostart.cpp $(CORE_H)
	$(COMPILE) -c src/core/xdgautostart.cpp -o $@

$(B)/shell_colorpicker.o: $(B)/.dir src/shell/colorpicker.cpp $(SHELL_H) \
		$(CORE_H)
	$(COMPILE) -c src/shell/colorpicker.cpp -o $@

$(B)/shell_colorspage.o: $(B)/.dir src/shell/colorspage.cpp $(SHELL_H) \
		$(X11_H)
	$(COMPILE) -c src/shell/colorspage.cpp -o $@

$(B)/shell_netstatus.o: $(B)/.dir src/shell/netstatus.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/netstatus.cpp -o $@

$(B)/shell_notifier.o: $(B)/.dir src/shell/notifier.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/notifier.cpp -o $@

$(B)/shell_notifysrc.o: $(B)/.dir src/shell/notifysrc.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) $(DBUS_CFLAGS) -c src/shell/notifysrc.cpp -o $@

$(B)/shell_pickers.o: $(B)/.dir src/shell/pickers.cpp $(SHELL_H) $(CORE_H)
	$(COMPILE) -c src/shell/pickers.cpp -o $@

$(B)/shell_quicklaunch.o: $(B)/.dir src/shell/quicklaunch.cpp $(SHELL_H) \
		$(X11_H)
	$(COMPILE) -c src/shell/quicklaunch.cpp -o $@

$(B)/shell_runcmd.o: $(B)/.dir src/shell/runcmd.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/runcmd.cpp -o $@

$(B)/shell_settings.o: $(B)/.dir src/shell/settings.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/settings.cpp -o $@

$(B)/shell_shell.o: $(B)/.dir src/shell/shell.cpp $(SHELL_H) $(CORE_H)
	$(COMPILE) -c src/shell/shell.cpp -o $@

$(B)/shell_shortcutsdlg.o: $(B)/.dir src/shell/shortcutsdlg.cpp $(SHELL_H) \
		$(X11_H)
	$(COMPILE) -c src/shell/shortcutsdlg.cpp -o $@

$(B)/shell_startmenu.o: $(B)/.dir src/shell/startmenu.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/startmenu.cpp -o $@

$(B)/shell_startuppage.o: $(B)/.dir src/shell/startuppage.cpp $(SHELL_H) \
		$(X11_H)
	$(COMPILE) -c src/shell/startuppage.cpp -o $@

$(B)/shell_taskbar.o: $(B)/.dir src/shell/taskbar.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/taskbar.cpp -o $@

$(B)/shell_storagemenu.o: $(B)/.dir src/shell/storagemenu.cpp $(SHELL_H) \
		$(X11_H)
	$(COMPILE) -c src/shell/storagemenu.cpp -o $@

$(B)/shell_tray.o: $(B)/.dir src/shell/tray.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/tray.cpp -o $@

$(B)/shell_volume.o: $(B)/.dir src/shell/volume.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/volume.cpp -o $@

$(B)/shell_winmenu.o: $(B)/.dir src/shell/winmenu.cpp $(SHELL_H) $(CORE_H)
	$(COMPILE) -c src/shell/winmenu.cpp -o $@

$(B)/x11_config.o: $(B)/.dir src/x11/config.cpp $(X11_H)
	$(COMPILE) -c src/x11/config.cpp -o $@

$(B)/x11_hints.o: $(B)/.dir src/x11/hints.cpp $(X11_H)
	$(COMPILE) -c src/x11/hints.cpp -o $@

$(B)/x11_iconload.o: $(B)/.dir src/x11/iconload.cpp $(X11_H)
	$(COMPILE) -c src/x11/iconload.cpp -o $@

# Vendored code: our flags, but not our warnings.
NANOSVG = third_party/nanosvg/src
$(B)/x11_svg.o: $(B)/.dir src/x11/svg.cpp src/x11/svg.h src/core/port.h \
		$(NANOSVG)/nanosvg.h $(NANOSVG)/nanosvgrast.h
	$(CXX) $(CPPFLAGS) -I$(NANOSVG) $(CXXFLAGS) -c src/x11/svg.cpp -o $@

$(B)/x11_keys.o: $(B)/.dir src/x11/keys.cpp $(X11_H)
	$(COMPILE) -c src/x11/keys.cpp -o $@

$(B)/x11_wm.o: $(B)/.dir src/x11/wm.cpp $(X11_H)
	$(COMPILE) -c src/x11/wm.cpp -o $@

$(B)/x11_xpainter.o: $(B)/.dir src/x11/xpainter.cpp $(X11_H)
	$(COMPILE) -c src/x11/xpainter.cpp -o $@

# A separate build with AddressSanitizer (gcc or clang), for chasing memory
# errors: build-asan/ttr. Run with ASAN_OPTIONS=detect_leaks=0.
asan:
	$(MAKE) B=build-asan \
		CXXFLAGS="-std=c++98 -g -O0 -fno-exceptions -fno-rtti -fsanitize=address -fno-omit-frame-pointer" \
		LDFLAGS=-fsanitize=address

# Plain mkdir/cp/chmod: IRIX install(1) takes different arguments.
install: $(B)/ttr
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(XSESSIONDIR)
	cp $(B)/ttr $(DESTDIR)$(BINDIR)/ttr
	chmod 755 $(DESTDIR)$(BINDIR)/ttr
	cp dist/ttr-session $(DESTDIR)$(BINDIR)/ttr-session
	chmod 755 $(DESTDIR)$(BINDIR)/ttr-session
	cp dist/ttr.desktop $(DESTDIR)$(XSESSIONDIR)/ttr.desktop
	chmod 644 $(DESTDIR)$(XSESSIONDIR)/ttr.desktop
	mkdir -p $(DESTDIR)$(DATADIR)/schemes/ttr $(DESTDIR)$(DATADIR)/schemes/irix
	cp data/schemes/ttr/*.ad data/schemes/ttr/README.md \
		$(DESTDIR)$(DATADIR)/schemes/ttr
	cp data/schemes/irix/*.ad data/schemes/irix/README.md \
		$(DESTDIR)$(DATADIR)/schemes/irix
	chmod 644 $(DESTDIR)$(DATADIR)/schemes/ttr/* \
		$(DESTDIR)$(DATADIR)/schemes/irix/*

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ttr $(DESTDIR)$(BINDIR)/ttr-session \
		$(DESTDIR)$(XSESSIONDIR)/ttr.desktop
	rm -rf $(DESTDIR)$(DATADIR)

clean:
	rm -rf $(B) build-asan
