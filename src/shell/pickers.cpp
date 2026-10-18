#include "shell/pickers.h"
#include "core/apps.h"

#include <Xm/Xm.h>
#include <Xm/FileSB.h>
#include <Xm/SelectioB.h>

#include <stdio.h>
#include <string.h>

namespace ttr {

static XmString xmstr(const char* s)
{
    return XmStringCreateLocalized((char*)s);
}

static Str xmstring_text(XmString s)
{
    char* t = (char*)XmStringUnparse(s, 0, XmCHARSET_TEXT, XmCHARSET_TEXT, 0,
                                     0, XmOUTPUT_ALL);
    Str r(t ? t : "");
    XtFree(t);
    return r;
}

/* ------------------------------------------------------------------ */
/* A program from the start menu                                       */

ProgramPicker::ProgramPicker(const AppFolder* apps, Chosen chosen,
                             void* closure)
    : apps_(apps), chosen_(chosen), closure_(closure), dialog_(0)
{
}

ProgramPicker::~ProgramPicker()
{
    if (dialog_)
        XtRemoveCallback(dialog_, XmNdestroyCallback, destroyed_cb,
                         (XtPointer)this);
}

void ProgramPicker::collect(const AppFolder* f, const char* prefix)
{
    int i;
    for (i = 0; i < f->items.size(); i++) {
        const AppNode* node = f->items[i];
        if (node->kind == AppNode::SEPARATOR)
            continue;
        char label[600];
        if (prefix[0])
            sprintf(label, "%.250s / %.250s", prefix, node->name.c_str());
        else
            sprintf(label, "%.250s", node->name.c_str());
        if (node->kind == AppNode::FOLDER) {
            collect(node->folder, label);
        } else {
            labels_.push_back(Str(label));
            nodes_.push_back(node);
        }
    }
}

void ProgramPicker::show(Widget parent)
{
    if (!dialog_) {
        labels_.clear();
        nodes_.clear();
        if (apps_)
            collect(apps_, "");
        int count = labels_.size();
        XmString* items = new XmString[count > 0 ? count : 1];
        int i;
        for (i = 0; i < count; i++)
            items[i] = xmstr(labels_[i].c_str());

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
        dialog_ = XmCreateSelectionDialog(parent, (char*)"programs", args, n);
        XmStringFree(title);
        XmStringFree(label);
        for (i = 0; i < count; i++)
            XmStringFree(items[i]);
        delete [] items;
        XtUnmanageChild(XtNameToWidget(dialog_, "Help"));
        XtUnmanageChild(XtNameToWidget(dialog_, "Apply"));
        XtAddCallback(dialog_, XmNokCallback, ok_cb, (XtPointer)this);
        XtAddCallback(dialog_, XmNdestroyCallback, destroyed_cb,
                      (XtPointer)this);
    }
    XtManageChild(dialog_);
}

void ProgramPicker::ok_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    ProgramPicker* self = (ProgramPicker*)closure;
    XmSelectionBoxCallbackStruct* cbs = (XmSelectionBoxCallbackStruct*)call;
    Str chosen = xmstring_text(cbs->value);
    int i;
    for (i = 0; i < self->labels_.size(); i++) {
        if (strcmp(self->labels_[i].c_str(), chosen.c_str()) == 0) {
            self->chosen_(self->closure_, self->nodes_[i]);
            return;
        }
    }
}

void ProgramPicker::destroyed_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((ProgramPicker*)closure)->dialog_ = 0;
}

/* ------------------------------------------------------------------ */
/* A file                                                              */

FileBrowser::FileBrowser(Chosen chosen, void* closure)
    : chosen_(chosen), closure_(closure), dialog_(0)
{
}

FileBrowser::~FileBrowser()
{
    if (dialog_)
        XtRemoveCallback(dialog_, XmNdestroyCallback, destroyed_cb,
                         (XtPointer)this);
}

void FileBrowser::show(Widget parent, const char* title, const char* dir)
{
    if (!dialog_) {
        dialog_ = XmCreateFileSelectionDialog(parent, (char*)"browse", 0, 0);
        XtUnmanageChild(XtNameToWidget(dialog_, "Help"));
        XtAddCallback(dialog_, XmNokCallback, ok_cb, (XtPointer)this);
        XtAddCallback(dialog_, XmNdestroyCallback, destroyed_cb,
                      (XtPointer)this);
    }
    XmString t = xmstr(title);
    XmString d = xmstr(dir);
    Arg args[2];
    XtSetArg(args[0], XmNdialogTitle, t);
    XtSetArg(args[1], XmNdirectory, d);
    XtSetValues(dialog_, args, 2);
    XmStringFree(t);
    XmStringFree(d);
    XtManageChild(dialog_);
}

void FileBrowser::ok_cb(Widget w, XtPointer closure, XtPointer call)
{
    FileBrowser* self = (FileBrowser*)closure;
    XmFileSelectionBoxCallbackStruct* cbs =
        (XmFileSelectionBoxCallbackStruct*)call;
    Str path = xmstring_text(cbs->value);
    XtUnmanageChild(w);
    if (!path.empty())
        self->chosen_(self->closure_, path.c_str());
}

void FileBrowser::destroyed_cb(Widget w, XtPointer closure, XtPointer call)
{
    (void)w;
    (void)call;
    ((FileBrowser*)closure)->dialog_ = 0;
}

}
