#include "shell/volume.h"
#include "shell/runcmd.h"
#include "core/icon.h"
#include "core/launch.h"
#include "core/painter.h"
#include "core/scale.h"
#include "shell/popup.h"
#include "x11/config.h"
#include "x11/iconload.h"

#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/PushBG.h>
#include <Xm/RowColumn.h>
#include <Xm/Scale.h>
#include <Xm/SeparatoG.h>
#include <Xm/ToggleB.h>
#include <Xm/ToggleBG.h>

#include <stdio.h>
#include <string.h>

namespace ttr {

static const unsigned long k_probe_ms = 3000;
static const unsigned long k_read_ms = 2000;
static const unsigned long k_poll_ms = 3000;    /* follow outside changes */
static const unsigned long k_soon_ms = 200;     /* re-read after a change */
static const unsigned long k_drag_ms = 120;     /* while dragging the slider */
static const int k_step = 5;                    /* wheel step, percent */

static const char* const k_icon_names[4] = {
    "audio-volume-muted", "audio-volume-low", "audio-volume-medium",
    "audio-volume-high"
};

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

VolumeControl::VolumeControl(Widget parent, WmLink* wm, const Config& cfg,
                             IconLoader* icons, const ColorSet& cs,
                             void (*changed)(void*), void* closure)
    : parent_(parent), wm_(wm), cs_(cs), changed_(changed),
      closure_(closure), server_(AUDIO_NONE), probe_index_(0), dialog_(0),
      title_(0), scale_(0), mute_(0), panel_menu_(0), dialog_panel_(0),
      menu_(0), menu_mute_(0), loading_(false), drag_value_(0),
      drag_timer_(0), poll_timer_(0), pending_panel_(-2)
{
    runner_ = new CommandRunner(XtWidgetToApplicationContext(parent));
    int i;
    for (i = 0; i < 4; i++)
        icons_[i] = icons->load(k_icon_names[i], scaled(16));
    find_sound_panels(cfg.terminal.c_str(), &panels_);
    probe(0);
}

VolumeControl::~VolumeControl()
{
    if (poll_timer_)
        XtRemoveTimeOut(poll_timer_);
    if (drag_timer_)
        XtRemoveTimeOut(drag_timer_);
    delete runner_;
    int i;
    for (i = 0; i < 4; i++)
        delete icons_[i];
    for (i = 0; i < panels_.size(); i++)
        delete panels_[i];
    for (i = 0; i < items_.size(); i++)
        delete items_[i];
}

/* ------------------------------------------------------------------ */
/* Talking to the sound server                                         */

void VolumeControl::probe(int index)
{
    AudioServer s = audio_probe_order(index);
    if (s == AUDIO_NONE)
        return;     /* nothing answered: no volume icon */
    probe_index_ = index;
    if (!runner_->run(audio_probe_command(s), k_probe_ms, probe_done,
                      (void*)this))
        probe(index + 1);
}

void VolumeControl::probe_done(void* closure, bool ok, const char* output)
{
    VolumeControl* self = (VolumeControl*)closure;
    AudioServer s = audio_probe_order(self->probe_index_);
    if (!ok || !audio_probe_ok(s, output)) {
        self->probe(self->probe_index_ + 1);
        return;
    }
    self->server_ = s;
    self->server_name_ = audio_server_name(s, output);
    if (self->changed_)
        self->changed_(self->closure_);
    self->refresh();
    self->schedule_poll();
}

void VolumeControl::refresh()
{
    if (!available() || runner_->busy())
        return;
    runner_->run(audio_get_command(server_), k_read_ms, read_done,
                 (void*)this);
}

void VolumeControl::read_done(void* closure, bool ok, const char* output)
{
    VolumeControl* self = (VolumeControl*)closure;
    VolumeState v;
    if (!ok || !audio_parse(self->server_, output, &v))
        return;
    bool differs = !self->state_.valid || v.percent != self->state_.percent ||
                   v.muted != self->state_.muted;
    self->state_ = v;
    if (differs) {
        self->update_widgets();
        if (self->changed_)
            self->changed_(self->closure_);
    }
}

void VolumeControl::schedule_poll()
{
    poll_timer_ = XtAppAddTimeOut(XtWidgetToApplicationContext(parent_),
                                  k_poll_ms, poll_cb, (XtPointer)this);
}

void VolumeControl::poll_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    VolumeControl* self = (VolumeControl*)closure;
    self->poll_timer_ = 0;
    self->refresh();
    self->schedule_poll();
}

void VolumeControl::refresh_soon()
{
    XtAppAddTimeOut(XtWidgetToApplicationContext(parent_), k_soon_ms,
                    soon_cb, (XtPointer)this);
}

void VolumeControl::soon_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    ((VolumeControl*)closure)->refresh();
}

/* Changes are fire and forget; show them at once and read back soon. */
void VolumeControl::set_volume(int percent)
{
    if (!available())
        return;
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    Str cmd = audio_set_volume_command(server_, percent);
    spawn(cmd.c_str());
    state_.percent = percent;
    if (changed_)
        changed_(closure_);
    refresh_soon();
}

void VolumeControl::set_mute(int mute)
{
    if (!available())
        return;
    Str cmd = audio_set_mute_command(server_, mute);
    spawn(cmd.c_str());
    state_.muted = mute < 0 ? !state_.muted : mute != 0;
    update_widgets();
    if (changed_)
        changed_(closure_);
    refresh_soon();
}

/* ------------------------------------------------------------------ */
/* The icon                                                            */

void VolumeControl::paint(Painter& p, const Rect& r)
{
    int level;
    if (state_.muted || state_.percent == 0)
        level = 0;
    else if (state_.percent < 34)
        level = 1;
    else if (state_.percent < 67)
        level = 2;
    else
        level = 3;
    int size = r.w < r.h ? r.w : r.h;
    int x = r.x + (r.w - size) / 2, y = r.y + (r.h - size) / 2;
    const Icon* ic = icons_[level];
    if (ic)
        p.icon(*ic, x, y, size, cs_.bg);
    else
        draw_speaker_icon(p, x, y, size, level, state_.muted, cs_);
}

void VolumeControl::button(XButtonEvent* ev)
{
    if (ev->type == ButtonPress) {
        if (ev->button == Button4)
            set_volume(state_.percent + k_step);
        else if (ev->button == Button5)
            set_volume(state_.percent - k_step);
        return;
    }
    /* Menus and dialogs on release (a Motif menu posted with the button
     * still down closes again when it comes up). */
    if (ev->button == Button1)
        show_dialog();
    else if (ev->button == Button2)
        set_mute(-1);
    else if (ev->button == Button3)
        post_menu(ev);
}

/* ------------------------------------------------------------------ */
/* Volume dialog                                                       */

void VolumeControl::show_dialog()
{
    if (!dialog_)
        build_dialog();
    update_widgets();
    XtManageChild(dialog_);
    refresh();
}

void VolumeControl::build_dialog()
{
    XmString title = xmstr("Volume");
    Arg args[10];
    int n = 0;
    XtSetArg(args[n], XmNdialogTitle, title); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNmarginWidth, scaled(10)); n++;
    XtSetArg(args[n], XmNmarginHeight, scaled(10)); n++;
    dialog_ = XmCreateFormDialog(parent_, (char*)"volumeDialog", args, n);
    XmStringFree(title);

    n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNspacing, scaled(8)); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget rc = XmCreateRowColumn(dialog_, (char*)"content", args, n);

    title_ = XmCreateLabel(rc, (char*)"title", 0, 0);
    XtManageChild(title_);

    n = 0;
    XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
    XtSetArg(args[n], XmNminimum, 0); n++;
    XtSetArg(args[n], XmNmaximum, 100); n++;
    XtSetArg(args[n], XmNshowValue, True); n++;
    XtSetArg(args[n], XmNscaleWidth, scaled(260)); n++;
    scale_ = XmCreateScale(rc, (char*)"volume", args, n);
    XtAddCallback(scale_, XmNvalueChangedCallback, scale_cb, (XtPointer)this);
    XtAddCallback(scale_, XmNdragCallback, scale_cb, (XtPointer)this);
    XtManageChild(scale_);

    XmString ml = xmstr("Mute");
    XtSetArg(args[0], XmNlabelString, ml);
    mute_ = XmCreateToggleButton(rc, (char*)"mute", args, 1);
    XmStringFree(ml);
    XtAddCallback(mute_, XmNvalueChangedCallback, mute_cb, (XtPointer)this);
    XtManageChild(mute_);

    /* Sound panel: [choice] [Open] */
    n = 0;
    XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
    Widget row = XmCreateRowColumn(rc, (char*)"panelRow", args, n);
    if (panels_.empty()) {
        XmString l = xmstr("No sound panel found (pavucontrol is a good one).");
        XtSetArg(args[0], XmNlabelString, l);
        XtManageChild(XmCreateLabel(row, (char*)"none", args, 1));
        XmStringFree(l);
    } else {
        XmString l = xmstr("Sound panel:");
        XtSetArg(args[0], XmNlabelString, l);
        XtManageChild(XmCreateLabel(row, (char*)"label", args, 1));
        XmStringFree(l);

        Widget pd = XmCreatePulldownMenu(row, (char*)"panels", 0, 0);
        int i;
        for (i = 0; i < panels_.size(); i++) {
            XmString pl = xmstr(panels_[i]->label.c_str());
            XtSetArg(args[0], XmNlabelString, pl);
            Widget b = XmCreatePushButtonGadget(pd, (char*)"panel", args, 1);
            XmStringFree(pl);
            PanelItem* it = new PanelItem;
            it->control = this;
            it->index = i;
            items_.push_back(it);
            XtAddCallback(b, XmNactivateCallback, dialog_item_cb,
                          (XtPointer)it);
            XtManageChild(b);
        }
        XmString empty = xmstr("");
        n = 0;
        XtSetArg(args[n], XmNsubMenuId, pd); n++;
        XtSetArg(args[n], XmNlabelString, empty); n++;
        panel_menu_ = XmCreateOptionMenu(row, (char*)"panelChoice", args, n);
        XmStringFree(empty);
        XtManageChild(panel_menu_);

        XmString ol = xmstr("Open");
        XtSetArg(args[0], XmNlabelString, ol);
        Widget open = XmCreatePushButton(row, (char*)"open", args, 1);
        XmStringFree(ol);
        XtAddCallback(open, XmNactivateCallback, open_panel_cb,
                      (XtPointer)this);
        XtManageChild(open);
    }
    XtManageChild(row);
    XtManageChild(rc);

    XmString cl = xmstr("Close");
    n = 0;
    XtSetArg(args[n], XmNlabelString, cl); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, rc); n++;
    XtSetArg(args[n], XmNtopOffset, scaled(10)); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget close = XmCreatePushButton(dialog_, (char*)"close", args, n);
    XmStringFree(cl);
    XtAddCallback(close, XmNactivateCallback, close_cb, (XtPointer)this);
    XtManageChild(close);
    XtSetArg(args[0], XmNdefaultButton, close);
    XtSetArg(args[1], XmNcancelButton, close);
    XtSetValues(dialog_, args, 2);
}

void VolumeControl::update_widgets()
{
    loading_ = true;
    if (dialog_) {
        char t[128];
        sprintf(t, "Master volume (%.60s)", server_name_.c_str());
        XmString l = xmstr(t);
        Arg args[1];
        XtSetArg(args[0], XmNlabelString, l);
        XtSetValues(title_, args, 1);
        XmStringFree(l);
        XmScaleSetValue(scale_, state_.percent > 100 ? 100 : state_.percent);
        XmToggleButtonSetState(mute_, state_.muted, False);
    }
    if (menu_mute_)
        XmToggleButtonGadgetSetState(menu_mute_, state_.muted, False);
    loading_ = false;
}

void VolumeControl::scale_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    VolumeControl* self = (VolumeControl*)closure;
    if (self->loading_)
        return;
    XmScaleCallbackStruct* cbs = (XmScaleCallbackStruct*)call;
    if (cbs->reason == XmCR_DRAG) {
        /* Many drag steps; send at most one change per k_drag_ms. */
        self->drag_value_ = cbs->value;
        if (!self->drag_timer_)
            self->drag_timer_ = XtAppAddTimeOut(
                XtWidgetToApplicationContext(self->parent_), k_drag_ms,
                drag_cb, closure);
        return;
    }
    self->set_volume(cbs->value);
}

void VolumeControl::drag_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    VolumeControl* self = (VolumeControl*)closure;
    self->drag_timer_ = 0;
    self->set_volume(self->drag_value_);
}

void VolumeControl::mute_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    VolumeControl* self = (VolumeControl*)closure;
    if (self->loading_)
        return;
    XmToggleButtonCallbackStruct* cbs = (XmToggleButtonCallbackStruct*)call;
    self->set_mute(cbs->set ? 1 : 0);
}

void VolumeControl::dialog_item_cb(Widget w, XtPointer closure,
                                   XtPointer call)
{
    (void)w;
    (void)call;
    PanelItem* it = (PanelItem*)closure;
    it->control->dialog_panel_ = it->index;
}

void VolumeControl::open_panel_cb(Widget w, XtPointer closure,
                                  XtPointer call)
{
    (void)w;
    (void)call;
    VolumeControl* self = (VolumeControl*)closure;
    self->open_panel(self->dialog_panel_);
}

void VolumeControl::open_panel(int i)
{
    if (i >= 0 && i < panels_.size())
        spawn(panels_[i]->command.c_str());
}

void VolumeControl::close_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    XtUnmanageChild(((VolumeControl*)closure)->dialog_);
}

/* ------------------------------------------------------------------ */
/* Right click menu                                                    */

void VolumeControl::build_menu()
{
    menu_ = XmCreatePopupMenu(parent_, (char*)"volumeMenu", 0, 0);
    XtAddCallback(menu_, XmNmapCallback, menu_map_cb, (XtPointer)this);
    XtAddCallback(menu_, XmNunmapCallback, menu_unmap_cb, (XtPointer)this);

    XmString l = xmstr("Mute");
    Arg args[2];
    XtSetArg(args[0], XmNlabelString, l);
    XtSetArg(args[1], XmNvisibleWhenOff, True);
    menu_mute_ = XmCreateToggleButtonGadget(menu_, (char*)"mute", args, 2);
    XmStringFree(l);
    XtAddCallback(menu_mute_, XmNvalueChangedCallback, mute_cb,
                  (XtPointer)this);
    XtManageChild(menu_mute_);

    PanelItem* vc = new PanelItem;
    vc->control = this;
    vc->index = -1;
    items_.push_back(vc);
    l = xmstr("Volume Control...");
    XtSetArg(args[0], XmNlabelString, l);
    Widget b = XmCreatePushButtonGadget(menu_, (char*)"volume", args, 1);
    XmStringFree(l);
    XtAddCallback(b, XmNactivateCallback, panel_item_cb, (XtPointer)vc);
    XtManageChild(b);

    if (!panels_.empty())
        XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    int i;
    for (i = 0; i < panels_.size(); i++) {
        PanelItem* it = new PanelItem;
        it->control = this;
        it->index = i;
        items_.push_back(it);
        char label[300];
        sprintf(label, "Open %.280s", panels_[i]->label.c_str());
        l = xmstr(label);
        XtSetArg(args[0], XmNlabelString, l);
        b = XmCreatePushButtonGadget(menu_, (char*)"panel", args, 1);
        XmStringFree(l);
        XtAddCallback(b, XmNactivateCallback, panel_item_cb, (XtPointer)it);
        XtManageChild(b);
    }
}

void VolumeControl::post_menu(XButtonEvent* ev)
{
    if (!menu_)
        build_menu();
    update_widgets();
    post_popup_above(menu_, ev);
}

/* Run after the menu has popped down. */
void VolumeControl::panel_item_cb(Widget w, XtPointer closure,
                                  XtPointer call)
{
    (void)w;
    (void)call;
    PanelItem* it = (PanelItem*)closure;
    it->control->pending_panel_ = it->index;
    XtAppAddTimeOut(XtWidgetToApplicationContext(it->control->parent_), 0,
                    deferred_cb, (XtPointer)it->control);
}

void VolumeControl::deferred_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    VolumeControl* self = (VolumeControl*)closure;
    int i = self->pending_panel_;
    self->pending_panel_ = -2;
    if (i == -1)
        self->show_dialog();
    else
        self->open_panel(i);
}

void VolumeControl::menu_map_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((VolumeControl*)closure)->wm_->menu_shown(true);
}

void VolumeControl::menu_unmap_cb(Widget w, XtPointer closure,
                                  XtPointer call)
{
    (void)w;
    (void)call;
    ((VolumeControl*)closure)->wm_->menu_shown(false);
}

}
