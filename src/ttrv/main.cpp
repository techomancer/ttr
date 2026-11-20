/*
 * ttrv, the To The Roots image viewer (ttrv/DESIGN.md).
 *
 *   ttrv [-fullscreen] [-renderer auto|gl|soft] [X toolkit options] [folder | image]
 *
 * A folder opens in the browser; an image opens in the viewer (full
 * screen with -fullscreen), its folder behind it. Archives are folders.
 */
#include "app.h"

#include "../../th/motif.h"
#include "../../th/theme.h"
#include "../../tx/appicon.h"
#include "../../tx/scale.h"
#include "../../tx/ttr.h"
#include "core/settings.h"
#include "icon_ttrv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <Xm/Xm.h>

namespace {

struct Resources {
    String layout, list_font, ui_scale, ui_theme, image_visual, view_mode, renderer;
    int sort_key, cache_mb, thumb_size, texture_mb;
    Boolean sort_reverse, show_hidden, fullscreen, auto_rotate;
};

#define ROFF(f) XtOffsetOf(Resources, f)
#define RSTR(name, cls, f, dflt) \
    { (String)name, (String)cls, (String)XtRString, sizeof(String), ROFF(f), (String)XtRString, \
      (XtPointer)dflt }
#define RINT(name, cls, f, dflt) \
    { (String)name, (String)cls, (String)XtRInt, sizeof(int), ROFF(f), (String)XtRString, \
      (XtPointer)dflt }
#define RBOOL(name, cls, f, dflt) \
    { (String)name, (String)cls, (String)XtRBoolean, sizeof(Boolean), ROFF(f), \
      (String)XtRString, (XtPointer)dflt }
XrmOptionDescRec k_options[] = {
    { (char*)"-fullscreen", (char*)".fullScreen", XrmoptionNoArg, (XPointer) "true" },
    { (char*)"-renderer", (char*)".renderer", XrmoptionSepArg, (XPointer)0 }
};

XtResource k_resources[] = {
    RSTR("layout", "Layout", layout, ""),
    RSTR("listFont", "ListFont", list_font, ""),
    RSTR("uiScale", "UiScale", ui_scale, ""),
    RSTR("uiTheme", "UiTheme", ui_theme, ""),
    RSTR("imageVisual", "ImageVisual", image_visual, "auto"),
    RSTR("renderer", "Renderer", renderer, "auto"),
    RINT("textureMegabytes", "TextureMegabytes", texture_mb, "0"),
    RINT("sortKey", "SortKey", sort_key, "0"),
    RSTR("viewMode", "ViewMode", view_mode, "details"),
    RINT("thumbnailSize", "ThumbnailSize", thumb_size, "128"),
    RINT("cacheMegabytes", "CacheMegabytes", cache_mb, "0"),
    RBOOL("sortReverse", "SortReverse", sort_reverse, "false"),
    RBOOL("showHidden", "ShowHidden", show_hidden, "false"),
    RBOOL("fullScreen", "FullScreen", fullscreen, "false"),
    RBOOL("autoRotate", "AutoRotate", auto_rotate, "true")
};
#undef RSTR
#undef RINT
#undef RBOOL
#undef ROFF

/* The settings file's lines as the application's resources, under what
 * the command line says. */
void merge_settings(Widget top)
{
    ttr::SettingsFile file;
    if (!file.load(ttrv::settings_path().c_str()))
        return;
    XrmDatabase db = XtDatabase(XtDisplay(top));
    XrmDatabase mine = 0;
    for (int i = 0; i < file.count(); i++) {
        char spec[200];
        if (strlen(file.name(i)) > 100)
            continue;
        sprintf(spec, "TTRv.%s", file.name(i));
        XrmPutStringResource(&mine, spec, file.value(i));
    }
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
        (String)"*TTRv.geometry: 1000x720",
        0
    };
    XtAppContext ctx;
    Widget top = XtAppInitialize(&ctx, "TTRv", k_options, XtNumber(k_options), &argc, argv,
                                 fallbacks, 0, 0);
    merge_settings(top);
    Resources res;
    XtGetApplicationResources(top, &res, k_resources, XtNumber(k_resources), 0, 0);
    /* More than one path (files dropped on the IRIX desktop icon): the
     * first. */
    for (int i = 1; i < argc; i++)
        if (argv[i][0] == '-') {
            fprintf(stderr, "usage: ttrv [-fullscreen] [-renderer auto|gl|soft] "
                            "[X toolkit options] [folder | image]\n");
            return 2;
        }

    static const tx::ScaledFont fonts[] = {
        { "*fontList", "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1" },
        { "*menuBar*fontList", "-*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1" },
        { "*tabs*fontList", "-*-helvetica-medium-r-normal--12-*-*-*-*-*-iso8859-1" }
    };
    int scale = tx::ui_scale_of(res.ui_scale);
    tx::apply_ui_scale(top, scale, fonts, (int)(sizeof(fonts) / sizeof(fonts[0])));
    th::Theme theme;
    th::find_theme(res.ui_theme && *res.ui_theme ? res.ui_theme : tx::ttr_theme(), &theme);
    XrmDatabase db = XtDatabase(XtDisplay(top));
    th::apply_theme(&db, theme);

    /* The start: an absolute path. */
    static char start[4200];
    start[0] = 0;
    if (argc == 2) {
        if (argv[1][0] == '/') {
            snprintf(start, sizeof(start), "%s", argv[1]);
        } else {
            char cwd[2048];
            if (!getcwd(cwd, sizeof(cwd)))
                strcpy(cwd, "/");
            snprintf(start, sizeof(start), "%s/%s", cwd, argv[1]);
        }
        size_t l = strlen(start);
        while (l > 1 && start[l - 1] == '/')
            start[--l] = 0;
    }

    ttrv::ImageView::set_visual_choice(res.image_visual);
    ttrv::ImageView::set_renderer(res.renderer, res.texture_mb);
    ttrv::Options o;
    o.start = start;
    o.layout = res.layout ? res.layout : "";
    o.list_font = res.list_font ? res.list_font : "";
    o.sort_key = res.sort_key;
    o.sort_reverse = res.sort_reverse != False;
    o.show_hidden = res.show_hidden != False;
    o.cache_mb = res.cache_mb;
    o.fullscreen = res.fullscreen != False;
    o.auto_rotate = res.auto_rotate != False;
    o.thumbnails = res.view_mode && strcmp(res.view_mode, "thumbnails") == 0;
    o.thumb_size = res.thumb_size;
    XtVaSetValues(top, XmNtitle, "ttrv", XmNiconName, "ttrv", (char*)0);
    ttrv::App* app = new ttrv::App(top, o);
    XtRealizeWidget(top);
    tx::set_app_icon(top, k_icon_ttrv);
    app->realized();
    XtAppMainLoop(ctx);
    return 0;
}
