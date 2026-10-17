#include "shell/shortcutsdlg.h"
#include "core/apps.h"
#include "core/icon.h"
#include "core/painter.h"
#include "x11/config.h"
#include "x11/iconload.h"
#include "x11/keys.h"
#include "x11/xpainter.h"

#include <Xm/Xm.h>
#include <Xm/FileSB.h>
#include <Xm/Form.h>
#include <Xm/Frame.h>
#include <Xm/Label.h>
#include <Xm/List.h>
#include <Xm/MessageB.h>
#include <Xm/PushB.h>
#include <Xm/RowColumn.h>
#include <Xm/SelectioB.h>
#include <Xm/Separator.h>
#include <Xm/TextF.h>
#include <Xm/ToggleB.h>

#include <stdio.h>
#include <string.h>

namespace ttr {

static const int k_label_width = 90;
static const int k_preview = 32;

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

static Str xmstring_text(XmString s)
{
    char* t = (char*)XmStringUnparse(s, 0, XmCHARSET_TEXT, XmCHARSET_TEXT, 0,
                                     0, XmOUTPUT_ALL);
    Str r(t ? t : "");
    XtFree(t);
    return r;
}

static Rgb pixel_rgb(Display* dpy, Pixel p)
{
    XColor c;
    c.pixel = p;
    XQueryColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &c);
    return Rgb(c.red, c.green, c.blue);
}

ShortcutsDialog::ShortcutsDialog(Widget parent, WmLink* wm,
                                 const AppFolder* apps)
    : parent_(parent), wm_(wm), apps_(apps), current_(-1), loading_(false),
      dialog_(0), preview_pm_(None), programs_(0), browser_(0),
      browse_target_(0)
{
    Display* dpy = XtDisplay(parent);
    painter_ = new XPainter(dpy, DefaultScreen(dpy));
    icons_ = new IconLoader(dpy);
}

ShortcutsDialog::~ShortcutsDialog()
{
    if (preview_pm_ != None)
        XFreePixmap(XtDisplay(parent_), preview_pm_);
    delete icons_;
    delete painter_;
}

void ShortcutsDialog::show()
{
    /* Rebuilt every time from the file, so Cancel really cancels. */
    if (dialog_)
        XtDestroyWidget(XtParent(dialog_));
    dialog_ = 0;
    programs_ = 0;
    browser_ = 0;
    Str path = ShortcutList::default_path();
    model_.load(path.c_str());
    current_ = -1;
    build();
    refresh_list();
    select(model_.size() > 0 ? 0 : -1);
    XtManageChild(dialog_);
}

/* ------------------------------------------------------------------ */
/* Building                                                            */

Widget ShortcutsDialog::field_row(Widget parent, const char* label,
                                  Widget* text, int columns)
{
    Arg args[4];
    XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
    XtSetArg(args[1], XmNmarginWidth, 0);
    XtSetArg(args[2], XmNmarginHeight, 0);
    Widget rc = XmCreateRowColumn(parent, (char*)"row", args, 3);

    XmString l = xmstr(label);
    int n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    XtSetArg(args[n], XmNwidth, k_label_width); n++;
    XtSetArg(args[n], XmNrecomputeSize, False); n++;
    XtSetArg(args[n], XmNalignment, XmALIGNMENT_BEGINNING); n++;
    XtManageChild(XmCreateLabel(rc, (char*)"label", args, n));
    XmStringFree(l);

    if (text) {
        XtSetArg(args[0], XmNcolumns, columns);
        *text = XmCreateTextField(rc, (char*)"text", args, 1);
        XtAddCallback(*text, XmNvalueChangedCallback, field_cb,
                      (XtPointer)this);
        XtManageChild(*text);
    }
    XtManageChild(rc);
    return rc;
}

static Widget push(Widget parent, const char* label, XtCallbackProc cb,
                   XtPointer closure)
{
    XmString l = xmstr(label);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    Widget b = XmCreatePushButton(parent, (char*)"button", args, 1);
    XmStringFree(l);
    XtAddCallback(b, XmNactivateCallback, cb, closure);
    XtManageChild(b);
    return b;
}

void ShortcutsDialog::build()
{
    XmString title = xmstr("Shortcuts");
    Arg args[12];
    int n = 0;
    XtSetArg(args[n], XmNdialogTitle, title); n++;
    XtSetArg(args[n], XmNautoUnmanage, False); n++;
    XtSetArg(args[n], XmNmarginWidth, 10); n++;
    XtSetArg(args[n], XmNmarginHeight, 10); n++;
    dialog_ = XmCreateFormDialog(parent_, (char*)"shortcutsDialog", args, n);
    XmStringFree(title);

    Pixel bg = 0, fg = 0;
    XtSetArg(args[0], XmNbackground, &bg);
    XtSetArg(args[1], XmNforeground, &fg);
    XtGetValues(dialog_, args, 2);
    cs_ = motif_colors(pixel_rgb(XtDisplay(parent_), bg));
    cs_.fg = pixel_rgb(XtDisplay(parent_), fg);

    /* Buttons on the right. */
    n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNpacking, XmPACK_COLUMN); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget buttons = XmCreateRowColumn(dialog_, (char*)"buttons", args, n);
    push(buttons, "Add from Programs...", add_program_cb, (XtPointer)this);
    push(buttons, "Add", add_cb, (XtPointer)this);
    remove_b_ = push(buttons, "Remove", remove_cb, (XtPointer)this);
    up_b_ = push(buttons, "Move Up", up_cb, (XtPointer)this);
    down_b_ = push(buttons, "Move Down", down_cb, (XtPointer)this);
    XtManageChild(buttons);

    /* The list, left of the buttons. */
    n = 0;
    XtSetArg(args[n], XmNvisibleItemCount, 10); n++;
    XtSetArg(args[n], XmNselectionPolicy, XmBROWSE_SELECT); n++;
    XtSetArg(args[n], XmNscrollBarDisplayPolicy, XmSTATIC); n++;
    list_ = XmCreateScrolledList(dialog_, (char*)"list", args, n);
    XtAddCallback(list_, XmNbrowseSelectionCallback, list_cb,
                  (XtPointer)this);
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNrightWidget, buttons); n++;
    XtSetArg(args[n], XmNrightOffset, 8); n++;
    XtSetValues(XtParent(list_), args, n);
    XtManageChild(list_);

    /* The selected shortcut's fields. */
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, XtParent(list_)); n++;
    XtSetArg(args[n], XmNtopOffset, 10); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget frame = XmCreateFrame(dialog_, (char*)"frame", args, n);
    XmString ft = xmstr("Shortcut");
    XtSetArg(args[0], XmNlabelString, ft);
    XtSetArg(args[1], XmNchildType, XmFRAME_TITLE_CHILD);
    XtManageChild(XmCreateLabel(frame, (char*)"title", args, 2));
    XmStringFree(ft);

    n = 0;
    XtSetArg(args[n], XmNorientation, XmVERTICAL); n++;
    XtSetArg(args[n], XmNmarginWidth, 8); n++;
    XtSetArg(args[n], XmNmarginHeight, 6); n++;
    fields_ = XmCreateRowColumn(frame, (char*)"fields", args, n);

    field_row(fields_, "Name:", &name_, 30);
    Widget r = field_row(fields_, "Command:", &command_, 36);
    push(r, "Browse...", browse_command_cb, (XtPointer)this);
    r = field_row(fields_, "Icon:", &icon_, 24);
    XtAddCallback(icon_, XmNactivateCallback, icon_done_cb, (XtPointer)this);
    XtAddCallback(icon_, XmNlosingFocusCallback, icon_done_cb,
                  (XtPointer)this);
    push(r, "Browse...", browse_icon_cb, (XtPointer)this);
    n = 0;
    XtSetArg(args[n], XmNlabelType, XmPIXMAP); n++;
    XtSetArg(args[n], XmNwidth, k_preview + 8); n++;
    XtSetArg(args[n], XmNheight, k_preview + 8); n++;
    XtSetArg(args[n], XmNrecomputeSize, False); n++;
    preview_ = XmCreateLabel(r, (char*)"preview", args, n);
    XtManageChild(preview_);

    r = field_row(fields_, "Key:", &key_, 16);
    XmString hint = xmstr("e.g. Ctrl+Alt+T, Super+E, F12");
    XtSetArg(args[0], XmNlabelString, hint);
    XtManageChild(XmCreateLabel(r, (char*)"hint", args, 1));
    XmStringFree(hint);

    r = field_row(fields_, "", 0, 0);
    XmString ql = xmstr("Show in the quick launch bar");
    XtSetArg(args[0], XmNlabelString, ql);
    quick_ = XmCreateToggleButton(r, (char*)"quick", args, 1);
    XmStringFree(ql);
    XtAddCallback(quick_, XmNvalueChangedCallback, field_cb, (XtPointer)this);
    XtManageChild(quick_);

    XtManageChild(fields_);
    XtManageChild(frame);

    /* OK / Cancel. */
    n = 0;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, frame); n++;
    XtSetArg(args[n], XmNtopOffset, 10); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget sep = XmCreateSeparator(dialog_, (char*)"sep", args, n);
    XtManageChild(sep);

    XmString note = xmstr("OK saves the shortcuts and restarts ttr.");
    n = 0;
    XtSetArg(args[n], XmNlabelString, note); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, 10); n++;
    XtSetArg(args[n], XmNleftAttachment, XmATTACH_FORM); n++;
    XtManageChild(XmCreateLabel(dialog_, (char*)"note", args, n));
    XmStringFree(note);

    XmString cancel = xmstr("Cancel"), ok = xmstr("OK");
    n = 0;
    XtSetArg(args[n], XmNlabelString, cancel); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, 6); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_FORM); n++;
    Widget cancel_b = XmCreatePushButton(dialog_, (char*)"cancel", args, n);
    n = 0;
    XtSetArg(args[n], XmNlabelString, ok); n++;
    XtSetArg(args[n], XmNtopAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNtopWidget, sep); n++;
    XtSetArg(args[n], XmNtopOffset, 6); n++;
    XtSetArg(args[n], XmNrightAttachment, XmATTACH_WIDGET); n++;
    XtSetArg(args[n], XmNrightWidget, cancel_b); n++;
    Widget ok_b = XmCreatePushButton(dialog_, (char*)"ok", args, n);
    XmStringFree(cancel);
    XmStringFree(ok);
    XtAddCallback(ok_b, XmNactivateCallback, ok_cb, (XtPointer)this);
    XtAddCallback(cancel_b, XmNactivateCallback, cancel_cb, (XtPointer)this);
    XtManageChild(ok_b);
    XtManageChild(cancel_b);

    XtSetArg(args[0], XmNdefaultButton, ok_b);
    XtSetArg(args[1], XmNcancelButton, cancel_b);
    XtSetValues(dialog_, args, 2);
}

/* ------------------------------------------------------------------ */
/* List and fields                                                     */

Str ShortcutsDialog::label_for(const Shortcut* s)
{
    char buf[512];
    const char* name = s->name.empty() ? s->command.c_str() : s->name.c_str();
    if (s->key.empty())
        sprintf(buf, "%.300s", name);
    else
        sprintf(buf, "%.300s    [%.60s]", name, s->key.c_str());
    if (!s->quicklaunch)
        strcat(buf, "    (not in quick launch)");
    return Str(buf);
}

void ShortcutsDialog::refresh_list()
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

void ShortcutsDialog::select(int i)
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

void ShortcutsDialog::load_fields()
{
    loading_ = true;
    const Shortcut* s = current_ >= 0 ? model_.at(current_) : 0;
    XmTextFieldSetString(name_, (char*)(s ? s->name.c_str() : ""));
    XmTextFieldSetString(command_, (char*)(s ? s->command.c_str() : ""));
    XmTextFieldSetString(icon_, (char*)(s ? s->icon.c_str() : ""));
    XmTextFieldSetString(key_, (char*)(s ? s->key.c_str() : ""));
    XmToggleButtonSetState(quick_, s ? s->quicklaunch : False, False);
    XtSetSensitive(fields_, s != 0);
    loading_ = false;
    update_preview();
}

/* Copy the fields into the current shortcut and refresh its label. */
void ShortcutsDialog::store_fields()
{
    if (current_ < 0 || current_ >= model_.size())
        return;
    Shortcut* s = model_.at(current_);
    s->name = text_value(name_);
    s->command = text_value(command_);
    s->icon = text_value(icon_);
    s->key = text_value(key_);
    s->quicklaunch = XmToggleButtonGetState(quick_);
    Str l = label_for(s);
    XmString xs = xmstr(l.c_str());
    XmListReplaceItemsPos(list_, &xs, 1, current_ + 1);
    XmStringFree(xs);
    XmListSelectPos(list_, current_ + 1, False);
}

void ShortcutsDialog::update_buttons()
{
    XtSetSensitive(remove_b_, current_ >= 0);
    XtSetSensitive(up_b_, current_ > 0);
    XtSetSensitive(down_b_, current_ >= 0 && current_ < model_.size() - 1);
}

void ShortcutsDialog::update_preview()
{
    Display* dpy = XtDisplay(parent_);
    int screen = DefaultScreen(dpy);
    Str name = text_value(icon_);
    Icon* ic = name.empty() ? 0 : icons_->load(name.c_str(), k_preview);

    Pixmap pm = XCreatePixmap(dpy, RootWindow(dpy, screen), k_preview,
                              k_preview, DefaultDepth(dpy, screen));
    painter_->begin(pm, k_preview, k_preview);
    draw_app_icon(*painter_, ic, 0, 0, k_preview, cs_);
    painter_->end();
    delete ic;

    Arg args[1];
    XtSetArg(args[0], XmNlabelPixmap, pm);
    XtSetValues(preview_, args, 1);
    if (preview_pm_ != None)
        XFreePixmap(dpy, preview_pm_);
    preview_pm_ = pm;
}

void ShortcutsDialog::list_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    XmListCallbackStruct* cbs = (XmListCallbackStruct*)call;
    int i = cbs->item_position - 1;
    if (i != self->current_)
        self->select(i);
}

void ShortcutsDialog::field_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    if (!self->loading_)
        self->store_fields();
}

void ShortcutsDialog::icon_done_cb(Widget w, XtPointer closure,
                                   XtPointer call)
{
    (void)w;
    (void)call;
    ((ShortcutsDialog*)closure)->update_preview();
}

/* ------------------------------------------------------------------ */
/* Adding, removing, ordering                                          */

void ShortcutsDialog::add_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    self->store_fields();
    Shortcut* s = new Shortcut;
    s->name = "New Shortcut";
    self->model_.add(s);
    self->current_ = -1;
    self->refresh_list();
    self->select(self->model_.size() - 1);
    XmProcessTraversal(self->command_, XmTRAVERSE_CURRENT);
}

void ShortcutsDialog::add_app(const AppNode* app)
{
    store_fields();
    Shortcut* s = new Shortcut;
    s->name = app->name;
    s->command = app->exec;
    s->icon = app->icon;
    model_.add(s);
    current_ = -1;
    refresh_list();
    select(model_.size() - 1);
}

void ShortcutsDialog::collect_apps(const AppFolder* f, const char* prefix)
{
    int i;
    for (i = 0; i < f->items.size(); i++) {
        const AppNode* node = f->items[i];
        char label[600];
        if (node->kind == AppNode::FOLDER) {
            if (prefix[0])
                sprintf(label, "%.250s / %.250s", prefix, node->name.c_str());
            else
                sprintf(label, "%.250s", node->name.c_str());
            collect_apps(node->folder, label);
        } else if (node->kind == AppNode::APP) {
            if (prefix[0])
                sprintf(label, "%.250s / %.250s", prefix, node->name.c_str());
            else
                sprintf(label, "%.250s", node->name.c_str());
            app_labels_.push_back(Str(label));
            app_nodes_.push_back(node);
        }
    }
}

void ShortcutsDialog::add_program_cb(Widget w, XtPointer closure,
                                     XtPointer call)
{
    (void)w;
    (void)call;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    if (!self->programs_) {
        self->app_labels_.clear();
        self->app_nodes_.clear();
        if (self->apps_)
            self->collect_apps(self->apps_, "");
        int count = self->app_labels_.size();
        XmString* items = new XmString[count > 0 ? count : 1];
        int i;
        for (i = 0; i < count; i++)
            items[i] = xmstr(self->app_labels_[i].c_str());

        XmString title = xmstr("Add from Programs");
        XmString label = xmstr("Programs:");
        Arg args[6];
        int n = 0;
        XtSetArg(args[n], XmNdialogTitle, title); n++;
        XtSetArg(args[n], XmNlistLabelString, label); n++;
        XtSetArg(args[n], XmNlistItems, items); n++;
        XtSetArg(args[n], XmNlistItemCount, count); n++;
        XtSetArg(args[n], XmNlistVisibleItemCount, 15); n++;
        XtSetArg(args[n], XmNmustMatch, True); n++;
        self->programs_ = XmCreateSelectionDialog(self->dialog_,
                                                  (char*)"programs", args, n);
        XmStringFree(title);
        XmStringFree(label);
        for (i = 0; i < count; i++)
            XmStringFree(items[i]);
        delete [] items;
        XtUnmanageChild(XtNameToWidget(self->programs_, "Help"));
        XtUnmanageChild(XtNameToWidget(self->programs_, "Apply"));
        XtAddCallback(self->programs_, XmNokCallback, program_ok_cb, closure);
    }
    XtManageChild(self->programs_);
}

void ShortcutsDialog::program_ok_cb(Widget w, XtPointer closure,
                                    XtPointer call)
{
    (void)w;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    XmSelectionBoxCallbackStruct* cbs = (XmSelectionBoxCallbackStruct*)call;
    Str chosen = xmstring_text(cbs->value);
    int i;
    for (i = 0; i < self->app_labels_.size(); i++) {
        if (strcmp(self->app_labels_[i].c_str(), chosen.c_str()) == 0) {
            self->add_app(self->app_nodes_[i]);
            return;
        }
    }
}

void ShortcutsDialog::remove_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
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

void ShortcutsDialog::up_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    int i = self->current_;
    if (i <= 0)
        return;
    self->store_fields();
    self->model_.swap(i, i - 1);
    self->current_ = -1;
    self->refresh_list();
    self->select(i - 1);
}

void ShortcutsDialog::down_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    int i = self->current_;
    if (i < 0 || i >= self->model_.size() - 1)
        return;
    self->store_fields();
    self->model_.swap(i, i + 1);
    self->current_ = -1;
    self->refresh_list();
    self->select(i + 1);
}

/* ------------------------------------------------------------------ */
/* Browsing for files                                                  */

static void open_browser(Widget* browser, Widget parent, const char* title,
                         const char* dir, XtCallbackProc cb, XtPointer closure)
{
    if (!*browser) {
        *browser = XmCreateFileSelectionDialog(parent, (char*)"browse", 0, 0);
        XtUnmanageChild(XtNameToWidget(*browser, "Help"));
        XtAddCallback(*browser, XmNokCallback, cb, closure);
    }
    XmString t = xmstr(title);
    XmString d = xmstr(dir);
    Arg args[2];
    XtSetArg(args[0], XmNdialogTitle, t);
    XtSetArg(args[1], XmNdirectory, d);
    XtSetValues(*browser, args, 2);
    XmStringFree(t);
    XmStringFree(d);
    XtManageChild(*browser);
}

void ShortcutsDialog::browse_command_cb(Widget w, XtPointer closure,
                                        XtPointer call)
{
    (void)w;
    (void)call;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    self->browse_target_ = self->command_;
    open_browser(&self->browser_, self->dialog_, "Choose a Program",
                 "/usr/bin", browsed_cb, closure);
}

void ShortcutsDialog::browse_icon_cb(Widget w, XtPointer closure,
                                     XtPointer call)
{
    (void)w;
    (void)call;
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    self->browse_target_ = self->icon_;
    open_browser(&self->browser_, self->dialog_, "Choose an Icon",
                 "/usr/share/pixmaps", browsed_cb, closure);
}

void ShortcutsDialog::browsed_cb(Widget w, XtPointer closure, XtPointer call)
{
    ShortcutsDialog* self = (ShortcutsDialog*)closure;
    XmFileSelectionBoxCallbackStruct* cbs =
        (XmFileSelectionBoxCallbackStruct*)call;
    Str path = xmstring_text(cbs->value);
    XtUnmanageChild(w);
    if (path.empty() || !self->browse_target_)
        return;
    XmTextFieldSetString(self->browse_target_, (char*)path.c_str());
    if (self->browse_target_ == self->icon_)
        self->update_preview();
}

/* ------------------------------------------------------------------ */
/* OK and Cancel                                                       */

void ShortcutsDialog::error(const char* msg)
{
    XmString m = xmstr(msg);
    Arg args[1];
    XtSetArg(args[0], XmNmessageString, m);
    Widget err = XmCreateErrorDialog(dialog_, (char*)"error", args, 1);
    XmStringFree(m);
    XtUnmanageChild(XtNameToWidget(err, "Help"));
    XtUnmanageChild(XtNameToWidget(err, "Cancel"));
    XtManageChild(err);
}

void ShortcutsDialog::save()
{
    store_fields();
    int i;
    for (i = 0; i < model_.size(); i++) {
        Shortcut* s = model_.at(i);
        if (s->command.empty()) {
            char msg[400];
            sprintf(msg, "\"%.300s\" has no command.", s->name.c_str());
            select(i);
            error(msg);
            return;
        }
        if (!s->key.empty()) {
            KeySym sym;
            unsigned int mods;
            if (!parse_key_spec(s->key.c_str(), &sym, &mods)) {
                char msg[400];
                sprintf(msg, "\"%.100s\" is not a key I understand.\n"
                        "Use for example Ctrl+Alt+T, Super+E or F12.",
                        s->key.c_str());
                select(i);
                error(msg);
                return;
            }
            /* Store the canonical spelling. */
            char canon[128];
            format_key_spec(sym, mods, canon, sizeof(canon));
            s->key = canon;
        }
    }

    Str path = ShortcutList::default_path();
    if (!model_.save(path.c_str())) {
        char msg[1200];
        sprintf(msg, "Could not write %.1000s", path.c_str());
        error(msg);
        return;
    }
    XtUnmanageChild(dialog_);
    wm_->restart();
}

void ShortcutsDialog::ok_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((ShortcutsDialog*)closure)->save();
}

void ShortcutsDialog::cancel_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    XtUnmanageChild(((ShortcutsDialog*)closure)->dialog_);
}

}
