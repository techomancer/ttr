/*
 * X11 mode window manager: reparents clients into mwm-style frames.
 * Runs inside the Xt main loop; WM events are routed through Xt event
 * dispatchers so they are handled even inside Motif's nested loops.
 */
#ifndef TTR_X11_WM_H
#define TTR_X11_WM_H

#include "core/frame.h"
#include "core/geom.h"
#include "core/str.h"
#include "core/vector.h"
#include "core/wmlink.h"

#include <X11/Intrinsic.h>

namespace ttr {

class WindowMenu;
class XPainter;

struct Client {
    Window win;
    Window frame;
    int x, y;           /* frame position */
    int w, h;           /* client size */
    int old_bw;
    Str title;
    SizeConstraints size;
    bool accepts_input;
    bool take_focus;
    bool delete_window;
    bool maximized;
    Rect saved;         /* frame x/y and client w/h before maximize */
    int ignore_unmap;
    FramePart pressed;
    FramePart cursor_part;

    Client();
};

class Wm : public WmLink {
public:
    Wm();
    ~Wm();

    bool init(int* argc, char** argv);
    int run();

    void request(ToplevelId id, WmAction action);

private:
    enum DragMode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };

    struct Drag {
        DragMode mode;
        Client* client;
        FramePart part;
        bool explicit_grab;
        int start_x, start_y;   /* pointer, root coordinates */
        int orig_x, orig_y;     /* frame position */
        int orig_w, orig_h;     /* client size */
    };

    static Boolean dispatch(XEvent* ev);
    static int on_x_error(Display* dpy, XErrorEvent* ev);
    static int on_startup_error(Display* dpy, XErrorEvent* ev);
    static void on_signal_input(XtPointer closure, int* fd, XtInputId* id);

    bool handle(XEvent* ev);
    void on_map_request(XMapRequestEvent* ev);
    void on_configure_request(XConfigureRequestEvent* ev);
    bool on_unmap(XUnmapEvent* ev);
    bool on_destroy(XDestroyWindowEvent* ev);
    bool on_property(XPropertyEvent* ev);
    bool on_client_message(XClientMessageEvent* ev);
    bool on_button_press(XButtonEvent* ev);
    bool on_button_release(XButtonEvent* ev);
    bool on_motion(XMotionEvent* ev);
    bool on_expose(XExposeEvent* ev);

    void adopt_existing();
    void manage(Window w, bool already_mapped);
    void unmanage(Client* c, bool destroyed);
    void restore_all();

    void read_title(Client* c);
    void read_size_hints(Client* c);
    void read_wm_hints(Client* c);
    void read_protocols(Client* c);

    Client* find_client(Window w);
    Client* find_frame(Window w);
    Client* find_id(ToplevelId id);

    void focus(Client* c, Time t);
    void raise(Client* c);
    void lower(Client* c);
    void update_grabs(Client* c, bool focused);
    void send_protocol(Client* c, Atom proto, Time t);
    void close(Client* c);
    void toggle_maximize(Client* c);

    void set_geometry(Client* c, int x, int y, int w, int h);
    void send_configure(Client* c);
    void set_wm_state(Window w, long state);
    void paint(Client* c);
    Cursor cursor_for(FramePart part);

    void begin_drag(Client* c, DragMode mode, FramePart part,
                    int x_root, int y_root, bool explicit_grab, Time t);
    void update_drag(int x_root, int y_root);
    void end_drag(Time t);

    XtAppContext app_;
    Widget toplevel_;
    Display* dpy_;
    int screen_;
    Window root_;
    int screen_w_, screen_h_;
    bool running_;
    int signal_pipe_[2];

    Atom wm_protocols_, wm_delete_window_, wm_take_focus_;
    Atom wm_state_, wm_change_state_;

    XPainter* painter_;
    FrameTheme theme_;
    WindowMenu* menu_;

    Cursor cursor_normal_, cursor_move_;
    Cursor cursor_border_[8];   /* FP_N .. FP_NW */

    Vector<Client*> clients_;   /* stacking order, bottom first */
    Client* active_;
    Drag drag_;
    int place_x_, place_y_;
};

}

#endif
