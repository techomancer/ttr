/*
 * Choosers shared by the settings dialogs: a program from the start menu
 * (an XmSelectionDialog), and a file (an XmFileSelectionDialog). Each
 * dialog is made on first use as a child of the given parent and goes
 * away with it.
 */
#ifndef TTR_SHELL_PICKERS_H
#define TTR_SHELL_PICKERS_H

#include "core/str.h"
#include "core/vector.h"

#include <X11/Intrinsic.h>

namespace ttr {

struct AppFolder;
struct AppNode;

class ProgramPicker {
public:
    typedef void (*Chosen)(void* closure, const AppNode* app);

    ProgramPicker(const AppFolder* apps, Chosen chosen, void* closure);
    ~ProgramPicker();
    void show(Widget parent);

private:
    ProgramPicker(const ProgramPicker&);
    ProgramPicker& operator=(const ProgramPicker&);

    void collect(const AppFolder* f, const char* prefix);
    static void ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void destroyed_cb(Widget w, XtPointer closure, XtPointer call);

    const AppFolder* apps_;
    Chosen chosen_;
    void* closure_;
    Widget dialog_;
    Vector<Str> labels_;
    Vector<const AppNode*> nodes_;
};

class FileBrowser {
public:
    typedef void (*Chosen)(void* closure, const char* path);

    FileBrowser(Chosen chosen, void* closure);
    ~FileBrowser();
    void show(Widget parent, const char* title, const char* dir);

private:
    FileBrowser(const FileBrowser&);
    FileBrowser& operator=(const FileBrowser&);

    static void ok_cb(Widget w, XtPointer closure, XtPointer call);
    static void destroyed_cb(Widget w, XtPointer closure, XtPointer call);

    Chosen chosen_;
    void* closure_;
    Widget dialog_;
};

}

#endif
