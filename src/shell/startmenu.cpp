#include "shell/startmenu.h"
#include "shell/launch.h"
#include "core/apps.h"
#include "core/icon.h"
#include "core/painter.h"
#include "x11/config.h"
#include "x11/iconload.h"
#include "x11/xpainter.h"

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

static const char* const k_menu_fonts[] = {
    "-*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1",
    "-*-helvetica-bold-r-normal--*-120-*-*-*-*-*-*",
    "variable",
    "fixed",
    0
};

static const int k_icon = 16;       /* menu icon size */
static const int k_icon_gap = 6;    /* between icon and text */

static Rgb pixel_rgb(Display* dpy, Pixel p)
{
    XColor c;
    c.pixel = p;
    XQueryColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), &c);
    return Rgb(c.red, c.green, c.blue);
}

StartMenu::StartMenu(Widget parent, WmLink* wm, const Config& cfg)
    : parent_(parent), wm_(wm), run_dialog_(0), exit_dialog_(0), unpost_(0),
      unpost_closure_(0)
{
    Display* dpy = XtDisplay(parent);
    apps_ = new AppFolder("Programs");
    load_apps(cfg, apps_);
    painter_ = new XPainter(dpy, DefaultScreen(dpy));
    painter_->load_font(k_menu_fonts);
    icons_ = new IconLoader(dpy);

    menu_ = XmCreatePopupMenu(parent, (char*)"startMenu", 0, 0);

    /* Item labels are drawn by us in the menu's own colors. */
    Pixel bg = 0, fg = 0;
    Arg args[2];
    XtSetArg(args[0], XmNbackground, &bg);
    XtSetArg(args[1], XmNforeground, &fg);
    XtGetValues(menu_, args, 2);
    cs_ = motif_colors(pixel_rgb(dpy, bg));
    cs_.fg = pixel_rgb(dpy, fg);

    Widget programs = XmCreatePulldownMenu(menu_, (char*)"programs", 0, 0);
    build(programs, apps_);
    item(menu_, "Programs", 0, GLYPH_FOLDER, 0, 0, programs);
    XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    item(menu_, "Run...", "system-run", GLYPH_NONE, run_cb, (XtPointer)this,
         0);
    XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    item(menu_, "Restart ttr", "view-refresh", GLYPH_NONE, restart_cb,
         (XtPointer)this, 0);
    item(menu_, "Exit...", "system-log-out", GLYPH_NONE, exit_cb,
         (XtPointer)this, 0);

    XtAddCallback(menu_, XmNunmapCallback, unmap_cb, (XtPointer)this);

    /* Realize now so post() knows the menu height. */
    XtRealizeWidget(XtParent(menu_));
}

StartMenu::~StartMenu()
{
    Display* dpy = XtDisplay(parent_);
    int i;
    for (i = 0; i < pixmaps_.size(); i++)
        XFreePixmap(dpy, pixmaps_[i]);
    delete icons_;
    delete painter_;
    delete apps_;
}

/* Motif 2.1 labels show text or a pixmap, not both, so the icon and the
 * text are drawn together into one pixmap. */
Pixmap StartMenu::label_pixmap(const char* text, const Icon* icon,
                               Glyph fallback)
{
    Display* dpy = XtDisplay(parent_);
    int screen = DefaultScreen(dpy);
    int len = (int)strlen(text);
    int asc = painter_->text_ascent(), desc = painter_->text_descent();
    int w = k_icon + k_icon_gap + painter_->text_width(text, len) + 2;
    int h = asc + desc + 4;
    if (h < k_icon + 2)
        h = k_icon + 2;

    Pixmap pm = XCreatePixmap(dpy, RootWindow(dpy, screen), w, h,
                              DefaultDepth(dpy, screen));
    painter_->begin(pm, w, h);
    painter_->fill(Rect(0, 0, w, h), cs_.bg);
    int iy = (h - k_icon) / 2;
    if (icon)
        painter_->icon(*icon, 0, iy, k_icon, cs_.bg);
    else if (fallback == GLYPH_FOLDER)
        draw_folder_icon(*painter_, 0, iy, k_icon);
    else if (fallback == GLYPH_APP)
        draw_app_icon(*painter_, 0, 0, iy, k_icon, cs_);
    painter_->text(k_icon + k_icon_gap, (h - asc - desc) / 2 + asc, text, len,
                   cs_.fg);
    painter_->end();
    pixmaps_.push_back(pm);
    return pm;
}

Widget StartMenu::item(Widget menu, const char* label, const char* icon,
                       Glyph fallback, XtCallbackProc cb, XtPointer closure,
                       Widget sub)
{
    Icon* ic = icon && *icon ? icons_->load(icon, k_icon) : 0;
    Pixmap pm = label_pixmap(label, ic, fallback);
    delete ic;      /* the painter keeps its own pixmap */

    Arg args[3];
    int n = 0;
    XtSetArg(args[n], XmNlabelType, XmPIXMAP); n++;
    XtSetArg(args[n], XmNlabelPixmap, pm); n++;
    Widget b;
    if (sub) {
        XtSetArg(args[n], XmNsubMenuId, sub); n++;
        b = XmCreateCascadeButtonGadget(menu, (char*)"folderItem", args, n);
    } else {
        b = XmCreatePushButtonGadget(menu, (char*)"item", args, n);
        XtAddCallback(b, XmNactivateCallback, cb, closure);
    }
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
            Widget b = item(menu, node->name.c_str(), node->icon.c_str(),
                            GLYPH_APP, app_cb, (XtPointer)node, 0);
            Arg args[1];
            XtSetArg(args[0], XmNuserData, (XtPointer)this);
            XtSetValues(b, args, 1);
        } else {
            Widget sub = XmCreatePulldownMenu(menu, (char*)"folder", 0, 0);
            build(sub, node->folder);
            item(menu, node->name.c_str(), 0, GLYPH_FOLDER, 0, 0, sub);
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
