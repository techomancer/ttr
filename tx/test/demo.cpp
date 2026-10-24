/*
 * tx_demo: the font chooser as ttrt (fixed-width, Unicode, Automatic)
 * and ttr's settings (any family, Bold) have it, for looking at.
 *
 *   tx_demo [font]
 */
#include "../fontchooser.h"

#include <stdio.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/Xm.h>

namespace {

void changed(tx::FontChooser* c, void* data)
{
    printf("%s: %s\n", (const char*)data, c->get().c_str());
    fflush(stdout);
}

} /* namespace */

int main(int argc, char** argv)
{
    XtAppContext app;
    Widget top = XtVaAppInitialize(&app, "TxDemo", 0, 0, &argc, argv, 0, (char*)0);
    Widget col = XtVaCreateManagedWidget("col", xmRowColumnWidgetClass, top, XmNorientation,
                                         XmVERTICAL, XmNspacing, 10, XmNmarginWidth, 10,
                                         XmNmarginHeight, 10, (char*)0);
    XtVaCreateManagedWidget("Terminal font (fixed width):", xmLabelWidgetClass, col, (char*)0);
    tx::FontChooser term(col, "term", tx::FontChooser::FIXED | tx::FontChooser::UNICODE |
                                          tx::FontChooser::AUTOMATIC);
    term.set_automatic_preview("fixed");
    term.set(argc > 1 ? argv[1] : "");
    term.on_changed(changed, (void*)"terminal");
    XtVaCreateManagedWidget("Window titles (any family, bold):", xmLabelWidgetClass, col,
                            (char*)0);
    tx::FontChooser ui(col, "ui", tx::FontChooser::BOLD);
    ui.set("-*-helvetica-bold-r-normal--12-*-*-*-*-*-iso8859-1");
    ui.on_changed(changed, (void*)"titles");
    XtRealizeWidget(top);
    XtAppMainLoop(app);
    return 0;
}
