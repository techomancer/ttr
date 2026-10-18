#include "shell/startmenu.h"
#include "core/launch.h"
#include "shell/settings.h"
#include "core/apps.h"
#include "core/icon.h"
#include "core/painter.h"
#include "core/scale.h"
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
#include <Xm/ToggleBG.h>

#include <stdio.h>
#include <string.h>

namespace ttr {


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

/* At 100%; scaled() when used. */
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
    : parent_(parent), wm_(wm), run_dialog_(0), exit_dialog_(0),
      settings_(0), desktops_(cfg.desktops), desk_items_(0), unpost_(0),
      unpost_closure_(0)
{
    Display* dpy = XtDisplay(parent);
    apps_ = new AppFolder("Programs");
    load_apps(cfg, apps_);
    settings_ = new SettingsDialog(parent, wm, cfg, apps_);
    painter_ = new XPainter(dpy, DefaultScreen(dpy));
    const char* fonts[6];
    fonts[0] = cfg.menu_xfont.c_str();
    int k;
    for (k = 0; k < 5; k++)
        fonts[k + 1] = k_menu_fonts[k];
    painter_->load_font(fonts);
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

    if (desktops_ > 1) {
        Arg dargs[1];
        XtSetArg(dargs[0], XmNradioBehavior, True);
        Widget desks = XmCreatePulldownMenu(menu_, (char*)"desktops", dargs,
                                            1);
        desk_items_ = new DeskItem[desktops_];
        int i;
        for (i = 0; i < desktops_; i++) {
            char label[32];
            sprintf(label, "Desktop %d", i + 1);
            XmString l = xmstr(label);
            Arg targs[3];
            XtSetArg(targs[0], XmNlabelString, l);
            XtSetArg(targs[1], XmNindicatorType, XmONE_OF_MANY);
            XtSetArg(targs[2], XmNvisibleWhenOff, True);
            Widget b = XmCreateToggleButtonGadget(desks, (char*)"desktopItem",
                                                  targs, 3);
            XmStringFree(l);
            desk_items_[i].menu = this;
            desk_items_[i].desktop = i;
            desk_items_[i].button = b;
            XtAddCallback(b, XmNvalueChangedCallback, desktop_cb,
                          (XtPointer)&desk_items_[i]);
            XtManageChild(b);
        }
        item(menu_, "Desktops", "user-desktop", GLYPH_NONE, 0, 0, desks);
    }
    XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    item(menu_, "Run...", "system-run", GLYPH_NONE, run_cb, (XtPointer)this,
         0);
    item(menu_, "Settings...", "preferences-system", GLYPH_NONE,
         settings_cb, (XtPointer)this, 0);
    XtManageChild(XmCreateSeparatorGadget(menu_, (char*)"sep", 0, 0));
    item(menu_, "Restart ttr", "view-refresh", GLYPH_NONE, restart_cb,
         (XtPointer)this, 0);
    item(menu_, "Exit...", "system-log-out", GLYPH_NONE, exit_cb,
         (XtPointer)this, 0);

    XtAddCallback(menu_, XmNmapCallback, map_cb, (XtPointer)this);
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
    delete [] desk_items_;
    delete settings_;
    delete icons_;
    delete painter_;
    delete apps_;
}

int StartMenu::label_height()
{
    int h = painter_->text_ascent() + painter_->text_descent() + scaled(4);
    int icon = scaled(k_icon) + scaled(2);
    return h > icon ? h : icon;
}

/* Menus longer than the screen wrap into more columns. A menu item is its
 * label plus the gadget's margins and shadows. */
int StartMenu::menu_rows()
{
    Display* dpy = XtDisplay(parent_);
    int rows = DisplayHeight(dpy, DefaultScreen(dpy)) * 9 / 10 /
               (label_height() + scaled(8));
    return rows < 10 ? 10 : rows;
}

/* Motif 2.1 labels show text or a pixmap, not both, so the icon and the
 * text are drawn together into one pixmap. */
Pixmap StartMenu::label_pixmap(const char* text, const Icon* ic,
                               Glyph fallback)
{
    Display* dpy = XtDisplay(parent_);
    int screen = DefaultScreen(dpy);
    int len = (int)strlen(text);
    int asc = painter_->text_ascent(), desc = painter_->text_descent();
    int icon = scaled(k_icon), gap = scaled(k_icon_gap);
    int w = icon + gap + painter_->text_width(text, len) + scaled(2);
    int h = label_height();

    Pixmap pm = XCreatePixmap(dpy, RootWindow(dpy, screen), w, h,
                              DefaultDepth(dpy, screen));
    painter_->begin(pm, w, h);
    painter_->fill(Rect(0, 0, w, h), cs_.bg);
    int iy = (h - icon) / 2;
    if (ic)
        painter_->icon(*ic, 0, iy, icon, cs_.bg);
    else if (fallback == GLYPH_FOLDER)
        draw_folder_icon(*painter_, 0, iy, icon);
    else if (fallback == GLYPH_APP)
        draw_app_icon(*painter_, 0, 0, iy, icon, cs_);
    painter_->text(icon + gap, (h - asc - desc) / 2 + asc, text, len, cs_.fg);
    painter_->end();
    pixmaps_.push_back(pm);
    return pm;
}

Widget StartMenu::item(Widget menu, const char* label, const char* icon,
                       Glyph fallback, XtCallbackProc cb, XtPointer closure,
                       Widget sub)
{
    Icon* ic = icon && *icon ? icons_->load(icon, scaled(k_icon)) : 0;
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

    int count = folder->items.size(), rows = menu_rows();
    if (count > rows) {
        Arg args[2];
        XtSetArg(args[0], XmNpacking, XmPACK_COLUMN);
        XtSetArg(args[1], XmNnumColumns, (short)((count + rows - 1) / rows));
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

void StartMenu::map_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StartMenu*)closure)->wm_->menu_shown(true);
}

void StartMenu::unmap_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    StartMenu* self = (StartMenu*)closure;
    self->wm_->menu_shown(false);
    if (self->unpost_)
        self->unpost_(self->unpost_closure_);
}

void StartMenu::set_desktop(int desktop)
{
    int i;
    for (i = 0; desk_items_ && i < desktops_; i++)
        XmToggleButtonGadgetSetState(desk_items_[i].button, i == desktop,
                                     False);
}

void StartMenu::desktop_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    if (!((XmToggleButtonCallbackStruct*)call)->set)
        return;     /* the radio partner being switched off */
    DeskItem* item = (DeskItem*)closure;
    item->menu->defer(CMD_DESKTOP, 0, item->desktop);
}

void StartMenu::defer(Command cmd, AppNode* app, int desktop)
{
    Pending* p = new Pending;
    p->menu = this;
    p->cmd = cmd;
    p->app = app;
    p->desktop = desktop;
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
    case CMD_SETTINGS:
        p->menu->settings_->show();
        break;
    case CMD_DESKTOP:
        p->menu->wm_->switch_desktop(p->desktop);
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

void StartMenu::settings_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((StartMenu*)closure)->defer(CMD_SETTINGS, 0);
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
