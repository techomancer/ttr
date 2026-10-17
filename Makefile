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
	$(B)/core_color.o \
	$(B)/core_frame.o \
	$(B)/core_geom.o \
	$(B)/core_str.o \
	$(B)/core_text.o \
	$(B)/shell_winmenu.o \
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
	src/core/text.h
WM_H = src/x11/wm.h src/x11/hints.h $(CORE_H)

$(B)/main.o: $(B)/.dir src/main.cpp $(WM_H)
	$(COMPILE) -c src/main.cpp -o $@

$(B)/core_color.o: $(B)/.dir src/core/color.cpp src/core/color.h
	$(COMPILE) -c src/core/color.cpp -o $@

$(B)/core_frame.o: $(B)/.dir src/core/frame.cpp $(CORE_H)
	$(COMPILE) -c src/core/frame.cpp -o $@

$(B)/core_geom.o: $(B)/.dir src/core/geom.cpp src/core/geom.h
	$(COMPILE) -c src/core/geom.cpp -o $@

$(B)/core_str.o: $(B)/.dir src/core/str.cpp src/core/str.h
	$(COMPILE) -c src/core/str.cpp -o $@

$(B)/core_text.o: $(B)/.dir src/core/text.cpp src/core/text.h src/core/str.h
	$(COMPILE) -c src/core/text.cpp -o $@

$(B)/shell_winmenu.o: $(B)/.dir src/shell/winmenu.cpp src/shell/winmenu.h \
		src/core/wmlink.h
	$(COMPILE) -c src/shell/winmenu.cpp -o $@

$(B)/x11_hints.o: $(B)/.dir src/x11/hints.cpp src/x11/hints.h $(CORE_H)
	$(COMPILE) -c src/x11/hints.cpp -o $@

$(B)/x11_wm.o: $(B)/.dir src/x11/wm.cpp $(WM_H) src/x11/xpainter.h \
		src/shell/winmenu.h
	$(COMPILE) -c src/x11/wm.cpp -o $@

$(B)/x11_xpainter.o: $(B)/.dir src/x11/xpainter.cpp src/x11/xpainter.h \
		$(CORE_H)
	$(COMPILE) -c src/x11/xpainter.cpp -o $@

clean:
	rm -rf $(B)
