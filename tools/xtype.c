/*
 * xtype: type into whatever window has the keyboard focus, with XTest.
 * For driving ttrt in tests, on Linux and on IRIX (plain C89).
 *
 *   xtype [-d display] [-delay ms] ARG...
 *
 * Each ARG is text to type, a key spec starting with '+' (+Return,
 * +ctrl+c, +shift+Prior, +alt+x; keysym names as in <X11/keysymdef.h>),
 * a click at root coordinates "@x,y" (button 1; "@x,y,b" for button b;
 * "@@x,y" double and "@@@x,y" triple click; "^@x,y,b" with Ctrl held),
 * a button 1 drag "~x1,y1,x2,y2", or
 * "%WxH" to resize the focused top-level window to W x H pixels.
 * Example: xtype @400,300 'ls -l' +Return
 *
 * Build: cc -o xtype xtype.c -lXtst -lXext -lX11
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

static Display* dpy;
static int delay_ms = 20;

static void pause_ms(int ms)
{
    XFlush(dpy);
    if (ms > 0)
        usleep((unsigned)ms * 1000u);
}

static KeyCode code_of(KeySym ks)
{
    return XKeysymToKeycode(dpy, ks);
}

/* Press the modifiers, the key, release in reverse order. */
static void press(KeySym ks, int shift, int ctrl, int alt)
{
    KeyCode k = code_of(ks);
    KeyCode ks_shift = code_of(XK_Shift_L), ks_ctrl = code_of(XK_Control_L), ks_alt = code_of(XK_Alt_L);
    if (!k) {
        fprintf(stderr, "xtype: no keycode for keysym 0x%lx\n", (unsigned long)ks);
        return;
    }
    /* A keysym only on the shifted level of its key needs Shift. */
    if (XKeycodeToKeysym(dpy, k, 0) != ks && XKeycodeToKeysym(dpy, k, 1) == ks)
        shift = 1;
    if (ctrl) XTestFakeKeyEvent(dpy, ks_ctrl, True, 0);
    if (alt) XTestFakeKeyEvent(dpy, ks_alt, True, 0);
    if (shift) XTestFakeKeyEvent(dpy, ks_shift, True, 0);
    XTestFakeKeyEvent(dpy, k, True, 0);
    XTestFakeKeyEvent(dpy, k, False, 0);
    if (shift) XTestFakeKeyEvent(dpy, ks_shift, False, 0);
    if (alt) XTestFakeKeyEvent(dpy, ks_alt, False, 0);
    if (ctrl) XTestFakeKeyEvent(dpy, ks_ctrl, False, 0);
    pause_ms(delay_ms);
}

static KeySym char_keysym(int c)
{
    char name[2];
    switch (c) {
    case ' ': return XK_space;
    case '\n': return XK_Return;
    case '\t': return XK_Tab;
    case '!': return XK_exclam;
    case '"': return XK_quotedbl;
    case '#': return XK_numbersign;
    case '$': return XK_dollar;
    case '%': return XK_percent;
    case '&': return XK_ampersand;
    case '\'': return XK_apostrophe;
    case '(': return XK_parenleft;
    case ')': return XK_parenright;
    case '*': return XK_asterisk;
    case '+': return XK_plus;
    case ',': return XK_comma;
    case '-': return XK_minus;
    case '.': return XK_period;
    case '/': return XK_slash;
    case ':': return XK_colon;
    case ';': return XK_semicolon;
    case '<': return XK_less;
    case '=': return XK_equal;
    case '>': return XK_greater;
    case '?': return XK_question;
    case '@': return XK_at;
    case '[': return XK_bracketleft;
    case '\\': return XK_backslash;
    case ']': return XK_bracketright;
    case '^': return XK_asciicircum;
    case '_': return XK_underscore;
    case '`': return XK_grave;
    case '{': return XK_braceleft;
    case '|': return XK_bar;
    case '}': return XK_braceright;
    case '~': return XK_asciitilde;
    default: break;
    }
    name[0] = (char)c;
    name[1] = 0;
    return XStringToKeysym(name);
}

static void type_text(const char* s)
{
    for (; *s; s++) {
        KeySym ks = char_keysym((unsigned char)*s);
        int shift = *s >= 'A' && *s <= 'Z';
        if (ks == NoSymbol) {
            fprintf(stderr, "xtype: cannot type '%c'\n", *s);
            continue;
        }
        press(ks, shift, 0, 0);
    }
}

static void type_key(const char* spec)
{
    int shift = 0, ctrl = 0, alt = 0;
    char buf[128];
    char* p;
    char* name;
    strncpy(buf, spec + 1, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    name = buf;
    while ((p = strchr(name, '+')) != 0 && p[1]) {
        *p = 0;
        if (strcmp(name, "shift") == 0) shift = 1;
        else if (strcmp(name, "ctrl") == 0) ctrl = 1;
        else if (strcmp(name, "alt") == 0) alt = 1;
        name = p + 1;
    }
    {
        KeySym ks = XStringToKeysym(name);
        if (ks == NoSymbol && strlen(name) == 1)
            ks = char_keysym((unsigned char)name[0]);
        if (ks == NoSymbol) {
            fprintf(stderr, "xtype: unknown key %s\n", name);
            return;
        }
        press(ks, shift, ctrl, alt);
    }
}

/* The top-level client window holding the focus: the focus window or
 * the nearest ancestor with WM_STATE. */
static Window focused_toplevel(void)
{
    Window w, root, parent, *kids;
    int revert;
    unsigned int n;
    Atom wm_state = XInternAtom(dpy, "WM_STATE", False);
    XGetInputFocus(dpy, &w, &revert);
    while (w && w != PointerRoot) {
        Atom type;
        int format;
        unsigned long items, after;
        unsigned char* data = 0;
        if (XGetWindowProperty(dpy, w, wm_state, 0, 2, False, AnyPropertyType, &type, &format,
                               &items, &after, &data) == Success && type != None) {
            if (data)
                XFree(data);
            return w;
        }
        if (!XQueryTree(dpy, w, &root, &parent, &kids, &n))
            break;
        if (kids)
            XFree(kids);
        if (parent == root)
            return w;
        w = parent;
    }
    return 0;
}

int main(int argc, char** argv)
{
    const char* display = 0;
    int i, ev, er, mj, mn;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
            display = argv[++i];
        else if (strcmp(argv[i], "-delay") == 0 && i + 1 < argc)
            delay_ms = atoi(argv[++i]);
        else
            break;
    }
    dpy = XOpenDisplay(display);
    if (!dpy) {
        fprintf(stderr, "xtype: cannot open display\n");
        return 1;
    }
    if (!XTestQueryExtension(dpy, &ev, &er, &mj, &mn)) {
        fprintf(stderr, "xtype: no XTEST extension\n");
        return 1;
    }
    for (; i < argc; i++) {
        int x, y, b = 1;
        int x2, y2;
        if (argv[i][0] == '@' || (argv[i][0] == '^' && argv[i][1] == '@')) {
            int clicks = 0, k;
            int ctrl = argv[i][0] == '^';
            KeyCode ctrl_kc = XKeysymToKeycode(dpy, XK_Control_L);
            while (argv[i][ctrl + clicks] == '@')
                clicks++;
            if (sscanf(argv[i] + ctrl + clicks, "%d,%d,%d", &x, &y, &b) >= 2) {
                XTestFakeMotionEvent(dpy, -1, x, y, 0);
                if (ctrl)
                    XTestFakeKeyEvent(dpy, ctrl_kc, True, 0);
                for (k = 0; k < clicks; k++) {
                    XTestFakeButtonEvent(dpy, (unsigned)b, True, 0);
                    XTestFakeButtonEvent(dpy, (unsigned)b, False, 0);
                    pause_ms(30);
                }
                if (ctrl)
                    XTestFakeKeyEvent(dpy, ctrl_kc, False, 0);
                pause_ms(250);
            }
        } else if (argv[i][0] == '~' &&
                   sscanf(argv[i] + 1, "%d,%d,%d,%d", &x, &y, &x2, &y2) == 4) {
            int steps = 8, k;
            XTestFakeMotionEvent(dpy, -1, x, y, 0);
            XTestFakeButtonEvent(dpy, 1, True, 0);
            pause_ms(30);
            for (k = 1; k <= steps; k++) {
                XTestFakeMotionEvent(dpy, -1, x + (x2 - x) * k / steps, y + (y2 - y) * k / steps, 0);
                pause_ms(30);
            }
            XTestFakeButtonEvent(dpy, 1, False, 0);
            pause_ms(250);
        } else if (argv[i][0] == '%' && sscanf(argv[i] + 1, "%dx%d", &x, &y) == 2) {
            Window t = focused_toplevel();
            if (t)
                XResizeWindow(dpy, t, (unsigned)x, (unsigned)y);
            else
                fprintf(stderr, "xtype: no focused window to resize\n");
            pause_ms(300);
        } else if (argv[i][0] == '+' && argv[i][1])
            type_key(argv[i]);
        else
            type_text(argv[i]);
    }
    XSync(dpy, False);
    XCloseDisplay(dpy);
    return 0;
}
