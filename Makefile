# To The Roots - portable Makefile.
#
# Plain make only: no pattern rules, no conditionals, no functions, no
# automatic variables except $@. Works with GNU make, BSD make and IRIX smake.
#
# Defaults are for Linux with gcc. Override on the command line, e.g.
#
#   make CXX=clang++
#   make CXX=CC CXXFLAGS="-n32 -O2 -LANG:exceptions=OFF" \
#        WARNFLAGS= LIBS="-lXm -lXt -lX11"                    (IRIX, MIPSpro)
#
# Every object has an explicit rule. When you add a source file, add its
# object to OBJS and give it a rule listing the headers it includes.

CXX = g++
CXXFLAGS = -std=c++98 -O2 -g -fno-exceptions -fno-rtti
WARNFLAGS = -pedantic -Wall -Wextra
CPPFLAGS = -Isrc
LDFLAGS =
LIBS = -lXm -lXt -lX11

B = build
COMPILE = $(CXX) $(CPPFLAGS) $(CXXFLAGS) $(WARNFLAGS)

OBJS = $(B)/main.o \
	$(B)/core_apps.o \
	$(B)/core_color.o \
	$(B)/core_frame.o \
	$(B)/core_geom.o \
	$(B)/core_icon.o \
	$(B)/core_painter.o \
	$(B)/core_str.o \
	$(B)/core_switcher.o \
	$(B)/core_text.o \
	$(B)/shell_launch.o \
	$(B)/shell_shell.o \
	$(B)/shell_startmenu.o \
	$(B)/shell_taskbar.o \
	$(B)/shell_tray.o \
	$(B)/shell_winmenu.o \
	$(B)/x11_config.o \
	$(B)/x11_hints.o \
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
	src/core/text.h src/core/icon.h src/core/apps.h src/core/switcher.h
X11_H = src/x11/wm.h src/x11/hints.h src/x11/config.h src/x11/xpainter.h \
	$(CORE_H)
SHELL_H = src/shell/shell.h src/shell/startmenu.h src/shell/taskbar.h \
	src/shell/tray.h src/shell/winmenu.h src/shell/launch.h

$(B)/main.o: $(B)/.dir src/main.cpp $(X11_H) $(SHELL_H)
	$(COMPILE) -c src/main.cpp -o $@

$(B)/core_apps.o: $(B)/.dir src/core/apps.cpp $(CORE_H)
	$(COMPILE) -c src/core/apps.cpp -o $@

$(B)/core_color.o: $(B)/.dir src/core/color.cpp $(CORE_H)
	$(COMPILE) -c src/core/color.cpp -o $@

$(B)/core_frame.o: $(B)/.dir src/core/frame.cpp $(CORE_H)
	$(COMPILE) -c src/core/frame.cpp -o $@

$(B)/core_geom.o: $(B)/.dir src/core/geom.cpp $(CORE_H)
	$(COMPILE) -c src/core/geom.cpp -o $@

$(B)/core_icon.o: $(B)/.dir src/core/icon.cpp $(CORE_H)
	$(COMPILE) -c src/core/icon.cpp -o $@

$(B)/core_painter.o: $(B)/.dir src/core/painter.cpp $(CORE_H)
	$(COMPILE) -c src/core/painter.cpp -o $@

$(B)/core_str.o: $(B)/.dir src/core/str.cpp $(CORE_H)
	$(COMPILE) -c src/core/str.cpp -o $@

$(B)/core_switcher.o: $(B)/.dir src/core/switcher.cpp $(CORE_H)
	$(COMPILE) -c src/core/switcher.cpp -o $@

$(B)/core_text.o: $(B)/.dir src/core/text.cpp $(CORE_H)
	$(COMPILE) -c src/core/text.cpp -o $@

$(B)/shell_launch.o: $(B)/.dir src/shell/launch.cpp $(SHELL_H)
	$(COMPILE) -c src/shell/launch.cpp -o $@

$(B)/shell_shell.o: $(B)/.dir src/shell/shell.cpp $(SHELL_H) $(CORE_H)
	$(COMPILE) -c src/shell/shell.cpp -o $@

$(B)/shell_startmenu.o: $(B)/.dir src/shell/startmenu.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/startmenu.cpp -o $@

$(B)/shell_taskbar.o: $(B)/.dir src/shell/taskbar.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/taskbar.cpp -o $@

$(B)/shell_tray.o: $(B)/.dir src/shell/tray.cpp $(SHELL_H) $(X11_H)
	$(COMPILE) -c src/shell/tray.cpp -o $@

$(B)/shell_winmenu.o: $(B)/.dir src/shell/winmenu.cpp $(SHELL_H) $(CORE_H)
	$(COMPILE) -c src/shell/winmenu.cpp -o $@

$(B)/x11_config.o: $(B)/.dir src/x11/config.cpp $(X11_H)
	$(COMPILE) -c src/x11/config.cpp -o $@

$(B)/x11_hints.o: $(B)/.dir src/x11/hints.cpp $(X11_H)
	$(COMPILE) -c src/x11/hints.cpp -o $@

$(B)/x11_wm.o: $(B)/.dir src/x11/wm.cpp $(X11_H)
	$(COMPILE) -c src/x11/wm.cpp -o $@

$(B)/x11_xpainter.o: $(B)/.dir src/x11/xpainter.cpp $(X11_H)
	$(COMPILE) -c src/x11/xpainter.cpp -o $@

clean:
	rm -rf $(B)
