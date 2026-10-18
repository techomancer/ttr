#include "shell/netstatus.h"
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
#include <Xm/SeparatoG.h>
#include <Xm/Text.h>

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

namespace ttr {

static const unsigned long k_poll_ms = 2000;

/* Theme names for each ICON_* state, with a fallback each. */
static const char* const k_icon_names[][2] = {
    { "network-offline", "network-wired-disconnected" },
    { "network-wired", "network-idle" },
    { "network-wireless-signal-none", "network-wireless" },
    { "network-wireless-signal-weak", "network-wireless" },
    { "network-wireless-signal-ok", "network-wireless" },
    { "network-wireless-signal-good", "network-wireless" },
    { "network-wireless-signal-excellent", "network-wireless" }
};

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

static double now_seconds()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec / 1e6;
}

/* 605368420 -> "605,368,420" */
static Str format_count(double v)
{
    char digits[64], out[96];
    sprintf(digits, "%.0f", v);
    int n = (int)strlen(digits), o = 0, i;
    for (i = 0; i < n; i++) {
        out[o++] = digits[i];
        int left = n - 1 - i;
        if (left > 0 && left % 3 == 0)
            out[o++] = ',';
    }
    out[o] = 0;
    return Str(out);
}

NetStatus::NetStatus(Widget parent, WmLink* wm, const Config& cfg,
                     IconLoader* icons, const ColorSet& cs,
                     void (*changed)(void*), void* closure)
    : parent_(parent), wm_(wm), cs_(cs), changed_(changed),
      closure_(closure), supported_(false), state_(ICON_OFFLINE),
      now_time_(0), prev_time_(0), timer_(0), dialog_(0), report_(0),
      dialog_tool_(0), menu_(0), pending_(-2)
{
    int i;
    for (i = 0; i < ICON_COUNT; i++) {
        icons_[i] = icons->load(k_icon_names[i][0], scaled(16));
        if (!icons_[i])
            icons_[i] = icons->load(k_icon_names[i][1], scaled(16));
    }
    find_net_tools(cfg.terminal.c_str(), &tools_);
    poll();
}

NetStatus::~NetStatus()
{
    if (timer_)
        XtRemoveTimeOut(timer_);
    net_free(&now_);
    net_free(&prev_);
    int i;
    for (i = 0; i < ICON_COUNT; i++)
        delete icons_[i];
    for (i = 0; i < tools_.size(); i++)
        delete tools_[i];
    for (i = 0; i < items_.size(); i++)
        delete items_[i];
}

/* ------------------------------------------------------------------ */
/* Polling and the icon                                                */

void NetStatus::poll()
{
    net_free(&prev_);
    int i;
    for (i = 0; i < now_.size(); i++)
        prev_.push_back(now_[i]);
    now_.clear();
    prev_time_ = now_time_;
    now_time_ = now_seconds();

    bool was = supported_;
    supported_ = net_snapshot(&now_);
    int state = icon_state();
    if (state != state_ || was != supported_) {
        state_ = state;
        if (changed_)
            changed_(closure_);
    }
    if (dialog_ && XtIsManaged(dialog_))
        update_report();
    timer_ = XtAppAddTimeOut(XtWidgetToApplicationContext(parent_), k_poll_ms,
                             poll_cb, (XtPointer)this);
}

void NetStatus::poll_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    NetStatus* self = (NetStatus*)closure;
    self->timer_ = 0;
    self->poll();
}

/* The default route's interface decides the icon. */
int NetStatus::icon_state() const
{
    if (now_.empty())
        return ICON_OFFLINE;
    const NetIface* n = now_[0];
    if (!n->up || !n->carrier || (n->ipv4.empty() && n->ipv6.empty()))
        return ICON_OFFLINE;
    if (!n->wireless)
        return ICON_WIRED;
    int q = n->quality;
    if (q < 0)
        return ICON_WIFI_2;
    return q >= 80 ? ICON_WIFI_4 : q >= 55 ? ICON_WIFI_3 : q >= 30
           ? ICON_WIFI_2 : q >= 5 ? ICON_WIFI_1 : ICON_WIFI_0;
}

void NetStatus::paint(Painter& p, const Rect& r)
{
    int size = r.w < r.h ? r.w : r.h;
    int x = r.x + (r.w - size) / 2, y = r.y + (r.h - size) / 2;
    if (icons_[state_]) {
        p.icon(*icons_[state_], x, y, size, cs_.bg);
        return;
    }
    int kind = state_ == ICON_OFFLINE ? 0 : state_ == ICON_WIRED ? 1 : 2;
    draw_network_icon(p, x, y, size, kind, state_ - ICON_WIFI_0, cs_);
}

void NetStatus::button(XButtonEvent* ev)
{
    /* On release: a Motif menu posted while the button is still down
     * closes again when it comes up. */
    if (ev->type != ButtonRelease)
        return;
    if (ev->button == Button1)
        show_dialog();
    else if (ev->button == Button3)
        post_menu(ev);
}

/* ------------------------------------------------------------------ */
/* Status dialog                                                       */

void NetStatus::show_dialog()
{
    if (!dialog_)
        build_dialog();
    update_report();
    XtManageChild(dialog_);
}

void NetStatus::build_dialog()
{
    XmString title = xmstr("Network");
    Arg args[12];
    int n = 0;
    XtSetArg(args[n], XmNdialogTitle, title); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNmarginWidth, scaled(10)); n++;
    XtSetArg(args[n], XmNmarginHeight, scaled(10)); n++;
    dialog_ = XmCreateFormDialog(parent_, (char*)"networkDialog", args, n);
    XmStringFree(title);

    XmString cl = xmstr("Close");
    n = 0;
    XtSetArg(args[n], XmNlabelString, cl); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget close = XmCreatePushButton(dialog_, (char*)"close", args, n);
    XmStringFree(cl);
    XtAddCallback(close, XmNactivateCallback, close_cb, (XtPointer)this);
    XtManageChild(close);

    /* Settings tool: [choice] [Open], left of Close. */
    n = 0;
    XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNbottomWidget, close); n++;
    XtSetArg(args[n], XmNbottomOffset, scaled(6)); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    Widget row = XmCreateRowColumn(dialog_, (char*)"toolRow", args, n);
    if (tools_.empty()) {
        XmString l = xmstr("No network settings tool found.");
        XtSetArg(args[0], XmNlabelString, l);
        XtManageChild(XmCreateLabel(row, (char*)"none", args, 1));
        XmStringFree(l);
    } else {
        XmString l = xmstr("Network settings:");
        XtSetArg(args[0], XmNlabelString, l);
        XtManageChild(XmCreateLabel(row, (char*)"label", args, 1));
        XmStringFree(l);
        Widget pd = XmCreatePulldownMenu(row, (char*)"tools", 0, 0);
        int i;
        for (i = 0; i < tools_.size(); i++) {
            XmString tl = xmstr(tools_[i]->label.c_str());
            XtSetArg(args[0], XmNlabelString, tl);
            Widget b = XmCreatePushButtonGadget(pd, (char*)"tool", args, 1);
            XmStringFree(tl);
            ToolItem* it = new ToolItem;
            it->status = this;
            it->index = i;
            items_.push_back(it);
            XtAddCallback(b, XmNactivateCallback, choice_cb, (XtPointer)it);
            XtManageChild(b);
        }
        XmString empty = xmstr("");
        n = 0;
        XtSetArg(args[n], XmNsubMenuId, pd); n++;
        XtSetArg(args[n], XmNlabelString, empty); n++;
        XtManageChild(XmCreateOptionMenu(row, (char*)"toolChoice", args, n));
        XmStringFree(empty);
        XmString ol = xmstr("Open");
        XtSetArg(args[0], XmNlabelString, ol);
        Widget open = XmCreatePushButton(row, (char*)"open", args, 1);
        XmStringFree(ol);
        XtAddCallback(open, XmNactivateCallback, open_cb, (XtPointer)this);
        XtManageChild(open);
    }
    XtManageChild(row);

    /* The report, in a fixed width font at the UI scale, above. */
    n = 0;
    XtSetArg(args[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
    XtSetArg(args[n], XmNeditable, False); n++;
    XtSetArg(args[n], XmNcursorPositionVisible, False); n++;
    XtSetArg(args[n], XmNrows, 18); n++;
    XtSetArg(args[n], XmNcolumns, 66); n++;
    XtSetArg(args[n], XmNtraversalOn, False); n++;
    Str font = scale_font(XtDisplay(parent_), "fixed", ui_scale());
    XmFontListEntry fe = XmFontListEntryLoad(XtDisplay(parent_),
                                             (char*)font.c_str(),
                                             XmFONT_IS_FONT,
                                             (char*)XmFONTLIST_DEFAULT_TAG);
    XmFontList fl = fe ? XmFontListAppendEntry(0, fe) : 0;
    if (fe)
        XmFontListEntryFree(&fe);
    if (fl) {
        XtSetArg(args[n], XmNfontList, fl); n++;
    }
    report_ = XmCreateScrolledText(dialog_, (char*)"report", args, n);
    if (fl)
        XmFontListFree(fl);
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNbottomWidget, row); n++;
    XtSetArg(args[n], XmNbottomOffset, scaled(6)); n++;
    XtSetValues(XtParent(report_), args, n);
    XtManageChild(report_);

    XtSetArg(args[0], XmNdefaultButton, close);
    XtSetArg(args[1], XmNcancelButton, close);
    XtSetValues(dialog_, args, 2);
}

/* Rate between the last two snapshots, e.g. "12.3 KB/s". */
static Str rate(double now, double before, double seconds)
{
    if (seconds <= 0 || before < 0 || now < before)
        return Str("");
    Str s = format_bytes((now - before) / seconds);
    char buf[48];
    sprintf(buf, "%s/s", s.c_str());
    return Str(buf);
}

void NetStatus::update_report()
{
    if (!report_)
        return;
    char* buf = new char[8192];
    int len = 0;
    buf[0] = 0;
    if (!supported_) {
        len += sprintf(buf + len, "Network status is not available on this "
                       "system yet.\n");
    } else if (now_.empty()) {
        len += sprintf(buf + len, "No network interfaces.\n");
    }
    double dt = now_time_ - prev_time_;
    int i;
    for (i = 0; i < now_.size() && len < 7000; i++) {
        const NetIface* n = now_[i];
        const NetIface* p = 0;
        int j;
        for (j = 0; j < prev_.size(); j++)
            if (strcmp(prev_[j]->name.c_str(), n->name.c_str()) == 0)
                p = prev_[j];

        len += sprintf(buf + len, "%s%.32s  (%s%s)\n", i ? "\n" : "",
                       n->name.c_str(), n->wireless ? "wireless" : "wired",
                       n->is_default ? ", default route" : "");
        len += sprintf(buf + len, "  State:    %s%s\n",
                       n->up ? "up" : "down",
                       n->up ? (n->carrier ? (n->wireless ? ", associated"
                                                          : ", link")
                                           : (n->wireless ? ", not associated"
                                                          : ", no cable"))
                             : "");
        if (!n->ipv4.empty())
            len += sprintf(buf + len, "  IPv4:     %.40s\n", n->ipv4.c_str());
        if (!n->ipv6.empty())
            len += sprintf(buf + len, "  IPv6:     %.60s\n", n->ipv6.c_str());
        if (!n->mac.empty())
            len += sprintf(buf + len, "  MAC:      %.40s\n", n->mac.c_str());
        if (n->wireless && !n->ssid.empty())
            len += sprintf(buf + len, "  Network:  %.64s\n", n->ssid.c_str());
        if (n->wireless && !n->bssid.empty())
            len += sprintf(buf + len, "  BSSID:    %.40s\n", n->bssid.c_str());
        if (n->wireless && n->quality >= 0)
            len += sprintf(buf + len, "  Signal:   %d%%\n", n->quality);

        Str rxp = format_count(n->rx_packets), txp = format_count(n->tx_packets);
        Str rxb = format_bytes(n->rx_bytes), txb = format_bytes(n->tx_bytes);
        Str rxr = p ? rate(n->rx_bytes, p->rx_bytes, dt) : Str("");
        Str txr = p ? rate(n->tx_bytes, p->tx_bytes, dt) : Str("");
        len += sprintf(buf + len, "  Received: %15s packets  %10s  %s\n",
                       rxp.c_str(), rxb.c_str(), rxr.c_str());
        len += sprintf(buf + len, "  Sent:     %15s packets  %10s  %s\n",
                       txp.c_str(), txb.c_str(), txr.c_str());
    }

    XmTextPosition top = XmTextGetTopCharacter(report_);
    XmTextSetString(report_, buf);
    XmTextSetTopCharacter(report_, top);
    delete [] buf;
}

void NetStatus::choice_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ToolItem* it = (ToolItem*)closure;
    it->status->dialog_tool_ = it->index;
}

void NetStatus::open_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    NetStatus* self = (NetStatus*)closure;
    self->open_tool(self->dialog_tool_);
}

void NetStatus::open_tool(int i)
{
    if (i >= 0 && i < tools_.size())
        spawn(tools_[i]->command.c_str());
}

void NetStatus::close_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    XtUnmanageChild(((NetStatus*)closure)->dialog_);
}

/* ------------------------------------------------------------------ */
/* Right click menu                                                    */

void NetStatus::post_menu(XButtonEvent* ev)
{
    if (!menu_) {
        menu_ = XmCreatePopupMenu(parent_, (char*)"networkMenu", 0, 0);
        XtAddCallback(menu_, XmNmapCallback, menu_map_cb, (XtPointer)this);
        XtAddCallback(menu_, XmNunmapCallback, menu_unmap_cb,
                      (XtPointer)this);
        ToolItem* st = new ToolItem;
        st->status = this;
        st->index = -1;
        items_.push_back(st);
        XmString l = xmstr("Network Status...");
        Arg args[1];
        XtSetArg(args[0], XmNlabelString, l);
        Widget b = XmCreatePushButtonGadget(menu_, (char*)"status", args, 1);
        XmStringFree(l);
        XtAddCallback(b, XmNactivateCallback, item_cb, (XtPointer)st);
        XtManageChild(b);
        if (!tools_.empty())
            XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
        int i;
        for (i = 0; i < tools_.size(); i++) {
            ToolItem* it = new ToolItem;
            it->status = this;
            it->index = i;
            items_.push_back(it);
            char label[300];
            sprintf(label, "Open %.280s", tools_[i]->label.c_str());
            l = xmstr(label);
            XtSetArg(args[0], XmNlabelString, l);
            b = XmCreatePushButtonGadget(menu_, (char*)"tool", args, 1);
            XmStringFree(l);
            XtAddCallback(b, XmNactivateCallback, item_cb, (XtPointer)it);
            XtManageChild(b);
        }
    }
    post_popup_above(menu_, ev);
}

/* Run after the menu has popped down. */
void NetStatus::item_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ToolItem* it = (ToolItem*)closure;
    it->status->pending_ = it->index;
    XtAppAddTimeOut(XtWidgetToApplicationContext(it->status->parent_), 0,
                    deferred_cb, (XtPointer)it->status);
}

void NetStatus::deferred_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    NetStatus* self = (NetStatus*)closure;
    int i = self->pending_;
    self->pending_ = -2;
    if (i == -1)
        self->show_dialog();
    else
        self->open_tool(i);
}

void NetStatus::menu_map_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((NetStatus*)closure)->wm_->menu_shown(true);
}

void NetStatus::menu_unmap_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((NetStatus*)closure)->wm_->menu_shown(false);
}

}
