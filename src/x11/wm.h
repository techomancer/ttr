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
#include "x11/config.h"
#include "x11/hints.h"

#include <X11/Intrinsic.h>

namespace ttr {

class Icon;
class XPainter;

struct Client {
    Window win;
    Window frame;
    Window transient_for;
    int x, y;           /* frame position */
    int w, h;           /* client size */
    int old_bw;
    int gravity;
    Str title;
    Icon* icon;         /* owned, may be null */
    WindowType type;
    FrameDecor decor;   /* decoration when not fullscreen */
    ActionMask actions; /* from type and _MOTIF_WM_HINTS */
    SizeConstraints size;
    bool accepts_input;
    bool take_focus;
    bool delete_window;
    bool maximized;
    bool fullscreen;
    bool minimized;
    bool listed;        /* has a taskbar entry */
    int desktop;        /* or ALL_DESKTOPS */
    int snap;           /* SNAP_*: docked or snapped to an edge */
    Rect saved;         /* frame x/y and client w/h in the normal state */
    int ignore_unmap;
    FramePart pressed;
    FramePart cursor_part;

    Client();
    ~Client();
};

enum SnapPos {
    SNAP_NONE, SNAP_LEFT, SNAP_CENTER, SNAP_RIGHT, SNAP_TOP, SNAP_BOTTOM
};

class Wm : public WmLink {
public:
    Wm();
    ~Wm();

    bool init(int* argc, char** argv);
    /* Parent widget for the shell's Motif widgets. */
    Widget shell_parent() const { return menu_parent_; }
    const Config& config() const { return config_; }
    void set_shell(ShellLink* shell) { shell_ = shell; }
    /* Adopt existing windows; call after set_shell(). */
    void start();
    int run();
    bool restart_requested() const { return restart_; }

    /* WmLink */
    void request(ToplevelId id, WmAction action);
    void switch_desktop(int desktop);
    void move_to_desktop(ToplevelId id, int desktop);
    void menu_shown(bool shown);
    void set_overlay(unsigned long window, bool on);
    void set_panel(unsigned long window, const Rect& r);
    void quit();
    void restart();

private:
    enum DragMode { DRAG_NONE, DRAG_MOVE, DRAG_RESIZE };
    enum ManageMode { MANAGE_NEW, MANAGE_MAPPED, MANAGE_ICONIC, MANAGE_OWN };

    struct Drag {
        DragMode mode;
        Client* client;
        FramePart part;
        bool explicit_grab;     /* started from menu or keyboard */
        int start_x, start_y;   /* pointer, root coordinates */
        int orig_x, orig_y;     /* frame position */
        int orig_w, orig_h;     /* client size */
        int zone;               /* SNAP_* under the pointer while moving */
        int was_snap;           /* docked before the drag; Escape restores */
    };

    static Boolean dispatch(XEvent* ev);
    static int on_x_error(Display* dpy, XErrorEvent* ev);
    static int on_startup_error(Display* dpy, XErrorEvent* ev);
    static void on_signal_input(XtPointer closure, int* fd, XtInputId* id);

    bool handle(XEvent* ev);
    void on_map_request(XMapRequestEvent* ev);
    bool on_map_notify(XMapEvent* ev);
    void on_configure_request(XConfigureRequestEvent* ev);
    bool on_unmap(XUnmapEvent* ev);
    bool on_destroy(XDestroyWindowEvent* ev);
    bool on_property(XPropertyEvent* ev);
    bool on_client_message(XClientMessageEvent* ev);
    bool on_button_press(XButtonEvent* ev);
    bool on_button_release(XButtonEvent* ev);
    bool on_motion(XMotionEvent* ev);
    bool on_enter(XCrossingEvent* ev);
    bool on_expose(XExposeEvent* ev);
    bool on_key_press(XKeyEvent* ev);
    bool on_key_release(XKeyEvent* ev);
    void on_mapping(XMappingEvent* ev);

    void setup_ewmh();
    void grab_keys();
    void adopt_existing();
    void manage(Window w, ManageMode mode);
    void unmanage(Client* c, bool destroyed);
    void restore_all();
    long get_wm_state(Window w);

    void read_decor(Client* c);
    void read_size_hints(Client* c);
    void read_wm_hints(Client* c);
    void read_protocols(Client* c);

    Client* find_client(Window w);
    Client* find_frame(Window w);
    Client* find_id(ToplevelId id);

    FrameDecor decor_of(const Client* c) const;
    FrameExtents extents_of(const Client* c) const;
    int frame_w(const Client* c) const;
    int frame_h(const Client* c) const;
    ActionMask allowed(const Client* c) const;
    bool cycles(const Client* c) const;
    bool on_taskbar(const Client* c) const;
    Client* owner_of(Client* c);
    Client* next_focus(Client* except);

    void focus(Client* c, Time t);
    void activate(Client* c, Time t);
    void raise(Client* c);
    void lower(Client* c);
    void cycle(bool forward, Time t);
    void update_grabs(Client* c, bool focused);
    void send_protocol(Client* c, Atom proto, Time t);
    void close(Client* c);
    void post_menu(Client* c, int x, int y, Time t);
    void post_menu_at_button(Client* c, Time t);

    Rect snap_rect(int snap) const;
    void set_snap(Client* c, int snap);
    int snap_zone(int x_root, int y_root) const;
    void show_snap_preview(int snap);
    void set_maximized(Client* c, bool on);
    void set_fullscreen(Client* c, bool on);
    void set_minimized(Client* c, bool on);
    void minimize_all(Time t);
    void restore_all(Time t);
    void leave_showing_desktop();
    void publish_showing_desktop();
    void apply_state(Client* c, bool restore);

    void menu_double_click(XButtonEvent* ev);
    bool on_current(const Client* c) const;
    void set_desktop(Client* c, int desktop);
    void publish_desktops();
    void show_osd();
    void paint_osd();
    static void osd_timeout(XtPointer closure, XtIntervalId* id);

    void begin_switch(bool forward, Time t);
    void step_switch(int dir);
    void end_switch(bool commit, Time t);
    void paint_switch();

    void notify(Client* c);
    void set_geometry(Client* c, int x, int y, int w, int h);
    void send_configure(Client* c);
    void set_wm_state(Window w, long state);
    void set_net_state(Client* c);
    void set_frame_extents(Client* c);
    void update_client_lists();
    void update_workarea();
    void paint(Client* c);
    Cursor cursor_for(FramePart part);

    void begin_drag(Client* c, DragMode mode, FramePart part,
                    int x_root, int y_root, bool explicit_grab, Time t);
    void update_drag(int x_root, int y_root);
    void end_drag(Time t, bool cancel);

    XtAppContext app_;
    Widget toplevel_;
    Widget menu_parent_;
    Display* dpy_;
    int screen_;
    Window root_;
    Window check_win_;
    int screen_w_, screen_h_;
    Rect work_;                 /* screen minus the panel */
    bool running_;
    bool restart_;
    int signal_pipe_[2];

    Config config_;
    Atoms atoms_;
    XPainter* painter_;
    FrameTheme theme_;
    ShellLink* shell_;
    Window panel_;

    Cursor cursor_normal_, cursor_move_;
    Cursor cursor_border_[8];   /* FP_N .. FP_NW */

    Vector<Client*> clients_;   /* stacking order, bottom first */
    Vector<Client*> mapped_;    /* order of management, for EWMH */
    Vector<Client*> mru_;       /* focus history, most recent first */
    Client* active_;
    Drag drag_;
    int place_x_, place_y_;
    Time last_click_time_;      /* title bar double click detection */
    Window last_click_win_;

    /* Shortcut keys from ~/.config/ttr/shortcuts. */
    struct UserKey {
        KeySym sym;
        unsigned int mods;
        Str command;
    };
    void load_user_keys();
    Vector<UserKey> user_keys_;
    /* The Windows key opens Start when released, unless used with
     * another key in the meantime. */
    bool super_pending_;

    /* A shell menu is posted and owns the pointer and keyboard: one the
     * shell told us about, or any of our own Motif popup shells mapped. */
    bool menu_up() const;
    void popups_changed();
    bool shell_menu_up_;
    Vector<Window> own_popups_;
    Time menu_click_time_;      /* last press on a window menu button */
    Window menu_click_win_;
    Window close_after_menu_;   /* double click on the menu button */

    /* Virtual desktops */
    int desktops_;
    int current_;
    Window osd_win_;
    XtIntervalId osd_timer_;

    /* Shell windows kept above everything (notifications). */
    Vector<Window> overlays_;

    /* Outline of where a window will snap, while dragging. */
    Window snap_preview_[4];

    /* Windows-style Alt+Tab */
    bool switching_;
    /* Super+D, Super+M: the windows minimized to show the desktop, and
     * the one that was active, to bring back as they were. */
    bool showing_desktop_;
    bool restoring_desktop_;
    Vector<Window> shown_away_;
    Window shown_active_;
    Window switch_win_;
    Vector<Client*> switch_list_;
    int switch_sel_;
};

}

#endif
