#include "shell/startuppage.h"
#include "shell/pickers.h"
#include "shell/settings.h"
#include "core/apps.h"
#include "core/launch.h"
#include "core/scale.h"
#include "x11/config.h"

#include <Xm/Xm.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace ttr {

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

static Str text_value(Widget t)
{
    char* s = XmTextFieldGetString(t);
    Str r(s ? s : "");
    XtFree(s);
    return r;
}

/* 'path' with the home directory shown as ~. */
static Str tilde_path(const char* path)
{
    const char* home = getenv("HOME");
    int n = home ? (int)strlen(home) : 0;
    if (n > 1 && strncmp(path, home, n) == 0 && path[n] == '/') {
        char buf[1100];
        sprintf(buf, "~%.1000s", path + n);
        return Str(buf);
    }
    return Str(path);
}

Widget StartupPage::push(Widget parent, const char* label, XtCallbackProc cb)
{
    XmString l = xmstr(label);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    Widget b = XmCreatePushButton(parent, (char*)"button", args, 1);
    XmStringFree(l);
    XtAddCallback(b, XmNactivateCallback, cb, (XtPointer)this);
    XtManageChild(b);
    return b;
}

StartupPage::StartupPage(SettingsDialog* dialog, Widget page,
                         const Config& cfg, const AppFolder* apps)
    : top_(dialog->widget()), cfg_(cfg), current_(-1), loading_(false)
{
    programs_ = new ProgramPicker(apps, program_chosen, this);
    browser_ = new FileBrowser(browsed, this);
    Str path = StartupList::default_path();
    model_.load(path.c_str());

    Widget f = dialog->frame(page, "Programs started with ttr");
    /* The list asks for the width of its longest command; a fixed-size
     * form around it keeps long commands from widening the dialog. */
    Arg args[8];
    int n = 0;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    Widget box = XmCreateForm(f, (char*)"listBox", args, n);
    n = 0;
    XtSetArg(args[n], XmNvisibleItemCount, 6); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    XtSetArg(args[n], XmNlistSizePolicy, XmCONSTANT); n++;
    list_ = XmCreateScrolledList(box, (char*)"startup", args, n);
    XtAddCallback(list_, XmNbrowseSelectionCallback, list_cb,
                  (XtPointer)this);
    XtManageChild(list_);
    XtWidgetGeometry pref;
    XtQueryGeometry(XtParent(list_), 0, &pref);
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetValues(XtParent(list_), args, n);
    n = 0;
    XtSetArg(args[n], XmNwidth, scaled(300)); n++;
    XtSetArg(args[n], XmNheight, pref.height); n++;
    XtSetValues(box, args, n);
    XtManageChild(box);

    n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNmarginWidth, 0); n++;
    XtSetArg(args[n], XmNmarginHeight, 0); n++;
    fields_ = XmCreateRowColumn(f, (char*)"fields", args, n);
    XtManageChild(fields_);
    Widget r = dialog->row(fields_, "Command:");
    command_ = dialog->text_field(r, "", 36);
    XtAddCallback(command_, XmNvalueChangedCallback, field_cb,
                  (XtPointer)this);
    push(r, "Browse...", browse_cb);
    r = dialog->row(fields_, "");
    XmString l = xmstr("Start this program");
    XtSetArg(args[0], XmNlabelString, l);
    enabled_ = XmCreateToggleButton(r, (char*)"enabled", args, 1);
    XmStringFree(l);
    XtAddCallback(enabled_, XmNvalueChangedCallback, field_cb,
                  (XtPointer)this);
    XtManageChild(enabled_);

    n = 0;
    XtSetArg(args[n], XmNorientation, XmHORIZONTAL); n++;
    XtSetArg(args[n], XmNmarginWidth, 0); n++;
    Widget buttons = XmCreateRowColumn(f, (char*)"buttons", args, n);
    XtManageChild(buttons);
    push(buttons, "Add", add_cb);
    push(buttons, "Add from Programs...", add_program_cb);
    remove_b_ = push(buttons, "Remove", remove_cb);
    up_b_ = push(buttons, "Move Up", up_cb);
    down_b_ = push(buttons, "Move Down", down_cb);

    f = dialog->frame(page, "Startup script");
    Str sh = tilde_path(autostart_script_path().c_str());
    r = dialog->row(f, "Script:");
    Widget shpath = dialog->text_field(r, sh.c_str(), 36);
    XtSetArg(args[0], XmNeditable, False);
    XtSetArg(args[1], XmNcursorPositionVisible, False);
    XtSetValues(shpath, args, 2);
    r = dialog->row(f, "");
    l = xmstr("Run it after the programs above");
    XtSetArg(args[0], XmNlabelString, l);
    script_ = XmCreateToggleButton(r, (char*)"script", args, 1);
    XmStringFree(l);
    XmToggleButtonSetState(script_, cfg.autostart_script, False);
    XtManageChild(script_);
    push(r, "Edit Script...", edit_script_cb);

    l = xmstr("Programs and the script start when ttr starts, "
              "not when it restarts.");
    XtSetArg(args[0], XmNlabelString, l);
    XtManageChild(XmCreateLabel(page, (char*)"note", args, 1));
    XmStringFree(l);

    refresh_list();
    select(model_.size() > 0 ? 0 : -1);
}

StartupPage::~StartupPage()
{
    delete browser_;
    delete programs_;
}

bool StartupPage::run_script() const
{
    return XmToggleButtonGetState(script_);
}

/* ------------------------------------------------------------------ */
/* The list                                                            */

Str StartupPage::label_for(const StartupItem* s)
{
    char buf[512];
    const char* c = s->command.empty() ? "(no command)" : s->command.c_str();
    sprintf(buf, "%s%.400s", s->enabled ? "" : "(off)  ", c);
    return Str(buf);
}

void StartupPage::refresh_list()
{
    XmListDeleteAllItems(list_);
    int i;
    for (i = 0; i < model_.size(); i++) {
        Str l = label_for(model_.at(i));
        XmString s = xmstr(l.c_str());
        XmListAddItemUnselected(list_, s, 0);
        XmStringFree(s);
    }
}

void StartupPage::select(int i)
{
    if (current_ >= 0)
        store_fields();
    current_ = i;
    if (i >= 0) {
        XmListSelectPos(list_, i + 1, False);
        XmListSetBottomPos(list_, i + 1);
    }
    load_fields();
    update_buttons();
}

void StartupPage::load_fields()
{
    loading_ = true;
    const StartupItem* s = current_ >= 0 ? model_.at(current_) : 0;
    XmTextFieldSetString(command_, (char*)(s ? s->command.c_str() : ""));
    XmToggleButtonSetState(enabled_, s ? s->enabled : False, False);
    XtSetSensitive(fields_, s != 0);
    loading_ = false;
}

void StartupPage::store_fields()
{
    if (current_ < 0 || current_ >= model_.size())
        return;
    StartupItem* s = model_.at(current_);
    s->command = text_value(command_);
    s->enabled = XmToggleButtonGetState(enabled_);
    Str l = label_for(s);
    XmString xs = xmstr(l.c_str());
    XmListReplaceItemsPos(list_, &xs, 1, current_ + 1);
    XmStringFree(xs);
    XmListSelectPos(list_, current_ + 1, False);
}

void StartupPage::update_buttons()
{
    XtSetSensitive(remove_b_, current_ >= 0);
    XtSetSensitive(up_b_, current_ > 0);
    XtSetSensitive(down_b_, current_ >= 0 && current_ < model_.size() - 1);
}

void StartupPage::list_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    StartupPage* self = (StartupPage*)closure;
    XmListCallbackStruct* cbs = (XmListCallbackStruct*)call;
    int i = cbs->item_position - 1;
    if (i != self->current_)
        self->select(i);
}

void StartupPage::field_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    StartupPage* self = (StartupPage*)closure;
    if (!self->loading_)
        self->store_fields();
}

/* ------------------------------------------------------------------ */
/* Adding, removing, ordering                                          */

void StartupPage::add(const char* command)
{
    store_fields();
    StartupItem* s = new StartupItem;
    s->command = command;
    model_.add(s);
    current_ = -1;
    refresh_list();
    select(model_.size() - 1);
    XmProcessTraversal(command_, XmTRAVERSE_CURRENT);
}

void StartupPage::add_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StartupPage*)closure)->add("");
}

void StartupPage::add_program_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    StartupPage* self = (StartupPage*)closure;
    self->programs_->show(self->top_);
}

void StartupPage::program_chosen(void* closure, const AppNode* app)
{
    ((StartupPage*)closure)->add(app->exec.c_str());
}

void StartupPage::remove_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    StartupPage* self = (StartupPage*)closure;
    int i = self->current_;
    if (i < 0)
        return;
    self->current_ = -1;        /* nothing to store */
    self->model_.remove(i);
    self->refresh_list();
    if (i >= self->model_.size())
        i = self->model_.size() - 1;
    self->select(i);
}

void StartupPage::up_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    StartupPage* self = (StartupPage*)closure;
    int i = self->current_;
    if (i <= 0)
        return;
    self->store_fields();
    self->model_.swap(i, i - 1);
    self->current_ = -1;
    self->refresh_list();
    self->select(i - 1);
}

void StartupPage::down_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    StartupPage* self = (StartupPage*)closure;
    int i = self->current_;
    if (i < 0 || i >= self->model_.size() - 1)
        return;
    self->store_fields();
    self->model_.swap(i, i + 1);
    self->current_ = -1;
    self->refresh_list();
    self->select(i + 1);
}

void StartupPage::browse_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    StartupPage* self = (StartupPage*)closure;
    self->browser_->show(self->top_, "Choose a Program", "/usr/bin");
}

void StartupPage::browsed(void* closure, const char* path)
{
    StartupPage* self = (StartupPage*)closure;
    if (self->current_ >= 0)
        XmTextFieldSetString(self->command_, (char*)path);
}

/* ------------------------------------------------------------------ */
/* The script                                                          */

/* Open autostart.sh (made from an example if missing) in $VISUAL or
 * $EDITOR, in the terminal. */
void StartupPage::edit_script()
{
    Str path = autostart_script_path();
    if (!create_autostart_script(path.c_str())) {
        char msg[1200];
        sprintf(msg, "Could not create %.1000s", path.c_str());
        XmString m = xmstr(msg);
        Arg args[1];
        XtSetArg(args[0], XmNmessageString, m);
        Widget err = XmCreateErrorDialog(top_, (char*)"error", args, 1);
        XmStringFree(m);
        XtUnmanageChild(XtNameToWidget(err, "Help"));
        XtUnmanageChild(XtNameToWidget(err, "Cancel"));
        XtManageChild(err);
        return;
    }
    Str quoted = shell_quote(path.c_str());
    int len = cfg_.terminal.length() + quoted.length() + 40;
    char* cmd = new char[len];
    sprintf(cmd, "%s ${VISUAL:-${EDITOR:-vi}} %s", cfg_.terminal.c_str(),
            quoted.c_str());
    spawn(cmd);
    delete [] cmd;
}

void StartupPage::edit_script_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StartupPage*)closure)->edit_script();
}

bool StartupPage::save(Str* error)
{
    store_fields();
    /* Entries without a command would be dropped on loading anyway. */
    int i;
    for (i = 0; i < model_.size(); ) {
        if (model_.at(i)->command.empty())
            model_.remove(i);
        else
            i++;
    }
    Str path = StartupList::default_path();
    if (model_.size() == 0 && access(path.c_str(), F_OK) != 0)
        return true;    /* nothing to keep: no file to make */
    if (model_.save(path.c_str()))
        return true;
    char msg[1200];
    sprintf(msg, "Could not write %.1000s", path.c_str());
    *error = msg;
    return false;
}

/* ------------------------------------------------------------------ */
/* XDG autostart                                                       */

static Widget read_only(Widget t)
{
    Arg args[2];
    XtSetArg(args[0], XmNeditable, False);
    XtSetArg(args[1], XmNcursorPositionVisible, False);
    XtSetValues(t, args, 2);
    return t;
}

static Widget label(Widget parent, const char* text)
{
    XmString l = xmstr(text);
    Arg args[2];
    XtSetArg(args[0], XmNlabelString, l);
    XtSetArg(args[1], XmNalignment, XmALIGNMENT_BEGINNING);
    Widget w = XmCreateLabel(parent, (char*)"note", args, 2);
    XmStringFree(l);
    XtManageChild(w);
    return w;
}

/* "~/.config/autostart, /etc/xdg/autostart" */
static Str autostart_dirs_text()
{
    Str user = tilde_path(xdg_autostart_user_dir().c_str());
    char buf[1400];
    sprintf(buf, "%.600s", user.c_str());
    const char* dirs = getenv("XDG_CONFIG_DIRS");
    if (!dirs || !*dirs)
        dirs = "/etc/xdg";
    const char* p = dirs;
    while (*p && strlen(buf) < 1000) {
        const char* e = strchr(p, ':');
        int n = e ? (int)(e - p) : (int)strlen(p);
        if (n > 0 && n < 300)
            sprintf(buf + strlen(buf), ", %.*s/autostart", n, p);
        if (!e)
            break;
        p = e + 1;
    }
    return Str(buf);
}

XdgAutostartPage::XdgAutostartPage(SettingsDialog* dialog, Widget page,
                                   const Config& cfg)
    : current_(-1)
{
    xdg_autostart_scan(&entries_);
    int i;
    for (i = 0; i < entries_.size(); i++)
        wanted_.push_back(entries_[i]->status == XdgAutostartEntry::START);

    Widget f = dialog->frame(page, "Desktop autostart (XDG)");
    Arg args[8];
    XmString l = xmstr("Start the programs that ask for it");
    XtSetArg(args[0], XmNlabelString, l);
    enabled_ = XmCreateToggleButton(f, (char*)"xdgAutostart", args, 1);
    XmStringFree(l);
    XmToggleButtonSetState(enabled_, cfg.xdg_autostart, False);
    XtManageChild(enabled_);

    /* As on the Startup page: a fixed-size form keeps long names from
     * widening the dialog. */
    int n = 0;
    XtSetArg(args[n], XmNresizePolicy, XmRESIZE_NONE); n++;
    Widget box = XmCreateForm(f, (char*)"listBox", args, n);
    n = 0;
    XtSetArg(args[n], XmNvisibleItemCount, 8); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    XtSetArg(args[n], XmNlistSizePolicy, XmCONSTANT); n++;
    list_ = XmCreateScrolledList(box, (char*)"entries", args, n);
    XtAddCallback(list_, XmNbrowseSelectionCallback, list_cb,
                  (XtPointer)this);
    XtManageChild(list_);
    XtWidgetGeometry pref;
    XtQueryGeometry(XtParent(list_), 0, &pref);
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNbottomAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    XtSetValues(XtParent(list_), args, n);
    n = 0;
    XtSetArg(args[n], XmNwidth, scaled(300)); n++;
    XtSetArg(args[n], XmNheight, pref.height); n++;
    XtSetValues(box, args, n);
    XtManageChild(box);

    command_ = read_only(dialog->text_field(dialog->row(f, "Command:"), "",
                                            40));
    status_ = read_only(dialog->text_field(dialog->row(f, "Status:"), "",
                                           40));
    Widget r = dialog->row(f, "");
    l = xmstr("Start this program");
    XtSetArg(args[0], XmNlabelString, l);
    start_ = XmCreateToggleButton(r, (char*)"start", args, 1);
    Str dirs = autostart_dirs_text();
    read_only(dialog->text_field(dialog->row(f, "Folders:"), dirs.c_str(),
                                 40));
    XmStringFree(l);
    XtAddCallback(start_, XmNvalueChangedCallback, start_cb, (XtPointer)this);
    XtManageChild(start_);

    label(page, "Turning a program off saves a copy of it with Hidden=true "
                "in your own folder.");

    for (i = 0; i < entries_.size(); i++) {
        Str t = label_for(i);
        XmString s = xmstr(t.c_str());
        XmListAddItemUnselected(list_, s, 0);
        XmStringFree(s);
    }
    select(entries_.size() > 0 ? 0 : -1);
}

XdgAutostartPage::~XdgAutostartPage()
{
    xdg_autostart_free(&entries_);
}

bool XdgAutostartPage::enabled() const
{
    return XmToggleButtonGetState(enabled_);
}

static bool switchable(const XdgAutostartEntry* e)
{
    return e->status == XdgAutostartEntry::START ||
           e->status == XdgAutostartEntry::OFF;
}

Str XdgAutostartPage::label_for(int i)
{
    const XdgAutostartEntry* e = entries_[i];
    const char* mark = !switchable(e) ? "(skipped)  "
                       : wanted_[i] ? "" : "(off)  ";
    char buf[600];
    sprintf(buf, "%s%.400s", mark, e->name.c_str());
    return Str(buf);
}

void XdgAutostartPage::select(int i)
{
    current_ = i;
    const XdgAutostartEntry* e = i >= 0 ? entries_[i] : 0;
    if (e) {
        XmListSelectPos(list_, i + 1, False);
        XmListSetBottomPos(list_, i + 1);
    }
    XmTextFieldSetString(command_, (char*)(e ? e->command.c_str() : ""));
    const char* status = "";
    if (e)
        status = !switchable(e) ? e->detail.c_str()
                 : wanted_[i] ? "Starts with ttr" : "Turned off";
    XmTextFieldSetString(status_, (char*)status);
    XmToggleButtonSetState(start_, e && wanted_[i], False);
    XtSetSensitive(start_, e && switchable(e));
}

void XdgAutostartPage::list_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    XdgAutostartPage* self = (XdgAutostartPage*)closure;
    XmListCallbackStruct* cbs = (XmListCallbackStruct*)call;
    self->select(cbs->item_position - 1);
}

void XdgAutostartPage::start_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)call;
    XdgAutostartPage* self = (XdgAutostartPage*)closure;
    int i = self->current_;
    if (i < 0)
        return;
    self->wanted_[i] = XmToggleButtonGetState(w) ? 1 : 0;
    Str t = self->label_for(i);
    XmString s = xmstr(t.c_str());
    XmListReplaceItemsPos(self->list_, &s, 1, i + 1);
    XmStringFree(s);
    XmListSelectPos(self->list_, i + 1, False);
    XmTextFieldSetString(self->status_, (char*)(self->wanted_[i]
                                                    ? "Starts with ttr"
                                                    : "Turned off"));
}

bool XdgAutostartPage::save(Str* error)
{
    int i;
    for (i = 0; i < entries_.size(); i++) {
        const XdgAutostartEntry* e = entries_[i];
        if (!switchable(e))
            continue;
        bool was = e->status == XdgAutostartEntry::START;
        if ((wanted_[i] != 0) == was)
            continue;
        if (!xdg_autostart_set_enabled(*e, wanted_[i] != 0)) {
            char msg[1200];
            sprintf(msg, "Could not write %.300s to %.800s", e->id.c_str(),
                    xdg_autostart_user_dir().c_str());
            *error = msg;
            return false;
        }
    }
    return true;
}

}
