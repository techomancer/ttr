#include "shell/startmenu.h"
#include "shell/launch.h"
#include "core/apps.h"
#include "x11/config.h"

#include <Xm/Xm.h>
#include <Xm/CascadeBG.h>
#include <Xm/MessageB.h>
#include <Xm/PushBG.h>
#include <Xm/RowColumn.h>
#include <Xm/SelectioB.h>
#include <Xm/SeparatoG.h>

#include <stdio.h>
#include <string.h>

namespace ttr {

/* Menus longer than this wrap into more columns. */
static const int k_menu_rows = 30;

static void load_apps(const Config& cfg, AppFolder* root)
{
    if (cfg.app_source == Config::APPS_TOOLCHEST) {
        ToolchestAppSource src;
        char buf[1024];
        const char* p = cfg.toolchest_files.c_str();
        while (*p) {
            const char* e = strchr(p, ':');
            int n = e ? (int)(e - p) : (int)strlen(p);
            if (n > 0 && n < (int)sizeof(buf)) {
                memcpy(buf, p, n);
                buf[n] = 0;
                src.add_file(buf);
            }
            if (!e)
                break;
            p = e + 1;
        }
        src.load(root);
        root->prune();
    } else {
        DesktopAppSource src(cfg.terminal.c_str());
        src.add_xdg_dirs();
        src.load(root);
        root->prune();
        root->sort_recursive();
    }
}

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

StartMenu::StartMenu(Widget parent, WmLink* wm, const Config& cfg)
    : parent_(parent), wm_(wm), run_dialog_(0), exit_dialog_(0), unpost_(0),
      unpost_closure_(0)
{
    apps_ = new AppFolder("Programs");
    load_apps(cfg, apps_);

    menu_ = XmCreatePopupMenu(parent, (char*)"startMenu", 0, 0);

    Widget programs = XmCreatePulldownMenu(menu_, (char*)"programs", 0, 0);
    build(programs, apps_);
    XmString l = xmstr("Programs");
    Arg args[3];
    int n = 0;
    XtSetArg(args[n], XmNlabelString, l); n++;
    XtSetArg(args[n], XmNmnemonic, (KeySym)'P'); n++;
    XtSetArg(args[n], XmNsubMenuId, programs); n++;
    XtManageChild(XmCreateCascadeButtonGadget(menu_, (char*)"programsItem",
                                              args, n));
    XmStringFree(l);

    XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    button(menu_, "Run...", run_cb, (XtPointer)this);
    XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    button(menu_, "Restart ttr", restart_cb, (XtPointer)this);
    button(menu_, "Exit...", exit_cb, (XtPointer)this);

    XtAddCallback(menu_, XmNunmapCallback, unmap_cb, (XtPointer)this);

    /* Realize now so post() knows the menu height. */
    XtRealizeWidget(XtParent(menu_));
}

StartMenu::~StartMenu()
{
    delete apps_;
}

Widget StartMenu::button(Widget menu, const char* label, XtCallbackProc cb,
                         XtPointer closure)
{
    XmString l = xmstr(label);
    Arg args[1];
    XtSetArg(args[0], XmNlabelString, l);
    Widget b = XmCreatePushButtonGadget(menu, (char*)"item", args, 1);
    XmStringFree(l);
    XtAddCallback(b, XmNactivateCallback, cb, closure);
    XtManageChild(b);
    return b;
}

void StartMenu::build(Widget menu, AppFolder* folder)
{
    int i;
    for (i = 0; i < folder->items.size(); i++) {
        AppNode* node = folder->items[i];
        if (node->kind == AppNode::SEPARATOR) {
            XtManageChild(XmCreateSeparatorGadget(menu, (char*)"sep", 0, 0));
        } else if (node->kind == AppNode::APP) {
            Widget b = button(menu, node->name.c_str(), app_cb,
                              (XtPointer)node);
            Arg args[1];
            XtSetArg(args[0], XmNuserData, (XtPointer)this);
            XtSetValues(b, args, 1);
        } else {
            Widget sub = XmCreatePulldownMenu(menu, (char*)"folder", 0, 0);
            build(sub, node->folder);
            XmString l = xmstr(node->name.c_str());
            Arg args[2];
            XtSetArg(args[0], XmNlabelString, l);
            XtSetArg(args[1], XmNsubMenuId, sub);
            XtManageChild(XmCreateCascadeButtonGadget(menu, (char*)"folderItem",
                                                      args, 2));
            XmStringFree(l);
        }
    }

    int count = folder->items.size();
    if (count > k_menu_rows) {
        Arg args[2];
        XtSetArg(args[0], XmNpacking, XmPACK_COLUMN);
        XtSetArg(args[1], XmNnumColumns,
                 (short)((count + k_menu_rows - 1) / k_menu_rows));
        XtSetValues(menu, args, 2);
    }
}

void StartMenu::post(int x, int y_bottom, unsigned long time)
{
    Dimension h = 0;
    Arg args[1];
    XtSetArg(args[0], XmNheight, &h);
    XtGetValues(menu_, args, 1);

    XButtonEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = ButtonPress;
    ev.time = time;
    ev.x_root = x;
    ev.y_root = y_bottom - (int)h;
    XmMenuPosition(menu_, &ev);
    XtManageChild(menu_);
}

void StartMenu::on_unpost(void (*fn)(void*), void* closure)
{
    unpost_ = fn;
    unpost_closure_ = closure;
}

void StartMenu::unmap_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    StartMenu* self = (StartMenu*)closure;
    if (self->unpost_)
        self->unpost_(self->unpost_closure_);
}

void StartMenu::defer(Command cmd, AppNode* app)
{
    Pending* p = new Pending;
    p->menu = this;
    p->cmd = cmd;
    p->app = app;
    XtAppAddTimeOut(XtWidgetToApplicationContext(parent_), 0, deferred_cb,
                    (XtPointer)p);
}

void StartMenu::deferred_cb(XtPointer closure, XtIntervalId* id)
{
    (void)id;
    Pending* p = (Pending*)closure;
    switch (p->cmd) {
    case CMD_APP:
        spawn(p->app->exec.c_str());
        break;
    case CMD_RUN:
        p->menu->run_dialog();
        break;
    case CMD_RESTART:
        p->menu->wm_->restart();
        break;
    case CMD_EXIT:
        p->menu->exit_dialog();
        break;
    }
    delete p;
}

void StartMenu::app_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)call;
    /* The menu is found through the widget tree: closure is the node. */
    StartMenu* self = 0;
    Arg args[1];
    XtSetArg(args[0], XmNuserData, &self);
    XtGetValues(w, args, 1);
    if (self)
        self->defer(CMD_APP, (AppNode*)closure);
}

void StartMenu::run_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StartMenu*)closure)->defer(CMD_RUN, 0);
}

void StartMenu::run_dialog()
{
    if (!run_dialog_) {
        XmString title = xmstr("Run");
        XmString label = xmstr("Type the name of a program to run:");
        Arg args[3];
        int n = 0;
        XtSetArg(args[n], XmNdialogTitle, title); n++;
        XtSetArg(args[n], XmNselectionLabelString, label); n++;
        XtSetArg(args[n], XmNautoUnmanage, True); n++;
        run_dialog_ = XmCreatePromptDialog(parent_, (char*)"runDialog",
                                           args, n);
        XmStringFree(title);
        XmStringFree(label);
        XtUnmanageChild(XtNameToWidget(run_dialog_, "Help"));
        XtAddCallback(run_dialog_, XmNokCallback, run_ok_cb, (XtPointer)this);
    }
    XtManageChild(run_dialog_);
}

void StartMenu::run_ok_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)closure;
    XmSelectionBoxCallbackStruct* cbs = (XmSelectionBoxCallbackStruct*)call;
    char* text = (char*)XmStringUnparse(cbs->value, 0, XmCHARSET_TEXT,
                                        XmCHARSET_TEXT, 0, 0, XmOUTPUT_ALL);
    if (text) {
        if (*text)
            spawn(text);
        XtFree(text);
    }
}

void StartMenu::restart_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StartMenu*)closure)->defer(CMD_RESTART, 0);
}

void StartMenu::exit_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StartMenu*)closure)->defer(CMD_EXIT, 0);
}

void StartMenu::exit_dialog()
{
    if (!exit_dialog_) {
        XmString title = xmstr("Exit");
        XmString msg = xmstr("Exit To The Roots?");
        Arg args[3];
        int n = 0;
        XtSetArg(args[n], XmNdialogTitle, title); n++;
        XtSetArg(args[n], XmNmessageString, msg); n++;
        XtSetArg(args[n], XmNdialogStyle, XmDIALOG_FULL_APPLICATION_MODAL); n++;
        exit_dialog_ = XmCreateQuestionDialog(parent_, (char*)"exitDialog",
                                              args, n);
        XmStringFree(title);
        XmStringFree(msg);
        XtUnmanageChild(XtNameToWidget(exit_dialog_, "Help"));
        XtAddCallback(exit_dialog_, XmNokCallback, exit_ok_cb,
                      (XtPointer)this);
    }
    XtManageChild(exit_dialog_);
}

void StartMenu::exit_ok_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StartMenu*)closure)->wm_->quit();
}

}
