#include "shell/shell.h"
#include "shell/startmenu.h"
#include "shell/taskbar.h"
#include "shell/notifier.h"
#include "shell/notifysrc.h"
#include "shell/winmenu.h"
#include "x11/config.h"
#include "x11/iconload.h"

namespace ttr {

Shell::Shell(Widget parent, WmLink* wm, const Config& cfg)
    : dbus_notify_(0), x_notify_(0), log_notify_(0)
{
    winmenu_ = new WindowMenu(parent, wm, cfg.desktops);
    start_ = new StartMenu(parent, wm, cfg);
    taskbar_ = new Taskbar(parent, wm, cfg, start_, winmenu_);

    /* Notifications pop up above the taskbar. */
    icons_ = new IconLoader(XtDisplay(parent));
    Display* dpy = XtDisplay(parent);
    int screen = DefaultScreen(dpy);
    const Rect& bar = taskbar_->geometry();
    Rect area(0, 0, DisplayWidth(dpy, screen), bar.y > 0 ? bar.y
                                                        : DisplayHeight(dpy,
                                                                        screen));
    notifier_ = new Notifier(parent, wm, cfg, icons_, area);

    XtAppContext app = XtWidgetToApplicationContext(parent);
    x_notify_ = new XNotifySource(parent, notifier_);
    if (!cfg.notification_log.empty())
        log_notify_ = new LogNotifySource(app, cfg.notification_log.c_str(),
                                          notifier_);
    dbus_notify_ = new DBusNotifySource(app, notifier_);
    if (dbus_notify_->start()) {
        notifier_->set_listener(dbus_notify_);
    } else {
        delete dbus_notify_;
        dbus_notify_ = 0;
    }
}

Shell::~Shell()
{
    delete dbus_notify_;
    delete x_notify_;
    delete log_notify_;
    delete notifier_;
    delete icons_;
    delete taskbar_;
    delete start_;
    delete winmenu_;
}

void Shell::task_added(const TaskInfo& t)
{
    taskbar_->add(t);
}

void Shell::task_changed(const TaskInfo& t)
{
    taskbar_->change(t);
}

void Shell::task_removed(ToplevelId id)
{
    taskbar_->remove(id);
}

void Shell::active_changed(ToplevelId id)
{
    taskbar_->set_active(id);
}

void Shell::show_window_menu(ToplevelId id, int x_root, int y_root,
                             unsigned long time, ActionMask allowed,
                             int desktop)
{
    winmenu_->post(id, x_root, y_root, 0, (Time)time, allowed, desktop);
}

void Shell::desktop_changed(int current, int count)
{
    (void)count;
    start_->set_desktop(current);
}

void Shell::show_start_menu(unsigned long time)
{
    taskbar_->post_start_menu(time);
}

void Shell::shutdown()
{
    taskbar_->release();
}

}
