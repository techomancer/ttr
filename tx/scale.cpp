/*
 * tx/scale.cpp - fonts and Motif's pixel sizes at the UI scale
 * (tx/scale.h).
 */
#include "scale.h"
#include "pages.h"
#include "ttr.h"

#include "core/scale.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xatom.h>
#include <Xm/Xm.h>
#include <Xm/ComboBox.h>
#include <Xm/List.h>

using ttr::Str;
using ttr::scaled;
using ttr::scaled_line;

namespace tx {

namespace {

/* Split an XLFD into its 14 fields; false if it is not one. */
bool xlfd_split(const char* name, char fields[14][128])
{
    if (name[0] != '-')
        return false;
    const char* p = name + 1;
    int f;
    for (f = 0; f < 14; f++) {
        const char* e = strchr(p, '-');
        if (f == 13)
            e = p + strlen(p);
        if (!e || e - p >= 128)
            return false;
        memcpy(fields[f], p, e - p);
        fields[f][e - p] = 0;
        p = *e ? e + 1 : e;
    }
    return true;
}

Str xlfd_join(char fields[14][128])
{
    char buf[14 * 129 + 2];
    char* o = buf;
    int f;
    for (f = 0; f < 14; f++) {
        *o++ = '-';
        strcpy(o, fields[f]);
        o += strlen(o);
    }
    *o = 0;
    return Str(buf);
}

bool loadable(Display* dpy, const char* name)
{
    XFontStruct* f = XLoadQueryFont(dpy, name);
    if (f)
        XFreeFont(dpy, f);
    return f != 0;
}

/* The full XLFD of an alias such as "fixed", from its FONT property, or
 * empty. */
Str alias_xlfd(Display* dpy, const char* name)
{
    XFontStruct* f = XLoadQueryFont(dpy, name);
    if (!f)
        return Str("");
    Str r("");
    unsigned long atom = 0;
    if (XGetFontProperty(f, XA_FONT, &atom) && atom) {
        char* n = XGetAtomName(dpy, (Atom)atom);
        if (n) {
            r = n;
            XFree(n);
        }
    }
    XFreeFont(dpy, f);
    return r;
}

/* Families to try, in order, when the one asked for is not installed or
 * has no size near the one wanted: common sans serif fonts (bitmap, or
 * scalable through the X server's font path), then the misc fixed fonts,
 * then anything. */
const char* const k_substitutes[] = {
    "helvetica", "nimbus sans", "nimbus sans l", "liberation sans",
    "dejavu sans", "arial", "luxi sans", "bitstream vera sans", "noto sans",
    "droid sans", "fixed", "*", 0
};

/* The installed font nearest to 'target' pixels with these fields (the
 * size fields are replaced). *dist is how far off it is, -1 if there is
 * none. */
Str nearest_font(Display* dpy, char fields[14][128], int target,
                 int* dist)
{
    char f[14][128];
    memcpy(f, fields, sizeof(f));
    sprintf(f[6], "%d", target);
    strcpy(f[7], "*");
    strcpy(f[8], "*");
    strcpy(f[9], "*");
    strcpy(f[11], "*");
    Str want = xlfd_join(f);
    if (loadable(dpy, want.c_str())) {
        *dist = 0;      /* that size, or a scalable font */
        return want;
    }

    /* Bitmap fonts come in a few sizes: take the nearest one there is. */
    strcpy(f[6], "*");
    Str pattern = xlfd_join(f);
    int n = 0;
    char** names = XListFonts(dpy, pattern.c_str(), 2000, &n);
    Str best("");
    int best_d = -1, i;
    for (i = 0; i < n; i++) {
        char f2[14][128];
        if (!xlfd_split(names[i], f2))
            continue;
        int size = atoi(f2[6]);
        if (size <= 0)
            continue;
        int d = size > target ? size - target : target - size;
        if (best_d < 0 || d < best_d) {
            best_d = d;
            best = names[i];
        }
    }
    if (names)
        XFreeFontNames(names);
    *dist = best_d;
    return best;
}

/* A font that can be loaded, as near as there is to 'name' at 'percent'
 * of its size. Fields are numbered from 0 here: 1 is the family, 2 the
 * weight, 6 the pixel size, 7 the point size (in tenths). */
} /* namespace */

Str scale_font(Display* dpy, const char* name, int percent, const char* who)
{
    if (percent == 100 && loadable(dpy, name))
        return Str(name);   /* as before the scale existed */
    char fields[14][128];
    if (!xlfd_split(name, fields)) {
        Str real = alias_xlfd(dpy, name);
        if (real.empty() || !xlfd_split(real.c_str(), fields))
            return Str(name);
    }

    int px = atoi(fields[6]), pt = atoi(fields[7]);
    /* Without a pixel size, take points (in tenths) as pixels. */
    int base = px > 0 ? px : pt > 0 ? (pt + 5) / 10 : 12;
    int target = (base * percent + 50) / 100;
    int close = target / 10;    /* near enough to keep the family */

    /* The family asked for, then the substitutes; all of them first in
     * the weight asked for, then in any weight. */
    Str best("");
    int best_d = -1, pass, i;
    for (pass = 0; pass < 2; pass++) {
        for (i = -1; i < 0 || k_substitutes[i]; i++) {
            char f[14][128];
            memcpy(f, fields, sizeof(f));
            if (i >= 0) {
                strcpy(f[0], "*");
                strcpy(f[1], k_substitutes[i]);
            }
            if (pass == 1)
                strcpy(f[2], "*");
            strcpy(f[3], "r");
            int d = -1;
            Str got = nearest_font(dpy, f, target, &d);
            if (d >= 0 && (best_d < 0 || d < best_d)) {
                best_d = d;
                best = got;
            }
            if (best_d >= 0 && best_d <= close)
                break;
        }
        if (best_d >= 0 && best_d <= close)
            break;
    }
    if (best.empty())
        return Str(name);

    if (who) {
        char bf[14][128];
        int size = xlfd_split(best.c_str(), bf) ? atoi(bf[6]) : 0;
        if (size > 0 && size < target * 8 / 10)
            fprintf(stderr, "%s: no font near %d pixels is installed "
                            "(using %d); see README.md, Fonts\n",
                    who, target, size);
        else if (size > 0 && strcmp(bf[1], fields[1]) != 0)
            fprintf(stderr, "%s: font family '%s' is not installed, "
                            "using '%s'\n", who, fields[1], bf[1]);
    }
    return best;
}

/* Motif sizes its widgets from their fonts, but shadows, focus
 * highlights and scroll bars are in pixels. Scaled per class, at Motif's
 * defaults, so classes that have none (labels, layout managers) do not
 * get one. */
void scale_motif_resources(XrmDatabase* db)
{
    const struct { const char* spec; int base; } k_res[] = {
        { "*XmPushButton.shadowThickness", 2 },
        { "*XmPushButtonGadget.shadowThickness", 2 },
        { "*XmCascadeButton.shadowThickness", 2 },
        { "*XmCascadeButtonGadget.shadowThickness", 2 },
        { "*XmArrowButton.shadowThickness", 2 },
        { "*XmText.shadowThickness", 2 },
        { "*XmTextField.shadowThickness", 2 },
        { "*XmList.shadowThickness", 2 },
        { "*XmScrollBar.shadowThickness", 2 },
        { "*XmScale.shadowThickness", 2 },
        { "*XmComboBox.shadowThickness", 2 },
        { "*XmMenuShell*XmRowColumn.shadowThickness", 2 },
        { "*XmPushButton.highlightThickness", 2 },
        { "*XmToggleButton.highlightThickness", 2 },
        { "*XmText.highlightThickness", 2 },
        { "*XmTextField.highlightThickness", 2 },
        { "*XmList.highlightThickness", 2 },
        { "*XmComboBox.arrowSize", 14 }
    };
    int i;
    for (i = 0; i < (int)(sizeof(k_res) / sizeof(k_res[0])); i++) {
        char v[16];
        int base = k_res[i].base;
        sprintf(v, "%d", strstr(k_res[i].spec, "Thickness") ? scaled_line(base)
                                                             : scaled(base));
        XrmPutStringResource(db, k_res[i].spec, v);
    }
}
const char* const k_ui_scales[] = { "100", "125", "150", "175", "200", "250", "300" };
const int k_ui_scale_count = (int)(sizeof(k_ui_scales) / sizeof(k_ui_scales[0]));

int ui_scale_of(const char* setting)
{
    int s = setting ? atoi(setting) : 0;
    if (s <= 0)
        return ttr_ui_scale();
    return s < ttr::k_min_scale ? ttr::k_min_scale : s > ttr::k_max_scale ? ttr::k_max_scale : s;
}

Widget scale_drop_down(Widget parent, const char* setting)
{
    char labels[16][16];
    const char* items[17];
    int n = 0, i, now = setting ? atoi(setting) : 0;
    items[n++] = "Same as ttr";
    for (i = 0; i < k_ui_scale_count && i < 15; i++) {
        sprintf(labels[i], "%.8s%%", k_ui_scales[i]);
        items[n++] = labels[i];
    }
    Widget w = drop_down(parent, "uiScale", items, n, n, 10);
    char want[16];
    if (now > 0)
        sprintf(want, "%d%%", now);
    else
        strcpy(want, items[0]);
    XmString s = XmStringCreateLocalized(want);
    Widget list = XtNameToWidget(w, "*List");
    if (list && !XmListItemExists(list, s))
        XmComboBoxAddItem(w, s, 0, False);     /* set by hand: kept */
    XmComboBoxSelectItem(w, s);
    XmStringFree(s);
    return w;
}

ttr::Str scale_drop_down_value(Widget w)
{
    Widget list = XtNameToWidget(w, "*List");
    int* pos = 0;
    int n = 0;
    XmStringTable items = 0;
    int count = 0;
    if (!list)
        return Str("");
    XtVaGetValues(list, XmNselectedPositions, &pos, XmNselectedPositionCount, &n, XmNitems,
                  &items, XmNitemCount, &count, (char*)0);
    if (!pos || n < 1 || pos[0] <= 1 || pos[0] > count)
        return Str("");     /* "Same as ttr" */
    char* t = (char*)XmStringUnparse(items[pos[0] - 1], 0, XmCHARSET_TEXT, XmCHARSET_TEXT, 0, 0,
                                     XmOUTPUT_ALL);
    char v[16];
    sprintf(v, "%d", t ? atoi(t) : 100);
    XtFree(t);
    return Str(v);
}

void apply_ui_scale(Widget top, int percent, const ScaledFont* fonts, int count)
{
    ttr::set_ui_scale(percent);
    if (percent == 100)
        return;
    Display* dpy = XtDisplay(top);
    XrmDatabase db = XtDatabase(dpy);
    for (int i = 0; i < count; i++)
        XrmPutStringResource(&db, fonts[i].spec,
                             scale_font(dpy, fonts[i].xlfd, percent).c_str());
    /* ttr's 17 pixels (x11/config.cpp), at the scale. */
    char w[16];
    sprintf(w, "%d", scaled(17));
    XrmPutStringResource(&db, "*XmScrollBar.width", w);
    XrmPutStringResource(&db, "*XmScrollBar.height", w);
    XrmPutStringResource(&db, "*XmScale.scaleHeight", w);
    scale_motif_resources(&db);
}

} /* namespace tx */
