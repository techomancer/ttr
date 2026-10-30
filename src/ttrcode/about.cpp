/*
 * Help > About ttrcode: what it is, ttr's license, and the libraries it
 * includes (with the notices their licenses ask binaries to carry) or
 * uses from the system.
 */
#include "app.h"

#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/PushB.h>
#include <Xm/Text.h>

namespace ttrcode {

namespace {

const char k_intro[] =
    "ttrcode, the To The Roots code editor, with a debugger in the manner of\n"
    "Visual C++ 6. Part of ttr: Motif, te, tw and td.\n"
    "Copyright (c) 2026, Dominik Behr. BSD 3-Clause License.";

const char k_mit[] =
    "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
    "of this software and associated documentation files (the \"Software\"), to deal\n"
    "in the Software without restriction, including without limitation the rights\n"
    "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
    "copies of the Software, and to permit persons to whom the Software is\n"
    "furnished to do so, subject to the following conditions:\n"
    "\n"
    "The above copyright notice and this permission notice shall be included in all\n"
    "copies or substantial portions of the Software.\n"
    "\n"
    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
    "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
    "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
    "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
    "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n"
    "SOFTWARE.\n";

const char k_bsd[] =
    "Redistribution and use in source and binary forms, with or without\n"
    "modification, are permitted provided that the following conditions are met:\n"
    "\n"
    "1. Redistributions of source code must retain the above copyright notice, this\n"
    "   list of conditions and the following disclaimer.\n"
    "\n"
    "2. Redistributions in binary form must reproduce the above copyright notice,\n"
    "   this list of conditions and the following disclaimer in the documentation\n"
    "   and/or other materials provided with the distribution.\n"
    "\n"
    "3. Neither the name of the copyright holder nor the names of its\n"
    "   contributors may be used to endorse or promote products derived from\n"
    "   this software without specific prior written permission.\n"
    "\n"
    "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS \"AS IS\"\n"
    "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE\n"
    "IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE\n"
    "DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE\n"
    "FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\n"
    "DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR\n"
    "SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER\n"
    "CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,\n"
    "OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE\n"
    "OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n";

/* The credits, then each license in full. */
void credits(td::Text* t)
{
    td::Text& s = *t;
    s.add("INCLUDED IN TTRCODE\n"
         "\n"
         "libdwarf - reads the programs' debugging information (DWARF).\n"
         "  https://github.com/davea42/libdwarf-code\n"
         "  Copyright (C) Silicon Graphics, Inc., David Anderson, SN Systems Ltd.,\n"
         "  Arxan Technologies, Inc., Google, Inc.\n"
         "  GNU Lesser General Public License 2.1. Its source is in ttr's\n"
         "  third_party/libdwarf; ttrcode can be rebuilt from ttr's sources with\n"
         "  another libdwarf.\n"
         "\n");
#ifndef __sgi
    s.add("Zydis and Zycore - x86-64 instructions as text (Disassembly).\n"
         "  https://github.com/zyantific/zydis\n"
         "  Copyright (c) 2014-2024 Florian Bernd, Joel Hoener. MIT License (below).\n"
         "\n");
#endif
    s.add("stb_image - reads PNG, JPEG, GIF and other images (icons, themes).\n"
         "  https://github.com/nothings/stb\n"
         "  Copyright (c) 2017 Sean Barrett. MIT License (below), or public domain.\n"
         "\n"
         "USED FROM THE SYSTEM\n"
         "\n"
         "Motif, the X Toolkit and Xlib (The Open Group, X.Org), libXpm, libpng,\n"
         "libjpeg, giflib and libtiff, where the system has them. This software is\n"
         "based in part on the work of the Independent JPEG Group.\n"
         "\n"
         "------------------------------------------------------------------------------\n"
         "ttr - BSD 3-Clause License\n"
         "\n"
         "Copyright (c) 2026, Dominik Behr\n"
         "\n");
    s.add(k_bsd);
#ifndef __sgi
    s.add("\n"
         "------------------------------------------------------------------------------\n"
         "Zydis - The MIT License (MIT)\n"
         "\n"
         "Copyright (c) 2014-2024 Florian Bernd\n"
         "Copyright (c) 2014-2024 Joel Hoener\n"
         "\n");
    s.add(k_mit);
    s.add("\n"
         "------------------------------------------------------------------------------\n"
         "Zycore - The MIT License (MIT)\n"
         "\n"
         "Copyright (c) 2018-2024 Florian Bernd\n"
         "Copyright (c) 2018-2024 Joel Hoener\n"
         "\n");
    s.add(k_mit);
#endif
    s.add("\n"
         "------------------------------------------------------------------------------\n"
         "stb_image - MIT License\n"
         "\n"
         "Copyright (c) 2017 Sean Barrett\n"
         "\n");
    s.add(k_mit);
    s.add("\n"
         "------------------------------------------------------------------------------\n"
         "libdwarf - GNU Lesser General Public License 2.1\n"
         "\n"
         "This library is free software; you can redistribute it and/or modify it\n"
         "under the terms of version 2.1 of the GNU Lesser General Public License as\n"
         "published by the Free Software Foundation. It is distributed in the hope\n"
         "that it would be useful, but WITHOUT ANY WARRANTY; without even the implied\n"
         "warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. The\n"
         "license's full text is in ttr's third_party/libdwarf/src/lib/libdwarf/\n"
         "LGPL.txt, and at https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html\n");
}

void about_ok_cb(Widget, XtPointer data, XtPointer)
{
    XtUnmanageChild((Widget)data);
}

} /* namespace */

void App::show_about()
{
    if (!about_dlg_) {
        Arg a[1];
        XtSetArg(a[0], XmNautoUnmanage, False);
        about_dlg_ = XmCreateFormDialog(top_, (char*)"about", a, 1);
        XtVaSetValues(XtParent(about_dlg_), XmNtitle, "About ttrcode", (char*)0);
        XtVaSetValues(about_dlg_, XmNmarginWidth, scaled(10), XmNmarginHeight, scaled(10),
                      (char*)0);
        XmString s = XmStringGenerate((XtPointer)k_intro, (char*)XmFONTLIST_DEFAULT_TAG,
                                      XmCHARSET_TEXT, 0);    /* newlines: separate lines */
        Widget intro = XtVaCreateManagedWidget("intro", xmLabelWidgetClass, about_dlg_,
                                               XmNlabelString, s, XmNalignment,
                                               XmALIGNMENT_BEGINNING,
                                               XmNtopAttachment, XmATTACH_FORM,
                                               XmNleftAttachment, XmATTACH_FORM, (char*)0);
        XmStringFree(s);
        Widget ok = XtVaCreateManagedWidget("OK", xmPushButtonWidgetClass, about_dlg_,
                                            XmNwidth, scaled(80), XmNrightAttachment, XmATTACH_FORM,
                                            XmNbottomAttachment, XmATTACH_FORM, (char*)0);
        XtAddCallback(ok, XmNactivateCallback, about_ok_cb, (XtPointer)about_dlg_);
        Arg t[8];
        Cardinal n = 0;
        XtSetArg(t[n], XmNeditMode, XmMULTI_LINE_EDIT); n++;
        XtSetArg(t[n], XmNeditable, False); n++;
        XtSetArg(t[n], XmNcursorPositionVisible, False); n++;
        XtSetArg(t[n], XmNrows, 18); n++;
        XtSetArg(t[n], XmNcolumns, 80); n++;
        XtSetArg(t[n], XmNwordWrap, False); n++;
        Widget text = XmCreateScrolledText(about_dlg_, (char*)"credits", t, n);
        XtVaSetValues(XtParent(text), XmNtopAttachment, XmATTACH_WIDGET, XmNtopWidget, intro,
                      XmNtopOffset, scaled(10), XmNleftAttachment, XmATTACH_FORM,
                      XmNrightAttachment, XmATTACH_FORM, XmNbottomAttachment, XmATTACH_WIDGET,
                      XmNbottomWidget, ok, XmNbottomOffset, scaled(10), (char*)0);
        td::Text c;
        credits(&c);
        XmTextSetString(text, (char*)c.c_str());
        XmTextShowPosition(text, 0);
        XtManageChild(text);
        XtVaSetValues(about_dlg_, XmNdefaultButton, ok, XmNcancelButton, ok, (char*)0);
    }
    XtManageChild(about_dlg_);
}

} /* namespace ttrcode */
