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
#        TP_SYS=build-irix/tp_sys_irix.o TF_SYS=build-irix/tf_sys_irix.o \
#        CC=cc CFLAGS=-n32
#                                     (IRIX 6.5, MIPSpro; see README "Building on IRIX")
#
# TTR_HAVE_XPM and TTR_HAVE_SVG enable icon files in those formats; drop
# the define (and library) to build without one. SVG uses the nanosvg
# submodule: git submodule update --init. PNG icons and the viewer's
# images go through ti, whose libraries are TI_CPPFLAGS and TI_LIBS.
#
# Every object has an explicit rule. When you add a source file, add its
# object to OBJS and give it a rule listing the headers it includes.

CXX = g++
CXXFLAGS = -std=c++98 -O2 -g -fno-exceptions -fno-rtti
# For the C sources of third-party libraries (zlib, bzip2).
CC = cc
CFLAGS = -O2 -g
WARNFLAGS = -pedantic -Wall -Wextra
CPPFLAGS = -Isrc -DTTR_HAVE_XPM -DTTR_HAVE_SVG -DTTR_HAVE_DBUS
# libdbus-1 for desktop notifications. Its include directories differ per
# distribution: make DBUS_CFLAGS="`pkg-config --cflags dbus-1`" if these
# are wrong, or drop -DTTR_HAVE_DBUS and -ldbus-1 to build without it.
DBUS_CFLAGS = -I/usr/include/dbus-1.0 -I/usr/lib64/dbus-1.0/include \
	-I/usr/lib/dbus-1.0/include -I/usr/lib/x86_64-linux-gnu/dbus-1.0/include
LDFLAGS =
LIBS = -lXm -lXt -lXpm -ldbus-1 -lX11 -lm
# tw's renderers: the soft one uses MIT-SHM (libXext), the GL one OpenGL
# 1.1 through GLX. Without OpenGL: make GL_CPPFLAGS= GL_LIBS= (the gl
# renderer is then absent and -renderer gl falls back to soft).
GL_CPPFLAGS = -DTTR_HAVE_GL
GL_LIBS = -lGL
TW_LIBS = -lXext $(GL_LIBS)

# make install: the program and the session launcher go to BINDIR, the
# session file to XSESSIONDIR, where display managers look for sessions,
# and the color themes to DATADIR/schemes. DESTDIR is prepended to all of
# them, for packaging.
PREFIX = /usr
BINDIR = $(PREFIX)/bin
XSESSIONDIR = $(PREFIX)/share/xsessions
DATADIR = $(PREFIX)/share/ttr
# ttrc's own files (the tc shell function); ttrc does not need ttr.
TTRC_DATADIR = $(PREFIX)/share/ttrc
# Where tic puts ttrt's compiled terminfo entry. Empty: tic's own default,
# the system directory every user's curses reads (/usr/share/lib/terminfo
# on IRIX, /usr/share/terminfo with ncurses). With DESTDIR it is never the
# system's: TERMINFO_DIR under DESTDIR, or DESTDIR/PREFIX/share/terminfo
# when it is empty (IRIX packages name /usr/share/lib/terminfo).
TERMINFO_DIR =
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

# The shared core's headers; defined before any rule lists them (make
# expands prerequisites as it reads them).
CORE_H = $(TH_H) src/core/port.h src/core/vector.h src/core/str.h src/core/geom.h \
	src/core/color.h src/core/painter.h src/core/frame.h src/core/wmlink.h \
	src/core/text.h src/core/icon.h src/core/apps.h src/core/switcher.h \
	src/core/settings.h src/core/shortcuts.h src/core/launch.h \
	src/core/startup.h src/core/xdgautostart.h src/core/scale.h \
	src/core/theme.h \
	src/core/audio.h src/core/net.h src/core/storage.h src/core/notify.h

all: $(B)/ttr $(B)/ttrt $(B)/ttrc $(B)/ttre $(B)/ttrcode

$(B)/ttr: $(OBJS) $(B)/libth.a $(B)/libti.a $(B)/libtx.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(B)/libth.a $(B)/libti.a $(B)/libtx.a $(TI_LIBS) \
		$(LIBS)

$(B)/.dir:
	mkdir -p $(B)
	touch $@

# tl, the terminal emulation library (tl/DESIGN.md): libtl.a and its tests.
# "make tl-test" builds and runs the tests; "make $(B)/tl_test" only
# builds them (on IRIX the binary must be copied off the NFS share to run).
TL_OBJS = $(B)/tl_charset.o \
	$(B)/tl_grid.o \
	$(B)/tl_image.o \
	$(B)/tl_input.o \
	$(B)/tl_kitty.o \
	$(B)/tl_parser.o \
	$(B)/tl_selection.o \
	$(B)/tl_style.o \
	$(B)/tl_terminal.o \
	$(B)/tl_unicode.o \
	$(B)/tl_unicode_data.o \
	$(B)/tl_view.o
TL_TEST_OBJS = $(B)/tl_test_main.o \
	$(B)/tl_test_image.o \
	$(B)/tl_test_parser.o \
	$(B)/tl_test_terminal.o \
	$(B)/tl_test_unicode.o \
	$(B)/tl_test_view.o \
	$(B)/tl_test_vterm.o
TL_TERMINAL_H = tl/terminal.h tl/buffer.h tl/charset.h tl/grid.h tl/host.h tl/image.h \
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
TW_OBJS = $(B)/tw_backgrounds.o \
	$(B)/tw_bdf.o \
	$(B)/tw_core_render.o \
	$(B)/tw_fonts.o \
	$(B)/tw_gl_render.o \
	$(B)/tw_glyphs.o \
	$(B)/tw_grid_widget.o \
	$(B)/tw_palette.o \
	$(B)/tw_sdf.o \
	$(B)/tw_soft_render.o \
	$(B)/tw_surface.o \
	$(B)/tw_visual.o
TW_H = tw/tw.h tw/bdf.h tw/fonts.h tw/glyphs.h tw/renderer.h tw/palette.h tw/surface.h \
	tw/backgrounds.h tw/sdf.h

$(B)/libtw.a: $(TW_OBJS)
	rm -f $@
	$(AR) rc $@ $(TW_OBJS)

$(B)/tw_bdf.o: $(B)/.dir tw/bdf.cpp tw/bdf.h tw/tw.h
	$(COMPILE) -c tw/bdf.cpp -o $@
$(B)/tw_sdf.o: $(B)/.dir tw/sdf.cpp $(TW_H)
	$(COMPILE) -c tw/sdf.cpp -o $@
$(B)/tw_glyphs.o: $(B)/.dir tw/glyphs.cpp $(TW_H)
	$(COMPILE) -c tw/glyphs.cpp -o $@
$(B)/tw_core_render.o: $(B)/.dir tw/core_render.cpp $(TW_H)
	$(COMPILE) -c tw/core_render.cpp -o $@
$(B)/tw_fonts.o: $(B)/.dir tw/fonts.cpp tw/fonts.h tw/bdf.h tw/tw.h
	$(COMPILE) -c tw/fonts.cpp -o $@
$(B)/tw_grid_widget.o: $(B)/.dir tw/grid_widget.cpp tw/grid_widget.h $(TW_H)
	$(COMPILE) -c tw/grid_widget.cpp -o $@
$(B)/tw_palette.o: $(B)/.dir tw/palette.cpp tw/palette.h tw/tw.h
	$(COMPILE) -c tw/palette.cpp -o $@
$(B)/tw_surface.o: $(B)/.dir tw/surface.cpp $(TW_H)
	$(COMPILE) -c tw/surface.cpp -o $@
$(B)/tw_soft_render.o: $(B)/.dir tw/soft_render.cpp $(TW_H)
	$(COMPILE) -c tw/soft_render.cpp -o $@
$(B)/tw_gl_render.o: $(B)/.dir tw/gl_render.cpp $(TW_H)
	$(COMPILE) $(GL_CPPFLAGS) -c tw/gl_render.cpp -o $@
$(B)/tw_backgrounds.o: $(B)/.dir tw/backgrounds.cpp $(TW_H)
	$(COMPILE) $(GL_CPPFLAGS) -c tw/backgrounds.cpp -o $@
$(B)/tw_visual.o: $(B)/.dir tw/visual.cpp $(TW_H)
	$(COMPILE) -c tw/visual.cpp -o $@

# The Unicode fonts ttrt draws with where the X server has none (IRIX): the
# public domain X.org misc-fixed fonts in their ISO 10646 BDF form.
TW_FONTS = third_party/misc-misc/6x13.bdf third_party/misc-misc/6x13B.bdf \
	third_party/misc-misc/7x13.bdf third_party/misc-misc/7x13B.bdf \
	third_party/misc-misc/7x14.bdf third_party/misc-misc/7x14B.bdf \
	third_party/misc-misc/8x13.bdf third_party/misc-misc/8x13B.bdf \
	third_party/misc-misc/9x15.bdf third_party/misc-misc/9x15B.bdf \
	third_party/misc-misc/9x18.bdf third_party/misc-misc/9x18B.bdf \
	third_party/misc-misc/10x20.bdf third_party/misc-misc/6x12.bdf \
	third_party/misc-misc/6x10.bdf third_party/misc-misc/6x9.bdf \
	third_party/misc-misc/5x8.bdf third_party/misc-misc/5x7.bdf

# Print the font list (for packaging scripts).
tw-fonts:
	@echo $(TW_FONTS)

# tw tests that need no display: "make tw-test".
TW_TEST_OBJS = $(B)/tw_bdf.o $(B)/tw_fonts.o $(B)/tw_glyphs.o $(B)/tw_test_tw.o

$(B)/tw_test: $(TW_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(TW_TEST_OBJS) -lX11

tw-test: $(B)/tw_test
	$(B)/tw_test third_party/misc-misc

$(B)/tw_test_tw.o: $(B)/.dir tw/test/test_tw.cpp $(TW_H)
	$(COMPILE) -c tw/test/test_tw.cpp -o $@

# tw_bench: a renderer's frames per second on a synthetic screen (needs a
# display; tw/test/bench_tw.cpp says how to run it).
$(B)/tw_bench: $(B)/tw_bench_tw.o $(B)/libtw.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/tw_bench_tw.o $(B)/libtw.a $(TW_LIBS) -lX11 -lm

$(B)/tw_bench_tw.o: $(B)/.dir tw/test/bench_tw.cpp $(TW_H)
	$(COMPILE) $(GL_CPPFLAGS) -c tw/test/bench_tw.cpp -o $@

# sdf_lab: the same text drawn 1-bit, box-filtered and from distance
# fields, for comparing (tw/DESIGN.md section 4.6; needs a display).
$(B)/sdf_lab: $(B)/.dir tw/test/sdf_lab.cpp
	$(CXX) $(CXXFLAGS) $(WARNFLAGS) -o $@ tw/test/sdf_lab.cpp $(LDFLAGS) -lX11 -lm

# tu, the terminal UI library (ttrc/DESIGN.md section 3): libtu.a and its
# tests. It reads terminfo itself; no curses. "make tu-test" runs them.
TU_OBJS = $(B)/tu_capnames.o \
	$(B)/tu_input.o \
	$(B)/tu_widgets.o \
	$(B)/tu_screen.o \
	$(B)/tu_term.o \
	$(B)/tu_terminfo.o \
	$(B)/tu_theme.o \
	$(B)/tu_tparm.o
TU_H = tu/tu.h tu/capnames.h tu/input.h tu/screen.h tu/term.h tu/terminfo.h \
	tu/tparm.h tu/widgets.h tu/theme.h tl/port.h

$(B)/libtu.a: $(TU_OBJS)
	rm -f $@
	$(AR) rc $@ $(TU_OBJS)

$(B)/tu_capnames.o: $(B)/.dir tu/capnames.cpp tu/capnames.h
	$(COMPILE) -c tu/capnames.cpp -o $@
$(B)/tu_widgets.o: $(B)/.dir tu/widgets.cpp $(TU_H)
	$(COMPILE) -c tu/widgets.cpp -o $@
$(B)/tu_input.o: $(B)/.dir tu/input.cpp $(TU_H)
	$(COMPILE) -c tu/input.cpp -o $@
$(B)/tu_screen.o: $(B)/.dir tu/screen.cpp $(TU_H) tl/unicode.h
	$(COMPILE) -c tu/screen.cpp -o $@
$(B)/tu_term.o: $(B)/.dir tu/term.cpp $(TU_H)
	$(COMPILE) -c tu/term.cpp -o $@
$(B)/tu_terminfo.o: $(B)/.dir tu/terminfo.cpp $(TU_H)
	$(COMPILE) -c tu/terminfo.cpp -o $@
$(B)/tu_theme.o: $(B)/.dir tu/theme.cpp $(TU_H)
	$(COMPILE) -c tu/theme.cpp -o $@
$(B)/tu_tparm.o: $(B)/.dir tu/tparm.cpp tu/tparm.h
	$(COMPILE) -c tu/tparm.cpp -o $@

$(B)/tu_test: $(B)/tu_test_tu.o $(B)/libtu.a $(B)/libtl.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/tu_test_tu.o $(B)/libtu.a $(B)/libtl.a

tu-test: $(B)/tu_test
	$(B)/tu_test

$(B)/tu_test_tu.o: $(B)/.dir tu/test/test_tu.cpp $(TU_H) $(TL_TERMINAL_H)
	$(COMPILE) -c tu/test/test_tu.cpp -o $@

# te, the text editing core shared by ttrc's editor and ttrcode
# (te/DESIGN.md): libte.a and its tests. "make te-test" runs them.
TE_OBJS = $(B)/te_document.o \
	$(B)/te_file.o \
	$(B)/te_syntax.o \
	$(B)/te_view.o
TE_H = te/document.h te/file.h te/syntax.h te/view.h tl/port.h

$(B)/libte.a: $(TE_OBJS)
	rm -f $@
	$(AR) rc $@ $(TE_OBJS)

$(B)/te_document.o: $(B)/.dir te/document.cpp $(TE_H)
	$(COMPILE) -c te/document.cpp -o $@
$(B)/te_file.o: $(B)/.dir te/file.cpp $(TE_H)
	$(COMPILE) -c te/file.cpp -o $@
$(B)/te_syntax.o: $(B)/.dir te/syntax.cpp $(TE_H)
	$(COMPILE) -c te/syntax.cpp -o $@
$(B)/te_view.o: $(B)/.dir te/view.cpp $(TE_H) tl/unicode.h
	$(COMPILE) -c te/view.cpp -o $@

$(B)/te_test: $(B)/te_test_te.o $(B)/libte.a $(B)/libtl.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/te_test_te.o $(B)/libte.a $(B)/libtl.a

te-test: $(B)/te_test
	$(B)/te_test

$(B)/te_test_te.o: $(B)/.dir te/test/test_te.cpp $(TE_H)
	$(COMPILE) -c te/test/test_te.cpp -o $@

# tx, Motif pieces every ttr program shares (tx/DESIGN.md): the font
# chooser.
TX_OBJS = $(B)/tx_fontchooser.o
TX_H = tx/fontchooser.h tx/dialog.h src/core/str.h src/core/vector.h

$(B)/libtx.a: $(TX_OBJS)
	rm -f $@
	$(AR) rc $@ $(TX_OBJS)

$(B)/tx_fontchooser.o: $(B)/.dir tx/fontchooser.cpp $(TX_H)
	$(COMPILE) -c tx/fontchooser.cpp -o $@

# tx_demo shows the font chooser (needs a display).
$(B)/tx_demo: $(B)/tx_demo.o $(B)/libtx.a $(B)/core_str.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/tx_demo.o $(B)/libtx.a $(B)/core_str.o $(LIBS)

$(B)/tx_demo.o: $(B)/.dir tx/test/demo.cpp $(TX_H)
	$(COMPILE) -c tx/test/demo.cpp -o $@

# th, ttr's colours for every ttr program (th/DESIGN.md): UI themes, text
# colour schemes, and their Motif glue (th_motif.o). "make th-test" runs
# its tests.
TH_OBJS = $(B)/th_th.o \
	$(B)/th_theme.o \
	$(B)/th_scheme.o \
	$(B)/th_motif.o
TH_H = th/th.h th/theme.h th/scheme.h th/motif.h th/internal.h

$(B)/libth.a: $(TH_OBJS)
	rm -f $@
	$(AR) rc $@ $(TH_OBJS)

$(B)/th_th.o: $(B)/.dir th/th.cpp $(TH_H)
	$(COMPILE) -DTTR_DATADIR=\"$(DATADIR)\" -c th/th.cpp -o $@
$(B)/th_theme.o: $(B)/.dir th/theme.cpp $(TH_H)
	$(COMPILE) -c th/theme.cpp -o $@
$(B)/th_scheme.o: $(B)/.dir th/scheme.cpp $(TH_H)
	$(COMPILE) -c th/scheme.cpp -o $@
$(B)/th_motif.o: $(B)/.dir th/motif.cpp $(TH_H)
	$(COMPILE) -c th/motif.cpp -o $@

$(B)/th_test: $(B)/th_test_th.o $(B)/libth.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/th_test_th.o $(B)/libth.a

th-test: $(B)/th_test
	$(B)/th_test

$(B)/th_test_th.o: $(B)/.dir th/test/test_th.cpp $(TH_H)
	$(COMPILE) -c th/test/test_th.cpp -o $@

# ti, ttr's images (ti/DESIGN.md): decoders and terminal encodings.
# TI_CPPFLAGS names the image libraries the build has and TI_LIBS links
# them; a format without its library uses the built-in decoder
# (stb_image, third_party/stb), and TIFF has none. STB_CFLAGS: extra C
# flags for stb_image (-fwrapv: its IDCT overflows on damaged files;
# IRIX: STB_CFLAGS=-c99). "make ti-test" runs its tests.
TI_CPPFLAGS = -DTTR_HAVE_PNG -DTTR_HAVE_JPEG -DTTR_HAVE_GIF -DTTR_HAVE_TIFF
TI_LIBS = -lpng -ljpeg -lgif -ltiff
STB_CFLAGS = -fwrapv
TI_OBJS = $(B)/ti_image.o \
	$(B)/ti_png.o \
	$(B)/ti_jpeg.o \
	$(B)/ti_gif.o \
	$(B)/ti_tiff.o \
	$(B)/ti_sgi.o \
	$(B)/ti_builtin.o \
	$(B)/ti_stb_image.o \
	$(B)/ti_term.o
TI_H = ti/ti.h ti/term.h ti/internal.h

$(B)/libti.a: $(TI_OBJS)
	rm -f $@
	$(AR) rc $@ $(TI_OBJS)

$(B)/ti_image.o: $(B)/.dir ti/image.cpp $(TI_H)
	$(COMPILE) $(TI_CPPFLAGS) -c ti/image.cpp -o $@
$(B)/ti_png.o: $(B)/.dir ti/png.cpp $(TI_H)
	$(COMPILE) $(TI_CPPFLAGS) -c ti/png.cpp -o $@
$(B)/ti_jpeg.o: $(B)/.dir ti/jpeg.cpp $(TI_H)
	$(COMPILE) $(TI_CPPFLAGS) -c ti/jpeg.cpp -o $@
$(B)/ti_gif.o: $(B)/.dir ti/gif.cpp $(TI_H)
	$(COMPILE) $(TI_CPPFLAGS) -c ti/gif.cpp -o $@
$(B)/ti_tiff.o: $(B)/.dir ti/tiff.cpp $(TI_H)
	$(COMPILE) $(TI_CPPFLAGS) -c ti/tiff.cpp -o $@
$(B)/ti_sgi.o: $(B)/.dir ti/sgi.cpp $(TI_H)
	$(COMPILE) -c ti/sgi.cpp -o $@
$(B)/ti_builtin.o: $(B)/.dir ti/builtin.cpp $(TI_H)
	$(COMPILE) -c ti/builtin.cpp -o $@
$(B)/ti_stb_image.o: $(B)/.dir ti/third/stb_image.c third_party/stb/stb_image.h
	$(CC) $(CFLAGS) $(STB_CFLAGS) -c ti/third/stb_image.c -o $@
$(B)/ti_term.o: $(B)/.dir ti/term.cpp $(TI_H)
	$(COMPILE) -c ti/term.cpp -o $@

$(B)/ti_test: $(B)/ti_test_ti.o $(B)/libti.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/ti_test_ti.o $(B)/libti.a $(TI_LIBS) -lm

ti-test: $(B)/ti_test
	$(B)/ti_test

$(B)/ti_test_ti.o: $(B)/.dir ti/test/test_ti.cpp $(TI_H)
	$(COMPILE) -c ti/test/test_ti.cpp -o $@

# tf, ttrc's file systems (ttrc/DESIGN.md section 5): libtf.a and its
# tests. TF_SYS is the per-system part: tf_sys_linux.o or tf_sys_irix.o
# (IRIX: make TF_SYS=$(B)/tf_sys_irix.o).
TF_SYS = $(B)/tf_sys_linux.o
TF_OBJS = $(B)/tf_archive.o \
	$(B)/tf_local.o \
	$(B)/tf_ops.o \
	$(B)/tf_path.o \
	$(B)/tf_vfs.o \
	$(B)/tf_zlib.o \
	$(B)/tf_zlib_inflate.o \
	$(B)/tf_zlib_inffast.o \
	$(B)/tf_zlib_inftrees.o \
	$(B)/tf_bzip2.o \
	$(TF_SYS)
TF_H = tf/tf.h tf/archive.h tf/ops.h tf/path.h tf/sys.h tf/vfs.h tl/port.h

$(B)/libtf.a: $(TF_OBJS)
	rm -f $@
	$(AR) rc $@ $(TF_OBJS)

$(B)/tf_local.o: $(B)/.dir tf/local.cpp $(TF_H)
	$(COMPILE) -c tf/local.cpp -o $@
$(B)/tf_archive.o: $(B)/.dir tf/archive.cpp $(TF_H)
	$(COMPILE) -c tf/archive.cpp -o $@
$(B)/tf_vfs.o: $(B)/.dir tf/vfs.cpp $(TF_H)
	$(COMPILE) -c tf/vfs.cpp -o $@
# zlib and bzip2 (third_party, pinned) through their wrappers in tf/third.
$(B)/tf_zlib.o: $(B)/.dir tf/third/zlib.c
	$(CC) $(CFLAGS) -c tf/third/zlib.c -o $@
$(B)/tf_zlib_inflate.o: $(B)/.dir tf/third/zlib_inflate.c
	$(CC) $(CFLAGS) -c tf/third/zlib_inflate.c -o $@
$(B)/tf_zlib_inffast.o: $(B)/.dir tf/third/zlib_inffast.c
	$(CC) $(CFLAGS) -c tf/third/zlib_inffast.c -o $@
$(B)/tf_zlib_inftrees.o: $(B)/.dir tf/third/zlib_inftrees.c
	$(CC) $(CFLAGS) -c tf/third/zlib_inftrees.c -o $@
$(B)/tf_bzip2.o: $(B)/.dir tf/third/bzip2.c
	$(CC) $(CFLAGS) -c tf/third/bzip2.c -o $@
$(B)/tf_ops.o: $(B)/.dir tf/ops.cpp $(TF_H)
	$(COMPILE) -c tf/ops.cpp -o $@
$(B)/tf_path.o: $(B)/.dir tf/path.cpp $(TF_H)
	$(COMPILE) -c tf/path.cpp -o $@
$(B)/tf_sys_linux.o: $(B)/.dir tf/sys_linux.cpp $(TF_H)
	$(COMPILE) -c tf/sys_linux.cpp -o $@
$(B)/tf_sys_irix.o: $(B)/.dir tf/sys_irix.cpp $(TF_H)
	$(COMPILE) -c tf/sys_irix.cpp -o $@

$(B)/tf_test: $(B)/tf_test_tf.o $(B)/libtf.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/tf_test_tf.o $(B)/libtf.a

tf-test: $(B)/tf_test
	$(B)/tf_test tf/test/data

$(B)/tf_test_tf.o: $(B)/.dir tf/test/test_tf.cpp $(TF_H)
	$(COMPILE) -c tf/test/test_tf.cpp -o $@

# td, the debugger library (td/DESIGN.md), and ttrdb, its command line.
# DWARF through libdwarf (third_party/libdwarf, pinned, LGPL 2.1), built
# with td/third/libdwarf/config.h. Its sources cannot share one wrapper
# (their headers have no include guards), so the rule compiles each in a
# loop; dwarf_line.c and dwarf_print_lines.c go through wrappers in
# td/third. LIBDWARF_CFLAGS: -c99 for MIPSpro.
# TD_SYS: td_target_linux.o (ptrace) or td_target_irix.o (/proc).
LIBDWARF_CFLAGS =
# For td/module.cpp, which includes libdwarf's C header: its 64-bit types
# are long long, which g++ -pedantic warns about in C++98. Empty for
# MIPSpro.
TD_DWARF_FLAGS = -Wno-long-long
LIBDWARF_DIR = third_party/libdwarf/src/lib/libdwarf
LIBDWARF_SRC = \
	third_party/libdwarf/src/lib/libdwarf/dwarf_64machoread.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_abbrev.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_alloc.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_arange.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_crc32.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_crc.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_debugaddr.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_debuglink.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_debugnames.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_debug_sup.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_die_deliv.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_dsc.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_elf_load_headers.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_elfread.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_elf_rel_detector.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_error.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_fill_in_attr_form.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_find_sigref.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_fission_to_cu.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_form.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_form_class_names.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_frame2.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_frame.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_frame_cfa_read.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_frame_iter.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_gdbindex.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_generic_init.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_global.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_gnu_index.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_groups.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_harmless.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_init_finish.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_leb.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_lname_version.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_local_malloc.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_locationop_read.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_loc.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_loclists.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_lvn_name.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_machoread.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_macro5.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_macro.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_memcpy_swap.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_names.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_object_detector.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_object_read_common.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_peread.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_query.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_ranges.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_rnglists.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_safe_arithmetic.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_safe_strcpy.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_secname_ck.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_seekr.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_setup_sections.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_string.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_stringsection.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_str_offsets.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_tied.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_tsearchhash.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_util.c \
	third_party/libdwarf/src/lib/libdwarf/dwarf_xu_index.c \
	td/third/libdwarf_line.c td/third/libdwarf_print_lines.c

$(B)/libdwarf.a: $(B)/.dir td/third/libdwarf/config.h
	rm -rf $(B)/libdwarf && mkdir -p $(B)/libdwarf
	top=`pwd`; for f in $(LIBDWARF_SRC); do \
		o=$(B)/libdwarf/`basename $$f .c`.o; \
		echo "  CC $$f"; \
		$(CC) $(CFLAGS) $(LIBDWARF_CFLAGS) -I$$top/td/third/libdwarf \
			-I$$top/$(LIBDWARF_DIR) -c $$top/$$f -o $$o || exit 1; \
	done
	rm -f $@
	$(AR) rc $@ $(B)/libdwarf/*.o

TD_SYS = $(B)/td_target_linux.o
TD_OBJS = $(B)/td_arch.o $(B)/td_elf.o $(B)/td_expr.o $(B)/td_info.o $(B)/td_module.o \
	$(B)/td_session.o $(B)/td_source.o $(B)/td_text.o $(B)/td_value.o $(TD_SYS)
TD_H = td/td.h td/target.h td/elf.h td/info.h td/module.h td/session.h td/source.h \
	td/value.h src/core/str.h src/core/vector.h src/core/port.h

$(B)/libtd.a: $(TD_OBJS)
	rm -f $@
	$(AR) rc $@ $(TD_OBJS)

$(B)/td_arch.o: $(B)/.dir td/arch.cpp $(TD_H)
	$(COMPILE) -c td/arch.cpp -o $@
$(B)/td_elf.o: $(B)/.dir td/elf.cpp $(TD_H)
	$(COMPILE) -c td/elf.cpp -o $@
$(B)/td_module.o: $(B)/.dir td/module.cpp $(TD_H) $(B)/libdwarf.a
	$(COMPILE) $(TD_DWARF_FLAGS) -I$(LIBDWARF_DIR) -c td/module.cpp -o $@
$(B)/td_info.o: $(B)/.dir td/info.cpp $(TD_H) $(B)/libdwarf.a
	$(COMPILE) $(TD_DWARF_FLAGS) -I$(LIBDWARF_DIR) -c td/info.cpp -o $@
$(B)/td_value.o: $(B)/.dir td/value.cpp $(TD_H) $(B)/libdwarf.a
	$(COMPILE) $(TD_DWARF_FLAGS) -I$(LIBDWARF_DIR) -c td/value.cpp -o $@
$(B)/td_expr.o: $(B)/.dir td/expr.cpp $(TD_H)
	$(COMPILE) -c td/expr.cpp -o $@
$(B)/td_text.o: $(B)/.dir td/text.cpp $(TD_H)
	$(COMPILE) -c td/text.cpp -o $@
$(B)/td_session.o: $(B)/.dir td/session.cpp $(TD_H)
	$(COMPILE) -c td/session.cpp -o $@
$(B)/td_source.o: $(B)/.dir td/source.cpp $(TD_H)
	$(COMPILE) -c td/source.cpp -o $@
$(B)/td_target_linux.o: $(B)/.dir td/target_linux.cpp $(TD_H)
	$(COMPILE) -c td/target_linux.cpp -o $@
$(B)/td_target_irix.o: $(B)/.dir td/target_irix.cpp $(TD_H)
	$(COMPILE) -c td/target_irix.cpp -o $@

ttrdb: $(B)/ttrdb
$(B)/ttrdb: $(B)/ttrdb_main.o $(B)/libtd.a $(B)/libdwarf.a $(B)/core_str.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/ttrdb_main.o $(B)/libtd.a $(B)/libdwarf.a \
		$(B)/core_str.o
$(B)/ttrdb_main.o: $(B)/.dir src/ttrdb/main.cpp $(TD_H)
	$(COMPILE) -c src/ttrdb/main.cpp -o $@

# The programs the tests debug, built with debug information and no
# optimisation (TD_TEST_CFLAGS), and the scripts in td/test.
TD_TEST_CFLAGS = -g -O0
# The test shared library: position-independent code (MIPSpro's is
# already) and its soname (IRIX: TD_TEST_SONAME="-soname libtdshlib.so").
TD_TEST_PIC = -fPIC
TD_TEST_SONAME = -Wl,-soname,libtdshlib.so
# dlopen's library (IRIX: in libc, TD_TEST_DLLIBS=).
TD_TEST_DLLIBS = -ldl
$(B)/td_demo: $(B)/td_demo.o $(B)/td_demo_util.o
	$(CC) $(TD_TEST_CFLAGS) -o $@ $(B)/td_demo.o $(B)/td_demo_util.o
$(B)/td_demo.o: $(B)/.dir td/test/progs/demo.c
	$(CC) $(TD_TEST_CFLAGS) -c td/test/progs/demo.c -o $@
$(B)/td_demo_util.o: $(B)/.dir td/test/progs/demo_util.c
	$(CC) $(TD_TEST_CFLAGS) -c td/test/progs/demo_util.c -o $@
$(B)/td_crash: $(B)/td_crash.o
	$(CC) $(TD_TEST_CFLAGS) -o $@ $(B)/td_crash.o
$(B)/td_crash.o: $(B)/.dir td/test/progs/crash.c
	$(CC) $(TD_TEST_CFLAGS) -c td/test/progs/crash.c -o $@

$(B)/td_values: $(B)/td_values.o
	$(CC) $(TD_TEST_CFLAGS) -o $@ $(B)/td_values.o
$(B)/td_values.o: $(B)/.dir td/test/progs/values.c
	$(CC) $(TD_TEST_CFLAGS) -c td/test/progs/values.c -o $@

$(B)/libtdshlib.so: $(B)/td_shlib.o
	$(CC) $(TD_TEST_CFLAGS) -shared $(TD_TEST_SONAME) -o $@ $(B)/td_shlib.o
$(B)/td_shlib.o: $(B)/.dir td/test/progs/shlib.c
	$(CC) $(TD_TEST_CFLAGS) $(TD_TEST_PIC) -c td/test/progs/shlib.c -o $@
$(B)/td_shmain: $(B)/td_shmain.o $(B)/libtdshlib.so
	$(CC) $(TD_TEST_CFLAGS) -o $@ $(B)/td_shmain.o -L$(B) -ltdshlib
$(B)/td_shmain.o: $(B)/.dir td/test/progs/shmain.c
	$(CC) $(TD_TEST_CFLAGS) -c td/test/progs/shmain.c -o $@

$(B)/td_dlmain: $(B)/td_dlmain.o $(B)/libtdshlib.so
	$(CC) $(TD_TEST_CFLAGS) -o $@ $(B)/td_dlmain.o $(TD_TEST_DLLIBS)
$(B)/td_dlmain.o: $(B)/.dir td/test/progs/dlmain.c
	$(CC) $(TD_TEST_CFLAGS) -c td/test/progs/dlmain.c -o $@

td-test: $(B)/ttrdb $(B)/td_demo $(B)/td_crash $(B)/td_shmain $(B)/td_dlmain $(B)/td_values
	sh td/test/run.sh $(B)

# ttrc, To The Roots Commander (ttrc/DESIGN.md): tu + tf (+ tl) on a
# character terminal; no X.
TTRC_OBJS = $(B)/ttrc_main.o \
	$(B)/ttrc_app.o \
	$(B)/ttrc_commands.o \
	$(B)/ttrc_config.o \
	$(B)/ttrc_panel.o \
	$(B)/ttrc_shell.o \
	$(B)/ttrc_viewer.o \
	$(B)/core_settings.o \
	$(B)/core_str.o
TTRC_H = src/ttrc/app.h src/ttrc/cmdline.h src/ttrc/config.h src/ttrc/panel.h src/ttrc/shell.h \
	src/ttrc/theme.h src/ttrc/viewer.h $(TU_H) $(TF_H) $(TL_TERMINAL_H) tp/tp.h \
	src/core/str.h src/core/vector.h ti/ti.h ti/term.h

ttrc: $(B)/ttrc

$(B)/ttrc: $(TTRC_OBJS) $(B)/libtu.a $(B)/libtf.a $(B)/libtl.a $(B)/libtp.a $(B)/libti.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(TTRC_OBJS) $(B)/libtu.a $(B)/libtf.a \
		$(B)/libtl.a $(B)/libtp.a $(B)/libti.a $(TI_LIBS)

$(B)/ttrc_main.o: $(B)/.dir src/ttrc/main.cpp $(TTRC_H)
	$(COMPILE) -c src/ttrc/main.cpp -o $@
# ttrc-drive runs ttrc on a pty and checks its screen through tl; the
# scripts in src/ttrc/test/ use it. "make ttrc-test" runs them.
$(B)/ttrc_drive: $(B)/ttrc_test_drive.o $(B)/libtl.a $(B)/libtp.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(B)/ttrc_test_drive.o $(B)/libtl.a $(B)/libtp.a
$(B)/ttrc_test_drive.o: $(B)/.dir src/ttrc/test/drive.cpp $(TL_TERMINAL_H) tp/tp.h
	$(COMPILE) -c src/ttrc/test/drive.cpp -o $@

ttrc-test: $(B)/ttrc $(B)/ttre $(B)/ttrc_drive
	sh src/ttrc/test/run.sh $(B)

# ttre, the editor (ttre/DESIGN.md): te + tu, standalone; ttrc's F4 runs
# it. "make ttre-test" drives it on a pty with ttrc_drive.
TTRE_OBJS = $(B)/ttre_main.o \
	$(B)/ttre_editor.o \
	$(B)/ttre_settings.o \
	$(B)/core_settings.o \
	$(B)/core_str.o
TTRE_H = src/ttre/editor.h src/ttre/settings.h $(TU_H) $(TE_H) $(TH_H) src/core/str.h \
	src/core/settings.h

ttre: $(B)/ttre

$(B)/ttre: $(TTRE_OBJS) $(B)/libtu.a $(B)/libte.a $(B)/libth.a $(B)/libtl.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(TTRE_OBJS) $(B)/libtu.a $(B)/libte.a \
		$(B)/libth.a $(B)/libtl.a

$(B)/ttre_main.o: $(B)/.dir src/ttre/main.cpp $(TTRE_H)
	$(COMPILE) -c src/ttre/main.cpp -o $@
$(B)/ttre_editor.o: $(B)/.dir src/ttre/editor.cpp $(TTRE_H)
	$(COMPILE) -c src/ttre/editor.cpp -o $@
$(B)/ttre_settings.o: $(B)/.dir src/ttre/settings.cpp $(TTRE_H)
	$(COMPILE) -c src/ttre/settings.cpp -o $@

ttre-test: $(B)/ttre $(B)/ttrc_drive
	sh src/ttre/test/run.sh $(B)

$(B)/ttrc_app.o: $(B)/.dir src/ttrc/app.cpp $(TTRC_H)
	$(COMPILE) -c src/ttrc/app.cpp -o $@
$(B)/ttrc_commands.o: $(B)/.dir src/ttrc/commands.cpp $(TTRC_H)
	$(COMPILE) -c src/ttrc/commands.cpp -o $@
$(B)/ttrc_config.o: $(B)/.dir src/ttrc/config.cpp $(TTRC_H) src/core/settings.h
	$(COMPILE) -c src/ttrc/config.cpp -o $@
$(B)/ttrc_shell.o: $(B)/.dir src/ttrc/shell.cpp $(TTRC_H)
	$(COMPILE) -c src/ttrc/shell.cpp -o $@
$(B)/ttrc_panel.o: $(B)/.dir src/ttrc/panel.cpp $(TTRC_H)
	$(COMPILE) -c src/ttrc/panel.cpp -o $@
$(B)/ttrc_viewer.o: $(B)/.dir src/ttrc/viewer.cpp $(TTRC_H)
	$(COMPILE) -c src/ttrc/viewer.cpp -o $@

# tt, the terminal pane (tt/DESIGN.md): a tl::Terminal in a TwGrid with
# its scroll bar, running a program on a tp::Pty. ttrt and ttrcode use it.
TT_OBJS = $(B)/tt_pane.o \
	$(B)/tt_source.o
TT_H = tt/pane.h tt/source.h tw/grid_widget.h $(TW_H) $(TL_TERMINAL_H) tp/tp.h ti/ti.h

$(B)/libtt.a: $(TT_OBJS)
	rm -f $@
	$(AR) rc $@ $(TT_OBJS)

$(B)/tt_pane.o: $(B)/.dir tt/pane.cpp $(TT_H)
	$(COMPILE) -c tt/pane.cpp -o $@
$(B)/tt_source.o: $(B)/.dir tt/source.cpp tt/source.h tw/tw.h $(TL_TERMINAL_H)
	$(COMPILE) -c tt/source.cpp -o $@

# ttrt, the terminal (ttrt/DESIGN.md): one tt pane in a Motif window.
TTRT_OBJS = $(B)/ttrt_main.o \
	$(B)/ttrt_settingsdlg.o \
	$(B)/core_color.o \
	$(B)/core_scale.o \
	$(B)/core_settings.o \
	$(B)/core_str.o \
	$(B)/core_theme.o \
	$(B)/shell_colorpicker.o
TTRT_H = src/ttrt/settings.h src/ttrt/settingsdlg.h $(TT_H) $(TX_H)

ttrt: $(B)/ttrt

$(B)/ttrt: $(TTRT_OBJS) $(B)/libtt.a $(B)/libtw.a $(B)/libtl.a $(B)/libtp.a $(B)/libth.a \
		$(B)/libti.a $(B)/libtx.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(TTRT_OBJS) $(B)/libtt.a $(B)/libtw.a \
		$(B)/libtl.a $(B)/libtp.a $(B)/libth.a $(B)/libti.a $(B)/libtx.a $(TI_LIBS) $(TW_LIBS) $(LIBS)

$(B)/ttrt_main.o: $(B)/.dir src/ttrt/main.cpp $(TTRT_H) $(CORE_H)
	$(COMPILE) -DTTR_DATADIR=\"$(DATADIR)\" -c src/ttrt/main.cpp -o $@
$(B)/ttrt_settingsdlg.o: $(B)/.dir src/ttrt/settingsdlg.cpp $(TTRT_H) \
		src/shell/colorpicker.h $(CORE_H)
	$(COMPILE) -c src/ttrt/settingsdlg.cpp -o $@

# ttrcode, the code editor (ttrcode/DESIGN.md): te + tw + tt in Motif.
TTRCODE_OBJS = $(B)/ttrcode_main.o \
	$(B)/ttrcode_app.o \
	$(B)/ttrcode_celllist.o \
	$(B)/ttrcode_debug.o \
	$(B)/ttrcode_debugpanes.o \
	$(B)/ttrcode_dock.o \
	$(B)/ttrcode_editsource.o \
	$(B)/ttrcode_findfiles.o \
	$(B)/ttrcode_options.o \
	$(B)/ttrcode_valuetree.o \
	$(B)/core_settings.o \
	$(B)/core_str.o
TTRCODE_H = src/ttrcode/app.h src/ttrcode/celllist.h src/ttrcode/dock.h \
	src/ttrcode/editsource.h src/ttrcode/valuetree.h $(TE_H) $(TT_H) $(TW_H) \
	$(TH_H) $(TX_H) $(TD_H) \
	src/core/settings.h src/core/str.h src/core/vector.h

ttrcode: $(B)/ttrcode

$(B)/ttrcode: $(TTRCODE_OBJS) $(B)/libtt.a $(B)/libte.a $(B)/libtw.a $(B)/libtl.a \
		$(B)/libtp.a $(B)/libth.a $(B)/libti.a $(B)/libtx.a $(B)/libtd.a $(B)/libdwarf.a
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(TTRCODE_OBJS) $(B)/libtt.a $(B)/libte.a \
		$(B)/libtw.a $(B)/libtl.a $(B)/libtp.a $(B)/libth.a $(B)/libti.a $(B)/libtx.a \
		$(B)/libtd.a $(B)/libdwarf.a $(TI_LIBS) $(TW_LIBS) $(LIBS)

$(B)/ttrcode_main.o: $(B)/.dir src/ttrcode/main.cpp $(TTRCODE_H)
	$(COMPILE) -DTTR_DATADIR=\"$(DATADIR)\" -c src/ttrcode/main.cpp -o $@
$(B)/ttrcode_app.o: $(B)/.dir src/ttrcode/app.cpp $(TTRCODE_H)
	$(COMPILE) -c src/ttrcode/app.cpp -o $@
$(B)/ttrcode_dock.o: $(B)/.dir src/ttrcode/dock.cpp $(TTRCODE_H)
	$(COMPILE) -c src/ttrcode/dock.cpp -o $@
$(B)/ttrcode_debug.o: $(B)/.dir src/ttrcode/debug.cpp $(TTRCODE_H)
	$(COMPILE) -c src/ttrcode/debug.cpp -o $@
$(B)/ttrcode_debugpanes.o: $(B)/.dir src/ttrcode/debugpanes.cpp $(TTRCODE_H)
	$(COMPILE) -c src/ttrcode/debugpanes.cpp -o $@
$(B)/ttrcode_celllist.o: $(B)/.dir src/ttrcode/celllist.cpp $(TTRCODE_H)
	$(COMPILE) -c src/ttrcode/celllist.cpp -o $@
$(B)/ttrcode_valuetree.o: $(B)/.dir src/ttrcode/valuetree.cpp $(TTRCODE_H)
	$(COMPILE) -c src/ttrcode/valuetree.cpp -o $@
$(B)/ttrcode_findfiles.o: $(B)/.dir src/ttrcode/findfiles.cpp $(TTRCODE_H)
	$(COMPILE) -c src/ttrcode/findfiles.cpp -o $@
$(B)/ttrcode_options.o: $(B)/.dir src/ttrcode/options.cpp $(TTRCODE_H)
	$(COMPILE) -c src/ttrcode/options.cpp -o $@
$(B)/ttrcode_editsource.o: $(B)/.dir src/ttrcode/editsource.cpp src/ttrcode/editsource.h \
		$(TE_H) tw/tw.h
	$(COMPILE) -c src/ttrcode/editsource.cpp -o $@

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
$(B)/tl_kitty.o: $(B)/.dir tl/kitty.cpp $(TL_TERMINAL_H)
	$(COMPILE) -c tl/kitty.cpp -o $@
$(B)/tl_image.o: $(B)/.dir tl/image.cpp tl/image.h tl/grid.h tl/port.h
	$(COMPILE) -c tl/image.cpp -o $@
$(B)/tl_view.o: $(B)/.dir tl/view.cpp $(TL_TERMINAL_H)
	$(COMPILE) -c tl/view.cpp -o $@
$(B)/tl_test_image.o: $(B)/.dir tl/test/test_image.cpp tl/test/tltest.h $(TL_TERMINAL_H)
	$(COMPILE) -c tl/test/test_image.cpp -o $@
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
		$(CORE_H) $(TX_H)
	$(COMPILE) -c src/shell/colorpicker.cpp -o $@

$(B)/shell_colorspage.o: $(B)/.dir src/shell/colorspage.cpp $(SHELL_H) \
		$(X11_H)
	$(COMPILE) -c src/shell/colorspage.cpp -o $@

$(B)/shell_netstatus.o: $(B)/.dir src/shell/netstatus.cpp $(SHELL_H) $(X11_H) $(TX_H)
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

$(B)/shell_settings.o: $(B)/.dir src/shell/settings.cpp $(SHELL_H) $(X11_H) $(TX_H)
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

$(B)/shell_volume.o: $(B)/.dir src/shell/volume.cpp $(SHELL_H) $(X11_H) $(TX_H)
	$(COMPILE) -c src/shell/volume.cpp -o $@

$(B)/shell_winmenu.o: $(B)/.dir src/shell/winmenu.cpp $(SHELL_H) $(CORE_H)
	$(COMPILE) -c src/shell/winmenu.cpp -o $@

$(B)/x11_config.o: $(B)/.dir src/x11/config.cpp $(X11_H)
	$(COMPILE) -c src/x11/config.cpp -o $@

$(B)/x11_hints.o: $(B)/.dir src/x11/hints.cpp $(X11_H)
	$(COMPILE) -c src/x11/hints.cpp -o $@

$(B)/x11_iconload.o: $(B)/.dir src/x11/iconload.cpp $(X11_H) ti/ti.h
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
install: $(B)/ttr $(B)/ttrt $(B)/ttrc $(B)/ttre $(B)/ttrcode
	mkdir -p $(DESTDIR)$(BINDIR) $(DESTDIR)$(XSESSIONDIR)
	if [ -f $(DESTDIR)$(BINDIR)/ttr ]; then \
		mv -f $(DESTDIR)$(BINDIR)/ttr $(DESTDIR)$(BINDIR)/ttr.old; fi
	cp $(B)/ttr $(DESTDIR)$(BINDIR)/ttr
	chmod 755 $(DESTDIR)$(BINDIR)/ttr
	rm -f $(DESTDIR)$(BINDIR)/ttrt
	cp $(B)/ttrt $(DESTDIR)$(BINDIR)/ttrt
	chmod 755 $(DESTDIR)$(BINDIR)/ttrt
	rm -f $(DESTDIR)$(BINDIR)/ttrc
	cp $(B)/ttrc $(DESTDIR)$(BINDIR)/ttrc
	chmod 755 $(DESTDIR)$(BINDIR)/ttrc
	rm -f $(DESTDIR)$(BINDIR)/ttrcode
	cp $(B)/ttrcode $(DESTDIR)$(BINDIR)/ttrcode
	chmod 755 $(DESTDIR)$(BINDIR)/ttrcode
	rm -f $(DESTDIR)$(BINDIR)/ttre
	cp $(B)/ttre $(DESTDIR)$(BINDIR)/ttre
	chmod 755 $(DESTDIR)$(BINDIR)/ttre
	mkdir -p $(DESTDIR)$(TTRC_DATADIR)
	cp data/ttrc/tc.sh data/ttrc/tc.csh $(DESTDIR)$(TTRC_DATADIR)
	chmod 644 $(DESTDIR)$(TTRC_DATADIR)/tc.sh $(DESTDIR)$(TTRC_DATADIR)/tc.csh
	mkdir -p $(DESTDIR)$(DATADIR)/fonts
	cp $(TW_FONTS) third_party/misc-misc/COPYING $(DESTDIR)$(DATADIR)/fonts
	chmod 644 $(DESTDIR)$(DATADIR)/fonts/*
	if [ -n "$(TERMINFO_DIR)" ]; then \
		mkdir -p $(DESTDIR)$(TERMINFO_DIR) && \
		TERMINFO=$(DESTDIR)$(TERMINFO_DIR) tic data/ttrt.ti; \
	elif [ -n "$(DESTDIR)" ]; then \
		mkdir -p $(DESTDIR)$(PREFIX)/share/terminfo && \
		TERMINFO=$(DESTDIR)$(PREFIX)/share/terminfo tic data/ttrt.ti; \
	else \
		tic data/ttrt.ti || echo "tic failed: ttrt will use TERM=xterm" >&2; \
	fi
	cp dist/ttr-session $(DESTDIR)$(BINDIR)/ttr-session
	chmod 755 $(DESTDIR)$(BINDIR)/ttr-session
	cp dist/ttr.desktop $(DESTDIR)$(XSESSIONDIR)/ttr.desktop
	chmod 644 $(DESTDIR)$(XSESSIONDIR)/ttr.desktop
	mkdir -p $(DESTDIR)$(DATADIR)/schemes/ttr $(DESTDIR)$(DATADIR)/schemes/irix \
		$(DESTDIR)$(DATADIR)/schemes/text
	cp data/schemes/ttr/*.ad data/schemes/ttr/README.md \
		$(DESTDIR)$(DATADIR)/schemes/ttr
	cp data/schemes/irix/*.ad data/schemes/irix/README.md \
		$(DESTDIR)$(DATADIR)/schemes/irix
	cp data/schemes/text/*.scheme data/schemes/text/README.md \
		$(DESTDIR)$(DATADIR)/schemes/text
	chmod 644 $(DESTDIR)$(DATADIR)/schemes/ttr/* \
		$(DESTDIR)$(DATADIR)/schemes/irix/* $(DESTDIR)$(DATADIR)/schemes/text/*

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ttr $(DESTDIR)$(BINDIR)/ttr.old \
		$(DESTDIR)$(BINDIR)/ttrt $(DESTDIR)$(BINDIR)/ttr-session \
		$(DESTDIR)$(BINDIR)/ttrc $(DESTDIR)$(BINDIR)/ttrcode $(DESTDIR)$(BINDIR)/ttre \
		$(DESTDIR)$(TERMINFO_DIR)/t/ttrt \
		$(DESTDIR)/usr/share/terminfo/t/ttrt \
		$(DESTDIR)/usr/share/lib/terminfo/t/ttrt \
		$(DESTDIR)$(XSESSIONDIR)/ttr.desktop
	rm -rf $(DESTDIR)$(DATADIR) $(DESTDIR)$(TTRC_DATADIR)

clean:
	rm -rf $(B) build-asan
