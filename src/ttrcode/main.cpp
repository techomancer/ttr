/*
 * ttrcode, the ttr code editor (ttrcode/DESIGN.md).
 *
 *   ttrcode [-fn font] [-renderer auto|core|soft|gl] [-dynbg name]
 *           [-text sharp|smooth|auto] [X toolkit options] [directory] [file ...]
 *
 * The directory is the workspace (default: the current one); files open
 * in tabs.
 */
#include "app.h"

#include "../../th/motif.h"
#include "../../tt/pane.h"
#include "core/settings.h"

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
    String scheme, ui_theme;
    int tree_width, bottom_height;
    String renderer, terminal_background;
    Boolean auto_indent, show_whitespace, highlight_line, cursor_blink;
    int ruler;
    String cursor_shape, cursor_color;
    String terminal_font, text_rendering, terminal_text_rendering;
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
    RSTR("colorScheme", "ColorScheme", scheme, "Light"),
    RSTR("uiTheme", "UiTheme", ui_theme, ""),
    { (String)"treeWidth", (String)"TreeWidth", (String)XtRInt, sizeof(int), ROFF(tree_width),
      (String)XtRString, (XtPointer)"220" },
    { (String)"bottomHeight", (String)"BottomHeight", (String)XtRInt, sizeof(int),
      ROFF(bottom_height), (String)XtRString, (XtPointer)"220" },
    RSTR("renderer", "Renderer", renderer, "auto"),
    { (String)"autoIndent", (String)"AutoIndent", (String)XtRBoolean, sizeof(Boolean),
      ROFF(auto_indent), (String)XtRString, (XtPointer)"true" },
    { (String)"showWhitespace", (String)"ShowWhitespace", (String)XtRBoolean, sizeof(Boolean),
      ROFF(show_whitespace), (String)XtRString, (XtPointer)"false" },
    { (String)"highlightLine", (String)"HighlightLine", (String)XtRBoolean, sizeof(Boolean),
      ROFF(highlight_line), (String)XtRString, (XtPointer)"true" },
    { (String)"cursorBlink", (String)"CursorBlink", (String)XtRBoolean, sizeof(Boolean),
      ROFF(cursor_blink), (String)XtRString, (XtPointer)"true" },
    { (String)"rulerColumn", (String)"RulerColumn", (String)XtRInt, sizeof(int), ROFF(ruler),
      (String)XtRString, (XtPointer)"80" },
    RSTR("cursorShape", "CursorShape", cursor_shape, "bar"),
    RSTR("cursorColor", "CursorColor", cursor_color, ""),
    RSTR("terminalBackground", "TerminalBackground", terminal_background, ""),
    RSTR("terminalFont", "TerminalFont", terminal_font, ""),
    RSTR("textRendering", "TextRendering", text_rendering, "auto"),
    RSTR("terminalTextRendering", "TerminalTextRendering", terminal_text_rendering, "auto")
};
#undef RSTR
#undef ROFF

XrmOptionDescRec k_options[] = {
    { (char*)"-fn", (char*)".font", XrmoptionSepArg, 0 },
    { (char*)"-font", (char*)".font", XrmoptionSepArg, 0 },
    { (char*)"-renderer", (char*)".renderer", XrmoptionSepArg, 0 },
    { (char*)"-dynbg", (char*)".terminalBackground", XrmoptionSepArg, 0 },
    { (char*)"-text", (char*)".textRendering", XrmoptionSepArg, 0 }
};

/* The settings file's lines as the application's resources, under what
 * the command line says. */
void merge_settings(Widget top)
{
    ttr::SettingsFile file;
    if (!file.load(ttrcode::settings_path().c_str()))
        return;
    XrmDatabase db = XtDatabase(XtDisplay(top));
    XrmDatabase mine = 0;
    for (int i = 0; i < file.count(); i++) {
        char spec[200];
        if (strlen(file.name(i)) > 100)
            continue;
        sprintf(spec, "TTRcode.%s", file.name(i));
        XrmPutStringResource(&mine, spec, file.value(i));
    }
    /* Not over what is already there (the command line, -xrm). */
    if (mine)
        XrmCombineDatabase(mine, &db, False);
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
    th::set_program_path(argv[0]);
    merge_settings(top);
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
        fprintf(stderr, "usage: ttrcode [-fn font] [-renderer auto|core|soft|gl] [-dynbg name]\n"
                        "               [-text sharp|smooth|auto] [directory] [file ...]\n");
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
    o.scheme = res.scheme;
    o.ui_theme = res.ui_theme ? res.ui_theme : "";
    o.tree_width = res.tree_width > 40 ? res.tree_width : 220;
    o.bottom_height = res.bottom_height > 40 ? res.bottom_height : 220;
    o.renderer = res.renderer;
    o.auto_indent = res.auto_indent != False;
    o.show_whitespace = res.show_whitespace != False;
    o.highlight_line = res.highlight_line != False;
    o.cursor_blink = res.cursor_blink != False;
    o.ruler = res.ruler >= 0 ? res.ruler : 0;
    o.cursor_shape = res.cursor_shape ? res.cursor_shape : "bar";
    o.cursor_color = res.cursor_color ? res.cursor_color : "";
    o.terminal_background = res.terminal_background;
    o.terminal_font = res.terminal_font ? res.terminal_font : "";
    o.text_rendering = res.text_rendering ? res.text_rendering : "auto";
    o.terminal_text_rendering = res.terminal_text_rendering ? res.terminal_text_rendering : "auto";

    /* The UI theme, for every widget made from here on. */
    th::Theme theme;
    th::find_theme(o.ui_theme[0] ? o.ui_theme : ttrcode::ttr_theme(), &theme);
    XrmDatabase db = XtDatabase(dpy);
    th::apply_theme(&db, theme);

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
