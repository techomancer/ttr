#include "shell/shell.h"
#include "shell/startmenu.h"
#include "shell/taskbar.h"
#include "shell/winmenu.h"

namespace ttr {

Shell::Shell(Widget parent, WmLink* wm, const Config& cfg)
{
    winmenu_ = new WindowMenu(parent, wm);
    start_ = new StartMenu(parent, wm, cfg);
    taskbar_ = new Taskbar(parent, wm, cfg, start_, winmenu_);
}

Shell::~Shell()
{
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
                             unsigned long time, ActionMask allowed)
{
    winmenu_->post(id, x_root, y_root, 0, (Time)time, allowed);
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
