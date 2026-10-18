/*
 * The volume icon in the notification area and its Volume dialog and
 * menu. The sound server is probed asynchronously at start (see
 * core/audio.h); until one answers, there is no icon.
 *
 *   left click    Volume dialog: slider, Mute, sound panel to open
 *   right click   menu: Mute, Volume Control..., every installed panel
 *   middle click  toggle mute
 *   wheel         volume up / down by 5%
 */
#ifndef TTR_SHELL_VOLUME_H
#define TTR_SHELL_VOLUME_H

#include "core/audio.h"
#include "core/color.h"
#include "core/geom.h"
#include "core/str.h"
#include "core/vector.h"
#include "core/wmlink.h"
#include "shell/trayitem.h"

#include <X11/Intrinsic.h>

namespace ttr {

class CommandRunner;
class Icon;
class IconLoader;
class Painter;
struct Config;

class VolumeControl : public TrayItem {
public:
    /* 'changed' is called when the icon appears or its look changes. */
    VolumeControl(Widget parent, WmLink* wm, const Config& cfg,
                  IconLoader* icons, const ColorSet& cs,
                  void (*changed)(void*), void* closure);
    ~VolumeControl();

    bool available() const { return server_ != AUDIO_NONE; }
    bool visible() const { return available(); }
    void paint(Painter& p, const Rect& r);
    /* A button event inside the icon (from the notification area). */
    void button(XButtonEvent* ev);

private:
    static void probe_done(void* closure, bool ok, const char* output);
    static void read_done(void* closure, bool ok, const char* output);
    static void poll_cb(XtPointer closure, XtIntervalId* id);
    static void soon_cb(XtPointer closure, XtIntervalId* id);
    static void drag_cb(XtPointer closure, XtIntervalId* id);
    static void scale_cb(Widget w, XtPointer closure, XtPointer call);
    static void mute_cb(Widget w, XtPointer closure, XtPointer call);
    static void open_panel_cb(Widget w, XtPointer closure, XtPointer call);
    static void panel_item_cb(Widget w, XtPointer closure, XtPointer call);
    static void dialog_item_cb(Widget w, XtPointer closure, XtPointer call);
    static void close_cb(Widget w, XtPointer closure, XtPointer call);
    static void menu_map_cb(Widget w, XtPointer closure, XtPointer call);
    static void menu_unmap_cb(Widget w, XtPointer closure, XtPointer call);
    static void deferred_cb(XtPointer closure, XtIntervalId* id);

    void probe(int index);
    void refresh();
    void refresh_soon();
    void schedule_poll();
    void set_volume(int percent);
    void set_mute(int mute);
    void show_dialog();
    void build_dialog();
    void build_menu();
    void post_menu(XButtonEvent* ev);
    void update_widgets();
    void open_panel(int i);

    Widget parent_;
    WmLink* wm_;
    ColorSet cs_;
    void (*changed_)(void*);
    void* closure_;
    CommandRunner* runner_;

    AudioServer server_;
    Str server_name_;
    int probe_index_;
    VolumeState state_;
    Icon* icons_[4];            /* muted, low, medium, high */

    Vector<SoundPanel*> panels_;
    Widget dialog_;
    Widget title_;
    Widget scale_;
    Widget mute_;
    Widget panel_menu_;         /* option menu in the dialog */
    int dialog_panel_;
    Widget menu_;               /* right click menu */
    Widget menu_mute_;
    bool loading_;              /* setting widgets; ignore callbacks */
    int drag_value_;
    XtIntervalId drag_timer_;
    XtIntervalId poll_timer_;

    struct PanelItem {
        VolumeControl* control;
        int index;              /* into panels_, or -1: Volume Control... */
    };
    Vector<PanelItem*> items_;
    int pending_panel_;
};

}

#endif
