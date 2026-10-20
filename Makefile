# To The Roots - portable Makefile.
#
# Plain make only: no pattern rules, no conditionals, no functions, no
# automatic variables except $@. Works with GNU make, BSD make and IRIX smake.
#
# Defaults are for Linux with gcc. Override on the command line, e.g.
#
#   make CXX=clang++
#   make B=build-irix CXX=CC WARNFLAGS= DBUS_CFLAGS= \
#        CXXFLAGS="-n32 -O2 -LANG:exceptions=OFF -no_prelink -ptused -woff 3970" \
#        CPPFLAGS="-Isrc -I/usr/Motif-2.1/include -DTTR_HAVE_XPM -DTTR_HAVE_SVG" \
#        LDFLAGS=-L/usr/Motif-2.1/lib32 LIBS="-lXm -lXt -lXpm -lX11 -lm" \
#        TP_SYS=build-irix/tp_sys_irix.o
#                                     (IRIX 6.5, MIPSpro; see README "Building on IRIX")
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

all: $(B)/ttr $(B)/ttrt

$(B)/ttr: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(B)/.dir:
	mkdir -p $(B)
	touch $@

# tl, the terminal emulation library (tl/DESIGN.md): libtl.a and its tests.
# "make tl-test" builds and runs the tests; "make $(B)/tl_test" only
# builds them (on IRIX the binary must be copied off the NFS share to run).
TL_OBJS = $(B)/tl_charset.o \
	$(B)/tl_grid.o \
	$(B)/tl_input.o \
	$(B)/tl_parser.o \
	$(B)/tl_selection.o \
	$(B)/tl_style.o \
	$(B)/tl_terminal.o \
	$(B)/tl_unicode.o \
	$(B)/tl_unicode_data.o \
	$(B)/tl_view.o
TL_TEST_OBJS = $(B)/tl_test_main.o \
	$(B)/tl_test_parser.o \
	$(B)/tl_test_terminal.o \
	$(B)/tl_test_unicode.o \
	$(B)/tl_test_view.o \
	$(B)/tl_test_vterm.o
TL_TERMINAL_H = tl/terminal.h tl/buffer.h tl/charset.h tl/grid.h tl/host.h \
	tl/input.h tl/parser.h tl/style.h tl/unicode.h tl/view.h tl/port.h
AR = ar

$(B)/libtl.a: $(TL_OBJS)
	rm -f $@
	$(AR) rc $@ $(TL_OBJS)

$(B)/tl_test: $(B)/libtl.a $(TL_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(TL_TEST_OBJS) $(B)/libtl.a

tl-test: $(B)/tl_test
	$(B)/tl_test -d tl/test/data -v third_party/libvterm/t

# tp, ptys and child processes (ttrt/DESIGN.md section 4). TP_SYS is the
# per-system implementation: tp_sys_posix.o (Linux, BSD) or tp_sys_irix.o
# (IRIX: make TP_SYS=$(B)/tp_sys_irix.o). "make tp-test" builds and runs
# the tests.
TP_SYS = $(B)/tp_sys_posix.o
TP_OBJS = $(B)/tp_pty.o \
	$(B)/tp_process.o \
	$(TP_SYS)

$(B)/libtp.a: $(TP_OBJS)
	rm -f $@
	$(AR) rc $@ $(TP_OBJS)

$(B)/tp_test: $(B)/libtp.a $(B)/tp_test_tp.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/tp_test_tp.o $(B)/libtp.a

tp-test: $(B)/tp_test
	$(B)/tp_test

$(B)/tp_pty.o: $(B)/.dir tp/pty.cpp tp/tp.h tp/sys.h
	$(COMPILE) -c tp/pty.cpp -o $@
$(B)/tp_process.o: $(B)/.dir tp/process.cpp tp/tp.h tp/sys.h
	$(COMPILE) -c tp/process.cpp -o $@
$(B)/tp_sys_posix.o: $(B)/.dir tp/sys_posix.cpp tp/sys.h
	$(COMPILE) -c tp/sys_posix.cpp -o $@
$(B)/tp_sys_irix.o: $(B)/.dir tp/sys_irix.cpp tp/sys.h
	$(COMPILE) -c tp/sys_irix.cpp -o $@
$(B)/tp_test_tp.o: $(B)/.dir tp/test/test_tp.cpp tp/tp.h
	$(COMPILE) -c tp/test/test_tp.cpp -o $@

# tw, the text window drawing engine and its TwGrid widget (tw/DESIGN.md).
TW_OBJS = $(B)/tw_core_render.o \
	$(B)/tw_fonts.o \
	$(B)/tw_grid_widget.o \
	$(B)/tw_palette.o \
	$(B)/tw_surface.o
TW_H = tw/tw.h tw/fonts.h tw/renderer.h tw/palette.h tw/surface.h

$(B)/libtw.a: $(TW_OBJS)
	rm -f $@
	$(AR) rc $@ $(TW_OBJS)

$(B)/tw_core_render.o: $(B)/.dir tw/core_render.cpp $(TW_H)
	$(COMPILE) -c tw/core_render.cpp -o $@
$(B)/tw_fonts.o: $(B)/.dir tw/fonts.cpp tw/fonts.h tw/tw.h
	$(COMPILE) -c tw/fonts.cpp -o $@
$(B)/tw_grid_widget.o: $(B)/.dir tw/grid_widget.cpp tw/grid_widget.h $(TW_H)
	$(COMPILE) -c tw/grid_widget.cpp -o $@
$(B)/tw_palette.o: $(B)/.dir tw/palette.cpp tw/palette.h tw/tw.h
	$(COMPILE) -c tw/palette.cpp -o $@
$(B)/tw_surface.o: $(B)/.dir tw/surface.cpp $(TW_H)
	$(COMPILE) -c tw/surface.cpp -o $@

# ttrt, the terminal (ttrt/DESIGN.md): tl + tp + tw and a Motif window.
TTRT_OBJS = $(B)/ttrt_main.o \
	$(B)/ttrt_source.o

ttrt: $(B)/ttrt

$(B)/ttrt: $(TTRT_OBJS) $(B)/libtw.a $(B)/libtl.a $(B)/libtp.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(TTRT_OBJS) $(B)/libtw.a $(B)/libtl.a \
		$(B)/libtp.a $(LIBS)

$(B)/ttrt_main.o: $(B)/.dir src/ttrt/main.cpp src/ttrt/source.h tw/grid_widget.h $(TW_H) \
		$(TL_TERMINAL_H) tp/tp.h
	$(COMPILE) -c src/ttrt/main.cpp -o $@
$(B)/ttrt_source.o: $(B)/.dir src/ttrt/source.cpp src/ttrt/source.h tw/tw.h $(TL_TERMINAL_H)
	$(COMPILE) -c src/ttrt/source.cpp -o $@

$(B)/tl_charset.o: $(B)/.dir tl/charset.cpp tl/charset.h tl/port.h
	$(COMPILE) -c tl/charset.cpp -o $@
$(B)/tl_grid.o: $(B)/.dir tl/grid.cpp tl/grid.h tl/port.h
	$(COMPILE) -c tl/grid.cpp -o $@
$(B)/tl_style.o: $(B)/.dir tl/style.cpp tl/style.h tl/port.h
	$(COMPILE) -c tl/style.cpp -o $@
$(B)/tl_terminal.o: $(B)/.dir tl/terminal.cpp $(TL_TERMINAL_H)
	$(COMPILE) -c tl/terminal.cpp -o $@
$(B)/tl_test_terminal.o: $(B)/.dir tl/test/test_terminal.cpp tl/test/tltest.h $(TL_TERMINAL_H)
	$(COMPILE) -c tl/test/test_terminal.cpp -o $@
$(B)/tl_selection.o: $(B)/.dir tl/selection.cpp $(TL_TERMINAL_H)
	$(COMPILE) -c tl/selection.cpp -o $@
$(B)/tl_input.o: $(B)/.dir tl/input.cpp $(TL_TERMINAL_H)
	$(COMPILE) -c tl/input.cpp -o $@
$(B)/tl_view.o: $(B)/.dir tl/view.cpp $(TL_TERMINAL_H)
	$(COMPILE) -c tl/view.cpp -o $@
$(B)/tl_test_view.o: $(B)/.dir tl/test/test_view.cpp tl/test/tltest.h $(TL_TERMINAL_H)
	$(COMPILE) -c tl/test/test_view.cpp -o $@
$(B)/tl_test_vterm.o: $(B)/.dir tl/test/test_vterm.cpp tl/test/tltest.h $(TL_TERMINAL_H)
	$(COMPILE) -c tl/test/test_vterm.cpp -o $@
$(B)/tl_parser.o: $(B)/.dir tl/parser.cpp tl/parser.h tl/port.h
	$(COMPILE) -c tl/parser.cpp -o $@
$(B)/tl_unicode.o: $(B)/.dir tl/unicode.cpp tl/unicode.h tl/port.h
	$(COMPILE) -c tl/unicode.cpp -o $@
$(B)/tl_unicode_data.o: $(B)/.dir tl/unicode_data.cpp tl/unicode.h tl/port.h
	$(COMPILE) -c tl/unicode_data.cpp -o $@
$(B)/tl_test_main.o: $(B)/.dir tl/test/test_main.cpp tl/test/tltest.h tl/port.h
	$(COMPILE) -c tl/test/test_main.cpp -o $@
$(B)/tl_test_parser.o: $(B)/.dir tl/test/test_parser.cpp tl/test/tltest.h tl/parser.h tl/port.h
	$(COMPILE) -c tl/test/test_parser.cpp -o $@
$(B)/tl_test_unicode.o: $(B)/.dir tl/test/test_unicode.cpp tl/test/tltest.h tl/unicode.h tl/port.h
	$(COMPILE) -c tl/test/test_unicode.cpp -o $@

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
# A running ttr cannot be overwritten ("Text file busy"), but it can be
# renamed: the old one becomes ttr.old and keeps running, and Restart ttr
# starts the new one.
install: $(B)/ttr $(B)/ttrt
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(XSESSIONDIR)
	if [ -f $(DESTDIR)$(BINDIR)/ttr ]; then \
		mv -f $(DESTDIR)$(BINDIR)/ttr $(DESTDIR)$(BINDIR)/ttr.old; fi
	cp $(B)/ttr $(DESTDIR)$(BINDIR)/ttr
	chmod 755 $(DESTDIR)$(BINDIR)/ttr
	rm -f $(DESTDIR)$(BINDIR)/ttrt
	cp $(B)/ttrt $(DESTDIR)$(BINDIR)/ttrt
	chmod 755 $(DESTDIR)$(BINDIR)/ttrt
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
	rm -f $(DESTDIR)$(BINDIR)/ttr $(DESTDIR)$(BINDIR)/ttr.old \
		$(DESTDIR)$(BINDIR)/ttrt $(DESTDIR)$(BINDIR)/ttr-session \
		$(DESTDIR)$(XSESSIONDIR)/ttr.desktop
	rm -rf $(DESTDIR)$(DATADIR)

clean:
	rm -rf $(B) build-asan
