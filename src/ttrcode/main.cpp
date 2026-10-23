/*
 * ttrcode, the ttr code editor (ttrcode/DESIGN.md).
 *
 *   ttrcode [-fn font] [X toolkit options] [directory] [file ...]
 *
 * The directory is the workspace (default: the current one); files open
 * in tabs.
 */
#include "app.h"

#include "../../tt/pane.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <Xm/Xm.h>

#if !defined(TTR_DATADIR)
#define TTR_DATADIR "/usr/share/ttr"
#endif

namespace {

struct Resources {
    String font;
    int tab_width;
    int indent_width;
    Boolean insert_spaces;
    Boolean line_numbers;
    String text_fg, text_bg, select_fg, select_bg, gutter_fg, gutter_bg, cursor;
};

#define ROFF(f) XtOffsetOf(Resources, f)
#define RSTR(name, cls, f, dflt) \
    { (String)name, (String)cls, (String)XtRString, sizeof(String), ROFF(f), (String)XtRString, \
      (XtPointer)dflt }
XtResource k_resources[] = {
    RSTR("font", "Font", font, ""),
    { (String)"tabWidth", (String)"TabWidth", (String)XtRInt, sizeof(int), ROFF(tab_width),
      (String)XtRString, (XtPointer)"8" },
    { (String)"indentWidth", (String)"IndentWidth", (String)XtRInt, sizeof(int),
      ROFF(indent_width), (String)XtRString, (XtPointer)"4" },
    { (String)"insertSpaces", (String)"InsertSpaces", (String)XtRBoolean, sizeof(Boolean),
      ROFF(insert_spaces), (String)XtRString, (XtPointer)"true" },
    { (String)"lineNumbers", (String)"LineNumbers", (String)XtRBoolean, sizeof(Boolean),
      ROFF(line_numbers), (String)XtRString, (XtPointer)"true" },
    RSTR("textForeground", "TextForeground", text_fg, "black"),
    RSTR("textBackground", "TextBackground", text_bg, "white"),
    RSTR("selectForeground", "SelectForeground", select_fg, "white"),
    RSTR("selectBackground", "SelectBackground", select_bg, "#000080"),
    RSTR("gutterForeground", "GutterForeground", gutter_fg, "#808080"),
    RSTR("gutterBackground", "GutterBackground", gutter_bg, "#f0f0f0"),
    RSTR("cursorColor", "CursorColor", cursor, "black")
};
#undef RSTR
#undef ROFF

XrmOptionDescRec k_options[] = {
    { (char*)"-fn", (char*)".font", XrmoptionSepArg, 0 },
    { (char*)"-font", (char*)".font", XrmoptionSepArg, 0 }
};

uint32_t color(Display* dpy, const char* name, uint32_t dflt)
{
    XColor c;
    if (!name || !XParseColor(dpy, DefaultColormap(dpy, DefaultScreen(dpy)), name, &c))
        return dflt;
    return ((uint32_t)(c.red >> 8) << 16) | ((uint32_t)(c.green >> 8) << 8) | (uint32_t)(c.blue >> 8);
}

} /* namespace */

int main(int argc, char** argv)
{
    static String fallbacks[] = {
        (String)"*fontList: -*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1",
        (String)"*menuBar*fontList: -*-helvetica-bold-r-normal--12-*",
        (String)"*tabs*fontList: -*-helvetica-medium-r-normal--12-*",
        (String)"*XmToggleButton.selectColor: #3c5a8c",
        (String)"*XmScrollBar.width: 17",
        (String)"*XmScrollBar.height: 17",
        (String)"*tree.visibleItemCount: 20",
        0
    };
    XtAppContext ctx;
    Widget top = XtAppInitialize(&ctx, "TTRcode", k_options, XtNumber(k_options), &argc, argv,
                                 fallbacks, 0, 0);
    Resources res;
    XtGetApplicationResources(top, &res, k_resources, XtNumber(k_resources), 0, 0);
    Display* dpy = XtDisplay(top);

    /* The workspace: a directory argument, or here. */
    char cwd[2048];
    if (!getcwd(cwd, sizeof(cwd)))
        strcpy(cwd, "/");
    static char workspace[2048];
    strcpy(workspace, cwd);
    int first_file = 1;
    struct stat st;
    if (argc > 1 && argv[1][0] == '-') {
        fprintf(stderr, "usage: ttrcode [-fn font] [directory] [file ...]\n");
        return 2;
    }
    if (argc > 1 && stat(argv[1], &st) == 0 && S_ISDIR(st.st_mode)) {
        if (argv[1][0] == '/')
            snprintf(workspace, sizeof(workspace), "%s", argv[1]);
        else
            snprintf(workspace, sizeof(workspace), "%s/%s", cwd, argv[1]);
        size_t l = strlen(workspace);
        while (l > 1 && workspace[l - 1] == '/')
            workspace[--l] = 0;
        first_file = 2;
    }

    static char fdirs[2048];
    tt::font_path(argv[0], TTR_DATADIR, fdirs, sizeof(fdirs));
    ttrcode::Options o;
    o.font = res.font && res.font[0] ? res.font : tt::pick_font(dpy);
    o.fallback_fonts = fdirs;
    o.workspace = workspace;
    o.tab_width = res.tab_width > 0 ? res.tab_width : 8;
    o.indent_width = res.indent_width > 0 ? res.indent_width : 0;
    o.insert_spaces = res.insert_spaces != False;
    o.line_numbers = res.line_numbers != False;
    o.colors.text = color(dpy, res.text_fg, 0x000000);
    o.colors.background = color(dpy, res.text_bg, 0xffffff);
    o.colors.selected_text = color(dpy, res.select_fg, 0xffffff);
    o.colors.selected_background = color(dpy, res.select_bg, 0x000080);
    o.colors.gutter_text = color(dpy, res.gutter_fg, 0x808080);
    o.colors.gutter_background = color(dpy, res.gutter_bg, 0xf0f0f0);
    o.colors.control = 0x800000;
    o.colors.found_background = 0xffff80;
    o.colors.cursor = color(dpy, res.cursor, 0x000000);

    ttrcode::App* app = new ttrcode::App(top, o);
    for (int i = first_file; i < argc; i++) {
        char path[2048];
        if (argv[i][0] == '/')
            snprintf(path, sizeof(path), "%s", argv[i]);
        else
            snprintf(path, sizeof(path), "%s/%s", cwd, argv[i]);
        app->open_file(path);
    }
    if (first_file >= argc)
        app->command(ttrcode::CMD_NEW);

    XtRealizeWidget(top);
    app->realized();
    XtAppMainLoop(ctx);
    return 0;
}
