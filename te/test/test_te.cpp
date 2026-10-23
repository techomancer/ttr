/* Tests for te: the document (edits, undo, search) and the view (moves,
 * editing commands, layout). */
#include "../document.h"
#include "../view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failures;

#define CHECK(c) check((c), __FILE__, __LINE__, #c)
#define CHECK_TEXT(d, want) check_text((d), (want), __FILE__, __LINE__)
#define CHECK_POS(p, l, c) check_pos((p), (l), (c), __FILE__, __LINE__)

static void check(bool ok, const char* file, int line, const char* what)
{
    g_checks++;
    if (!ok) {
        g_failures++;
        printf("%s:%d: failed: %s\n", file, line, what);
    }
}

static void check_text(const te::Document& d, const char* want, const char* file, int line)
{
    size_t n;
    char* t = d.text(&n);
    g_checks++;
    if (n != strlen(want) || memcmp(t, want, n) != 0) {
        g_failures++;
        printf("%s:%d: got \"%.*s\", want \"%s\"\n", file, line, (int)n, t, want);
    }
    free(t);
}

static void check_pos(te::Pos p, long l, size_t c, const char* file, int line)
{
    g_checks++;
    if (p.line != l || p.col != c) {
        g_failures++;
        printf("%s:%d: got %ld:%lu, want %ld:%lu\n", file, line, p.line, (unsigned long)p.col, l,
               (unsigned long)c);
    }
}

static void load(te::Document& d, const char* s)
{
    d.set_text(s, strlen(s));
}

static void test_document()
{
    te::Document d;
    CHECK(d.lines() == 1);
    CHECK_TEXT(d, "");
    load(d, "one\ntwo\nthree\n");
    CHECK(d.lines() == 4);
    CHECK(d.length(2) == 5);
    CHECK(!d.modified());
    CHECK_TEXT(d, "one\ntwo\nthree\n");

    te::Pos e = d.insert(te::Pos(1, 1), "XX\nYY", 5);
    CHECK_POS(e, 2, 2);
    CHECK_TEXT(d, "one\ntXX\nYYwo\nthree\n");
    CHECK(d.modified());
    d.erase(te::Pos(1, 1), te::Pos(2, 2));
    CHECK_TEXT(d, "one\ntwo\nthree\n");

    size_t n;
    char* g = d.get(te::Pos(0, 1), te::Pos(1, 2), &n);
    CHECK(n == 5 && memcmp(g, "ne\ntw", 5) == 0);
    free(g);

    /* Undo and redo; back at the saved state counts as unmodified. */
    te::Pos c;
    CHECK(d.undo(&c));
    CHECK_TEXT(d, "one\ntXX\nYYwo\nthree\n");
    CHECK(d.undo(&c));
    CHECK_TEXT(d, "one\ntwo\nthree\n");
    CHECK(!d.modified());
    CHECK(!d.undo(&c));
    CHECK(d.redo(&c));
    CHECK(d.modified());
    CHECK(d.redo(&c));
    CHECK(!d.redo(&c));
    d.mark_saved();
    CHECK(!d.modified());

    /* CRLF files stay CRLF; mixed ones are taken as LF. */
    load(d, "a\r\nb\r\n");
    CHECK(d.crlf());
    CHECK(d.length(0) == 1);
    CHECK_TEXT(d, "a\r\nb\r\n");
    load(d, "a\r\nb\n");
    CHECK(!d.crlf());

    /* Clamp. */
    load(d, "ab\ncd");
    CHECK_POS(d.clamp(te::Pos(5, 9)), 1, 2);
    CHECK_POS(d.clamp(te::Pos(-1, 9)), 0, 0);
    CHECK_POS(d.end(), 1, 2);
}

static void test_typing_merge()
{
    te::Document d;
    te::View v(d);
    v.type("a", 1);
    v.type("b", 1);
    v.type("c", 1);
    CHECK_TEXT(d, "abc");
    CHECK(v.undo());
    CHECK_TEXT(d, "");
    CHECK(v.redo());
    CHECK_TEXT(d, "abc");
    /* A jump stops merging. */
    v.move(te::MOVE_HOME, false);
    v.type("x", 1);
    CHECK_TEXT(d, "xabc");
    CHECK(v.undo());
    CHECK_TEXT(d, "abc");
}

static void test_find()
{
    te::Document d;
    load(d, "Foo bar\nfoo baz\nFOO\n");
    te::Pos a, b;
    bool wrapped;
    CHECK(d.find("foo", te::Pos(0, 0), true, false, &a, &b, &wrapped));
    CHECK_POS(a, 0, 0);
    CHECK(!wrapped);
    CHECK(d.find("foo", te::Pos(0, 1), true, true, &a, &b, &wrapped));
    CHECK_POS(a, 1, 0);
    CHECK(d.find("foo", te::Pos(1, 1), true, false, &a, &b, &wrapped));
    CHECK_POS(a, 2, 0);
    CHECK(d.find("foo", te::Pos(2, 1), true, false, &a, &b, &wrapped));
    CHECK_POS(a, 0, 0);
    CHECK(wrapped);
    CHECK(d.find("foo", te::Pos(1, 0), false, false, &a, &b, &wrapped));
    CHECK_POS(a, 0, 0);
    CHECK(!d.find("nope", te::Pos(0, 0), true, false, &a, &b, &wrapped));

    CHECK(d.replace_all("foo", "qux", false) == 3);
    CHECK_TEXT(d, "qux bar\nqux baz\nqux\n");
    te::Pos c;
    CHECK(d.undo(&c));
    CHECK_TEXT(d, "Foo bar\nfoo baz\nFOO\n");
}

static void test_moves()
{
    te::Document d;
    load(d, "    indented line\nshort\n\tx\n");
    te::View v(d);
    v.set_size(40, 10);

    v.move(te::MOVE_END, false);
    CHECK_POS(v.cursor(), 0, 17);
    v.move(te::MOVE_HOME, false);
    CHECK_POS(v.cursor(), 0, 4);
    v.move(te::MOVE_HOME, false);
    CHECK_POS(v.cursor(), 0, 0);

    /* Up and Down keep the column across short lines. */
    v.set_cursor(te::Pos(0, 12), false);
    v.move(te::MOVE_DOWN, false);
    CHECK_POS(v.cursor(), 1, 5);
    v.move(te::MOVE_DOWN, false);
    CHECK_POS(v.cursor(), 2, 2);    /* a tab is 8 columns: past it */
    v.move(te::MOVE_UP, false);
    v.move(te::MOVE_UP, false);
    CHECK_POS(v.cursor(), 0, 12);

    /* Words. */
    v.set_cursor(te::Pos(0, 0), false);
    v.move(te::MOVE_WORD_RIGHT, false);
    CHECK_POS(v.cursor(), 0, 4);
    v.move(te::MOVE_WORD_RIGHT, false);
    CHECK_POS(v.cursor(), 0, 13);
    v.move(te::MOVE_WORD_LEFT, false);
    CHECK_POS(v.cursor(), 0, 4);
    v.move(te::MOVE_WORD_LEFT, false);
    CHECK_POS(v.cursor(), 0, 0);

    /* Left and Right cross lines; UTF-8 is one step. */
    v.move(te::MOVE_LEFT, false);
    CHECK_POS(v.cursor(), 0, 0);
    v.set_cursor(te::Pos(1, 0), false);
    v.move(te::MOVE_LEFT, false);
    CHECK_POS(v.cursor(), 0, 17);
    v.move(te::MOVE_RIGHT, false);
    CHECK_POS(v.cursor(), 1, 0);

    load(d, "a\xc3\xa9z");
    v.set_cursor(te::Pos(0, 1), false);
    v.move(te::MOVE_RIGHT, false);
    CHECK_POS(v.cursor(), 0, 3);
    v.move(te::MOVE_LEFT, false);
    CHECK_POS(v.cursor(), 0, 1);
    v.set_cursor(te::Pos(0, 3), false);
    v.backspace();
    CHECK_TEXT(d, "az");

    /* Selection with Shift; Left collapses to its start. */
    load(d, "hello world");
    v.set_cursor(te::Pos(0, 0), false);
    v.move(te::MOVE_WORD_RIGHT, true);
    CHECK(v.has_selection());
    size_t n;
    char* t = v.copy(&n);
    CHECK(n == 6 && memcmp(t, "hello ", 6) == 0);
    free(t);
    v.move(te::MOVE_LEFT, false);
    CHECK(!v.has_selection());
    CHECK_POS(v.cursor(), 0, 0);
    v.move(te::MOVE_DOC_END, false);
    CHECK_POS(v.cursor(), 0, 11);
}

static void test_editing()
{
    te::Document d;
    te::View v(d);
    v.set_size(40, 10);

    load(d, "    abc");
    v.move(te::MOVE_END, false);
    v.newline();
    CHECK_TEXT(d, "    abc\n    ");
    v.type("d", 1);
    CHECK(v.undo());
    CHECK(v.undo());
    CHECK_TEXT(d, "    abc");

    /* Typing replaces the selection, as one undo step. */
    load(d, "one two");
    v.set_cursor(te::Pos(0, 4), false);
    v.set_cursor(te::Pos(0, 7), true);
    v.type("2", 1);
    CHECK_TEXT(d, "one 2");
    CHECK(v.undo());
    CHECK_TEXT(d, "one two");

    /* Backspace and Delete join lines. */
    load(d, "ab\ncd");
    v.set_cursor(te::Pos(1, 0), false);
    v.backspace();
    CHECK_TEXT(d, "abcd");
    CHECK_POS(v.cursor(), 0, 2);
    v.move(te::MOVE_END, false);
    v.del();
    CHECK_TEXT(d, "abcd");

    /* Tab and indenting lines. */
    load(d, "a\nb\nc");
    v.set_cursor(te::Pos(0, 0), false);
    v.tab(false);
    CHECK_TEXT(d, "\ta\nb\nc");
    v.insert_spaces = true;
    v.tab_width = 4;
    v.tab(false);
    CHECK_TEXT(d, "\t    a\nb\nc");     /* the tab is 4 columns now: 4 to 8 */
    /* Tabs shown 8 wide, indenting by 4 spaces. */
    load(d, "x");
    v.tab_width = 8;
    v.indent_width = 4;
    v.set_cursor(te::Pos(0, 0), false);
    v.tab(false);
    CHECK_TEXT(d, "    x");
    v.tab(false);
    CHECK_TEXT(d, "        x");
    v.tab(true);
    CHECK_TEXT(d, "    x");
    v.indent_width = 0;
    v.insert_spaces = false;
    v.tab_width = 8;
    load(d, "a\nb\nc");
    v.set_cursor(te::Pos(0, 0), false);
    v.set_cursor(te::Pos(2, 0), true);
    v.tab(false);
    CHECK_TEXT(d, "\ta\n\tb\nc");
    v.tab(true);
    CHECK_TEXT(d, "a\nb\nc");
    v.tab(false);
    CHECK(v.undo());
    CHECK_TEXT(d, "a\nb\nc");

    /* Shift+Tab on one line. */
    load(d, "        x");
    v.set_cursor(te::Pos(0, 9), false);
    v.tab(true);
    CHECK_TEXT(d, "x");
    CHECK_POS(v.cursor(), 0, 1);

    /* Delete line. */
    load(d, "1\n2\n3");
    v.set_cursor(te::Pos(1, 0), false);
    v.delete_line();
    CHECK_TEXT(d, "1\n3");
    v.move(te::MOVE_DOC_END, false);
    v.delete_line();
    CHECK_TEXT(d, "1");
    v.delete_line();
    CHECK_TEXT(d, "");

    /* Cut and paste. */
    load(d, "abc def");
    v.set_cursor(te::Pos(0, 0), false);
    v.set_cursor(te::Pos(0, 4), true);
    size_t n;
    char* t = v.cut(&n);
    CHECK_TEXT(d, "def");
    v.move(te::MOVE_END, false);
    v.paste(t, n);
    free(t);
    CHECK_TEXT(d, "defabc ");
    CHECK(v.undo());
    CHECK_TEXT(d, "def");
}

static void test_blocks()
{
    te::Document d;
    te::View v(d);
    v.set_size(40, 10);
    /* Mark "xyz", move the cursor away: the block stays. */
    load(d, "ABC xyz\n");
    v.set_cursor(te::Pos(0, 4), false);
    v.set_cursor(te::Pos(0, 7), true);
    v.mark_block();
    CHECK(v.has_block());
    CHECK(!v.has_selection());
    v.move(te::MOVE_HOME, false);
    v.move(te::MOVE_DOWN, false);
    CHECK(v.has_block());
    CHECK(v.copy_block());
    CHECK_TEXT(d, "ABC xyz\nxyz");
    te::Pos a, b;
    v.block(&a, &b);            /* the copy is the block now */
    CHECK_POS(a, 1, 0);
    CHECK_POS(b, 1, 3);
    CHECK(v.undo());
    CHECK_TEXT(d, "ABC xyz\n");
    CHECK(!v.has_block());      /* undo is an edit */

    /* Move forward and back. */
    load(d, "ab\nCD\nef");
    v.set_cursor(te::Pos(0, 0), false);
    v.set_cursor(te::Pos(1, 0), true);
    v.mark_block();
    v.move(te::MOVE_DOC_END, false);
    CHECK(v.move_block());
    CHECK_TEXT(d, "CD\nefab\n");
    v.block(&a, &b);
    CHECK_POS(a, 1, 2);
    CHECK_POS(b, 2, 0);
    v.move(te::MOVE_DOC_START, false);
    CHECK(v.move_block());
    CHECK_TEXT(d, "ab\nCD\nef");
    CHECK(v.undo());
    CHECK_TEXT(d, "CD\nefab\n");

    /* The cursor inside the block: nothing happens. */
    load(d, "0123456789");
    v.set_cursor(te::Pos(0, 2), false);
    v.set_cursor(te::Pos(0, 6), true);
    v.mark_block();
    v.set_cursor(te::Pos(0, 4), false);
    CHECK(!v.copy_block());
    CHECK(!v.move_block());
    CHECK_TEXT(d, "0123456789");
    v.set_cursor(te::Pos(0, 9), false);
    te::Cell row[40];
    v.row(0, row);
    CHECK(row[1].kind == te::CELL_TEXT);
    CHECK(row[2].kind == te::CELL_BLOCK);
    CHECK(row[5].kind == te::CELL_BLOCK);
    CHECK(row[6].kind == te::CELL_TEXT);
    CHECK(v.erase_block());
    CHECK_TEXT(d, "016789");
    CHECK(!v.has_block());

    /* Typing drops the block. */
    load(d, "abc");
    v.set_cursor(te::Pos(0, 0), false);
    v.set_cursor(te::Pos(0, 1), true);
    v.mark_block();
    v.move(te::MOVE_END, false);
    v.type("x", 1);
    CHECK(!v.has_block());
}

static void test_layout()
{
    te::Document d;
    load(d, "a\tb\x01" "c\n\xe4\xb8\xad" "x\n");
    te::View v(d);
    v.set_size(12, 3);
    te::Cell row[12];
    CHECK(v.row(0, row));
    CHECK(row[0].cp == 'a');
    CHECK(row[1].cp == ' ');
    CHECK(row[8].cp == 'b');
    CHECK(row[9].cp == '^' && row[9].kind == te::CELL_CONTROL);
    CHECK(row[10].cp == 'A');
    CHECK(row[11].cp == 'c');
    CHECK(v.row(1, row));
    CHECK(row[0].cp == 0x4e2d && row[0].width == 2);
    CHECK(row[1].width == 0);
    CHECK(row[2].cp == 'x');
    CHECK(v.row(2, row));       /* the empty last line */
    CHECK(!v.row(3, row));

    CHECK(v.column_of(te::Pos(0, 2)) == 8);
    CHECK(v.column_of(te::Pos(0, 4)) == 11);
    CHECK_POS(v.pos_at(0, 5), 0, 1);
    CHECK_POS(v.pos_at(0, 8), 0, 2);
    CHECK_POS(v.pos_at(1, 1), 1, 0);

    /* Selection cells, including the line end. */
    v.set_cursor(te::Pos(0, 2), false);
    v.set_cursor(te::Pos(1, 0), true);
    v.row(0, row);
    CHECK(row[0].kind == te::CELL_TEXT);
    CHECK(row[8].kind == te::CELL_SELECTED);
    CHECK(row[11].kind == te::CELL_SELECTED);

    /* Scrolling follows the cursor. */
    load(d, "1\n2\n3\n4\n5\n6\n7\n8\n9\n");
    v.set_cursor(te::Pos(7, 0), false);
    v.follow();
    CHECK(v.top() == 5);
    int x, y;
    CHECK(v.screen_cursor(&x, &y));
    CHECK(x == 0 && y == 2);
    v.move(te::MOVE_DOC_START, false);
    v.follow();
    CHECK(v.top() == 0);
    v.goto_line(9);
    CHECK_POS(v.cursor(), 8, 0);
    v.follow();
    CHECK(v.top() <= 8 && v.top() + 3 > 8);

    /* Horizontal scrolling. */
    load(d, "0123456789abcdefghijklmnopqrstuvwxyz");
    v.move(te::MOVE_END, false);
    v.follow();
    CHECK(v.left() > 0);
    CHECK(v.screen_cursor(&x, &y));
    v.row(0, row);
    CHECK(row[x - 1].cp == 'z');
}

static void test_select_word()
{
    te::Document d;
    load(d, "int foo_bar = 1;\n");
    te::View v(d);
    v.select_word(te::Pos(0, 6));
    CHECK_POS(v.anchor(), 0, 4);
    CHECK_POS(v.cursor(), 0, 11);
    v.select_word(te::Pos(0, 12));
    CHECK_POS(v.anchor(), 0, 12);
    CHECK_POS(v.cursor(), 0, 13);
    v.select_word(te::Pos(0, 16));      /* the line end: nothing */
    CHECK(!v.has_selection());
}

static void test_view_find()
{
    te::Document d;
    load(d, "cat dog cat\ncat");
    te::View v(d);
    bool wrapped;
    CHECK(v.find("cat", true, true, &wrapped));
    CHECK_POS(v.anchor(), 0, 0);
    CHECK_POS(v.cursor(), 0, 3);
    CHECK(v.find("cat", true, true, &wrapped));
    CHECK_POS(v.anchor(), 0, 8);
    CHECK(v.find("cat", true, true, &wrapped));
    CHECK_POS(v.anchor(), 1, 0);
    CHECK(v.find("cat", true, true, &wrapped));
    CHECK_POS(v.anchor(), 0, 0);
    CHECK(wrapped);
    CHECK(v.find("cat", false, true, &wrapped));
    CHECK_POS(v.anchor(), 1, 0);
    CHECK(v.replace_all("cat", "ox", true) == 3);
    CHECK_TEXT(d, "ox dog ox\nox");
}

int main()
{
    test_document();
    test_typing_merge();
    test_find();
    test_moves();
    test_editing();
    test_blocks();
    test_layout();
    test_select_word();
    test_view_find();
    printf("te: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
