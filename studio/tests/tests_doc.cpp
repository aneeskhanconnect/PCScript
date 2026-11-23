// Tests: Document (editing model), including a randomised undo/redo fuzz.
#include <random>
#include "document.hpp"
#include "scan.hpp"
#include "test_util.hpp"

using namespace studio;

static void typeSeq(Document& d, const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\n') { d.newline(); ++i; continue; }
        int n = Document::charLen(s, static_cast<int>(i));
        d.typeChar(s.substr(i, static_cast<size_t>(n)));
        i += static_cast<size_t>(n);
    }
}
static Document fresh(bool autoClose = false, bool autoCap = true) {
    Document d;
    d.options().autoCloseBlocks = autoClose;
    d.options().autoCapitalizeKeywords = autoCap;
    return d;
}

TEST(doc_basic_text_and_eol) {
    Document d("a\r\nb\r\n");
    CHECK_EQ(d.eol(), std::string("\r\n")); CHECK_EQ(d.lineCount(), 3); CHECK_EQ(d.text(), std::string("a\r\nb\r\n"));
    Document u("x\ny"); CHECK_EQ(u.eol(), std::string("\n")); CHECK_EQ(u.lineCount(), 2);
    Document b("\xEF\xBB\xBFhello"); CHECK_EQ(b.line(0), std::string("hello"));
    CHECK(!u.modified()); u.typeChar("z"); CHECK(u.modified()); u.markSaved(); CHECK(!u.modified());
    Document e(""); CHECK_EQ(e.lineCount(), 1); CHECK_EQ(e.line(0), std::string());
    e.markDirty(); CHECK(e.modified()); e.markSaved(); CHECK(!e.modified());
}

TEST(doc_auto_indent_and_capitalise) {
    Document d = fresh();
    typeSeq(d, "if x then\noutput 1\nendif\n");
    CHECK_EQ(d.text(), std::string("IF x THEN\n    OUTPUT 1\nENDIF\n"));
    CHECK(d.caret() == (Pos{3, 0}));
    Document e = fresh();
    typeSeq(e, "for i <- 1 to 3\nwhile i < 2 do\ni <- i + 1\nendwhile\nnext i");
    CHECK_EQ(e.text(), std::string("FOR i <- 1 TO 3\n    WHILE i < 2 DO\n        i <- i + 1\n    ENDWHILE\nNEXT i"));
    // capitalisation can be switched off; names that merely look like keywords are respected
    Document f = fresh(false, false); typeSeq(f, "if x then"); CHECK_EQ(f.text(), std::string("if x then"));
    Document g = fresh(); typeSeq(g, "declare input : string\ninput <- \"a\"\n");
    CHECK_EQ(g.text(), std::string("DECLARE input : string\ninput <- \"a\"\n"));
    Document h = fresh(); typeSeq(h, "// if then\nx <- \"if \"\n"); CHECK_EQ(h.text(), std::string("// if then\nx <- \"if \"\n"));   // not in comments/strings
    Document i = fresh(); typeSeq(i, "x <- obj.next + 1"); CHECK_EQ(i.text(), std::string("x <- obj.next + 1"));                  // member access
    Document j = fresh(); typeSeq(j, "x <- true and false\n"); CHECK_EQ(j.text(), std::string("x <- TRUE AND FALSE\n"));
    // Enter between existing text splits it and keeps the right-hand part's indentation sensible
    Document k = fresh(); k.setText("IF a THEN\n    x <- 1 y <- 2\nENDIF"); k.options().autoCapitalizeKeywords = false;
    k.setCaret({1, 11}); k.newline(); CHECK_EQ(k.line(1), std::string("    x <- 1 ")); CHECK_EQ(k.line(2), std::string("    y <- 2"));
    // Enter before a closer dedents it
    Document m = fresh(); m.setText("IF a THEN\n    x <- 1\n    ENDIF"); m.setCaret({2, 4}); m.newline();
    CHECK_EQ(m.line(2), std::string("")); CHECK_EQ(m.line(3), std::string("ENDIF"));
}

TEST(doc_auto_close_blocks) {
    Document d = fresh(true);
    typeSeq(d, "if x then\n");
    CHECK_EQ(d.text(), std::string("IF x THEN\n    \nENDIF"));      // body line keeps its indent until something is typed
    CHECK(d.caret() == (Pos{1, 4}));
    typeSeq(d, "output 1");
    CHECK_EQ(d.text(), std::string("IF x THEN\n    OUTPUT 1\nENDIF"));
    Document e = fresh(true); typeSeq(e, "FOR i <- 1 TO 5\n"); CHECK_EQ(e.text(), std::string("FOR i <- 1 TO 5\n    \nNEXT i"));
    Document f = fresh(true); typeSeq(f, "while x do\n"); CHECK_EQ(f.text(), std::string("WHILE x DO\n    \nENDWHILE"));
    Document g = fresh(true); typeSeq(g, "REPEAT\n"); CHECK_EQ(g.text(), std::string("REPEAT\n    \nUNTIL"));
    Document h = fresh(true); typeSeq(h, "case of x\n"); CHECK_EQ(h.text(), std::string("CASE OF x\n    \nENDCASE"));
    Document p = fresh(true); typeSeq(p, "procedure Hi()\n"); CHECK_EQ(p.text(), std::string("PROCEDURE Hi()\n    \nENDPROCEDURE"));
    Document q = fresh(true); typeSeq(q, "function F() returns integer\n"); CHECK_EQ(q.text(), std::string("FUNCTION F() RETURNS integer\n    \nENDFUNCTION"));
    Document c = fresh(true); typeSeq(c, "class Dog\n"); CHECK_EQ(c.text(), std::string("CLASS Dog\n    \nENDCLASS"));
    Document r = fresh(true); typeSeq(r, "type Player\n"); CHECK_EQ(r.text(), std::string("TYPE Player\n    \nENDTYPE"));
    // no closer for an unfinished header, for a one-line TYPE, or when the block is already closed below
    Document n1 = fresh(true); typeSeq(n1, "IF x\n"); CHECK_EQ(n1.lineCount(), 2);
    Document n2 = fresh(true); typeSeq(n2, "TYPE S = (a, b)\n"); CHECK_EQ(n2.lineCount(), 2);
    Document n3 = fresh(true); n3.setText("IF a THEN\nENDIF"); n3.setCaret({0, 9}); n3.newline(); CHECK_EQ(n3.text(), std::string("IF a THEN\n    \nENDIF"));
    // nested: inner IF inside an indented body gets its closer at the right indentation
    Document n4 = fresh(true); typeSeq(n4, "WHILE a DO\nIF b THEN\n"); CHECK_EQ(n4.text(), std::string("WHILE a DO\n    IF b THEN\n        \n    ENDIF\nENDWHILE"));
}

TEST(doc_brackets_and_quotes) {
    Document d = fresh();
    typeSeq(d, "OUTPUT (1 + 2)");
    CHECK_EQ(d.text(), std::string("OUTPUT (1 + 2)")); CHECK(d.caret() == (Pos{0, 14}));
    Document e = fresh(); typeSeq(e, "OUTPUT \"hi\""); CHECK_EQ(e.text(), std::string("OUTPUT \"hi\"")); CHECK(e.caret() == (Pos{0, 11}));
    Document f = fresh(); typeSeq(f, "x <- 'a'"); CHECK_EQ(f.text(), std::string("x <- 'a'"));
    Document g = fresh(); typeSeq(g, "// don't"); CHECK_EQ(g.text(), std::string("// don't"));          // apostrophe in a comment is not a CHAR
    Document h = fresh(); typeSeq(h, "x[2]"); CHECK_EQ(h.text(), std::string("x[2]"));
    Document i = fresh(); i.typeChar("("); CHECK_EQ(i.text(), std::string("()")); i.backspace(); CHECK_EQ(i.text(), std::string(""));
    Document j = fresh(); j.options().autoCloseBrackets = false; j.typeChar("("); CHECK_EQ(j.text(), std::string("("));
    // wrap a selection
    Document k = fresh(); k.setText("abc"); k.setCaret({0, 0}); k.setCaret({0, 3}, true); k.typeChar("\"");
    CHECK_EQ(k.text(), std::string("\"abc\"")); CHECK_EQ(k.selectedText(), std::string("abc"));
    // no pairing before a word character
    Document m = fresh(); m.setText("abc"); m.setCaret({0, 0}); m.typeChar("("); CHECK_EQ(m.text(), std::string("(abc"));
}

TEST(doc_backspace_delete_tab) {
    Document d = fresh(); d.setText("        x"); d.setCaret({0, 8}); d.backspace(); CHECK_EQ(d.line(0), std::string("    x"));
    d.setText("      x"); d.setCaret({0, 6}); d.backspace(); CHECK_EQ(d.line(0), std::string("    x"));   // back to the previous indent stop
    d.setText("ab\ncd"); d.setCaret({1, 0}); d.backspace(); CHECK_EQ(d.text(), std::string("abcd")); CHECK(d.caret() == (Pos{0, 2}));
    d.deleteForward(); CHECK_EQ(d.text(), std::string("abd"));
    d.setText("x\xE2\x86\x90y"); d.setCaret({0, 4});
    d.moveLeft(false); CHECK(d.caret() == (Pos{0, 1}));                       // steps over the whole 3-byte arrow
    d.moveRight(false); CHECK(d.caret() == (Pos{0, 4}));
    d.backspace(); CHECK_EQ(d.text(), std::string("xy")); CHECK(d.caret() == (Pos{0, 1}));
    Document t = fresh(); t.tab(false); CHECK_EQ(t.text(), std::string("    "));
    t.setText("ab"); t.setCaret({0, 2}); t.tab(false); CHECK_EQ(t.text(), std::string("ab  "));
    t.options().insertSpaces = false; t.setText(""); t.tab(false); CHECK_EQ(t.text(), std::string("\t"));
    // block indent / outdent
    Document b = fresh(); b.setText("a\nb\n\nc"); b.selectAll(); b.tab(false); CHECK_EQ(b.text(), std::string("    a\n    b\n\n    c"));
    b.tab(true); CHECK_EQ(b.text(), std::string("a\nb\n\nc"));
    b.setCaret({1, 0}); b.tab(true); CHECK_EQ(b.text(), std::string("a\nb\n\nc"));      // nothing to remove
    // word movement and deletion
    Document w = fresh(); w.setText("foo bar_baz  qux"); w.setCaret({0, 0}); w.moveRight(false, true); CHECK(w.caret() == (Pos{0, 4}));
    w.moveRight(false, true); CHECK(w.caret() == (Pos{0, 13})); w.moveLeft(false, true); CHECK(w.caret() == (Pos{0, 4}));
    w.deleteWordRight(); CHECK_EQ(w.text(), std::string("foo qux")); w.deleteWordLeft(); CHECK_EQ(w.text(), std::string("qux"));
}

TEST(doc_line_operations_and_comments) {
    Document d = fresh(); d.setText("a\nb\nc");
    d.setCaret({1, 1}); d.duplicateLines(); CHECK_EQ(d.text(), std::string("a\nb\nb\nc")); CHECK_EQ(d.caret().line, 2);
    d.deleteLines(); CHECK_EQ(d.text(), std::string("a\nb\nc"));
    d.setCaret({1, 0}); d.moveLines(-1); CHECK_EQ(d.text(), std::string("b\na\nc")); CHECK_EQ(d.caret().line, 0);
    d.moveLines(-1); CHECK_EQ(d.text(), std::string("b\na\nc"));                             // already at the top
    d.setCaret({0, 0}); d.moveLines(1); d.moveLines(1); CHECK_EQ(d.text(), std::string("a\nc\nb"));
    d.moveLines(1); CHECK_EQ(d.text(), std::string("a\nc\nb"));                              // already at the bottom
    d.setCaret({2, 0}); d.deleteLines(); CHECK_EQ(d.text(), std::string("a\nc"));
    d.setText("only"); d.deleteLines(); CHECK_EQ(d.text(), std::string(""));
    Document c = fresh(); c.setText("    x <- 1\n\n    y <- 2");
    c.selectAll(); c.toggleComment(); CHECK_EQ(c.text(), std::string("    // x <- 1\n\n    // y <- 2"));
    c.toggleComment(); CHECK_EQ(c.text(), std::string("    x <- 1\n\n    y <- 2"));
    c.setText("a\n  // b"); c.selectAll(); c.toggleComment(); CHECK_EQ(c.text(), std::string("// a\n//   // b"));   // mixed: comment everything
    c.undo(); CHECK_EQ(c.text(), std::string("a\n  // b"));
    c.setText("x"); c.setCaret({0, 1}); c.toggleComment(); CHECK_EQ(c.text(), std::string("// x")); CHECK_EQ(c.caret().col, 4);
}

TEST(doc_undo_redo) {
    Document d = fresh(); d.setText("");
    typeSeq(d, "ab cd"); CHECK_EQ(d.text(), std::string("ab cd"));
    CHECK(d.undo()); CHECK_EQ(d.text(), std::string("ab ")); CHECK(d.undo()); CHECK_EQ(d.text(), std::string("")); CHECK(!d.undo());
    CHECK(d.redo()); CHECK_EQ(d.text(), std::string("ab ")); CHECK(d.redo()); CHECK_EQ(d.text(), std::string("ab cd")); CHECK(!d.redo());
    d.typeChar("x"); CHECK(!d.canRedo());                                                  // new edit clears redo
    d.setText("one\ntwo"); d.selectAll(); d.insertText("replaced"); CHECK_EQ(d.text(), std::string("replaced"));
    d.undo(); CHECK_EQ(d.text(), std::string("one\ntwo")); CHECK(d.hasSelection());      // selection restored
    d.redo(); CHECK_EQ(d.text(), std::string("replaced"));
    // one undo step reverses a whole compound command
    d.setText("a\nb"); d.selectAll(); d.toggleComment(); d.undo(); CHECK_EQ(d.text(), std::string("a\nb"));
    d.setText("IF a THEN\nx <- 1\nENDIF"); d.formatDocument(FormatOptions{}); CHECK_EQ(d.line(1), std::string("    x <- 1")); d.undo(); CHECK_EQ(d.line(1), std::string("x <- 1"));
    // modified flag follows
    d.setText("q"); d.typeChar("z"); CHECK(d.modified()); d.markSaved(); d.undo(); CHECK(d.modified());
}

TEST(doc_find_replace) {
    Document d = fresh(); d.setText("Cat cat concat\nCAT");
    FindOptions any, cs; cs.matchCase = true; FindOptions ww; ww.wholeWord = true;
    Pos s, e;
    CHECK(d.find("cat", any, {0, 0}, true, false, s, e)); CHECK(s == (Pos{0, 0})); CHECK(e == (Pos{0, 3}));
    CHECK(d.find("cat", cs, {0, 0}, true, false, s, e)); CHECK(s == (Pos{0, 4}));
    CHECK(d.find("cat", ww, {0, 1}, true, false, s, e)); CHECK(s == (Pos{0, 4}));
    CHECK(d.find("cat", any, {1, 3}, false, false, s, e)); CHECK(s == (Pos{1, 0}));
    CHECK(d.find("cat", any, {0, 0}, false, false, s, e) == false);
    CHECK(d.find("cat", any, {0, 0}, false, true, s, e)); CHECK(s == (Pos{1, 0}));        // wraps backwards
    CHECK(d.find("cat", any, {1, 3}, true, true, s, e)); CHECK(s == (Pos{0, 0}));         // wraps forwards
    CHECK(!d.find("dog", any, {0, 0}, true, true, s, e)); CHECK(!d.find("", any, {0, 0}, true, true, s, e));
    CHECK(d.findNext("cat", any, true, false)); CHECK_EQ(d.selectedText(), std::string("Cat"));
    CHECK(d.findNext("cat", any, true, false)); CHECK(d.selStart() == (Pos{0, 4}));
    CHECK_EQ(d.replaceAll("cat", "dog", any), 4); CHECK_EQ(d.text(), std::string("dog dog condog\ndog"));
    d.undo(); CHECK_EQ(d.text(), std::string("Cat cat concat\nCAT"));                     // replace-all is one undo step
    CHECK_EQ(d.replaceAll("cat", "X", ww), 3);
    d.setText("a a a"); d.setCaret({0, 0}); d.findNext("a", any, true, true);
    CHECK(d.replaceCurrent("a", "bb", any)); CHECK_EQ(d.text(), std::string("bb a a")); CHECK_EQ(d.selectedText(), std::string("a"));
}

TEST(doc_completion_and_snippets) {
    Document d = fresh(); d.setText("OUT");
    d.applyCompletion(0, 0, 3, "OUTPUT"); CHECK_EQ(d.text(), std::string("OUTPUT")); CHECK(d.caret() == (Pos{0, 6}));
    d.setText("x <- LEN"); d.applyCompletion(0, 5, 8, "LENGTH($0)"); CHECK_EQ(d.text(), std::string("x <- LENGTH()")); CHECK(d.caret() == (Pos{0, 12}));
    d.setText("    if"); d.applyCompletion(0, 4, 6, "IF $0 THEN\n    \nENDIF");
    CHECK_EQ(d.text(), std::string("    IF  THEN\n        \n    ENDIF")); CHECK(d.caret() == (Pos{0, 7}));   // later lines keep the indentation
    d.options().insertSpaces = false; d.setText("\tx"); d.applyCompletion(0, 1, 2, "IF $0 THEN\n    A\nENDIF");
    CHECK_EQ(d.text(), std::string("\tIF  THEN\n\t\tA\n\tENDIF"));                                           // snippet indentation follows the document style
    // accepting a closing keyword dedents its line
    Document e = fresh(); e.setText("IF a THEN\n    OUTPUT 1\n    END"); e.applyCompletion(2, 4, 7, "ENDIF"); CHECK_EQ(e.line(2), std::string("ENDIF"));
    e.undo(); CHECK_EQ(e.line(2), std::string("    END"));
}

TEST(doc_format_document_keeps_caret) {
    Document d = fresh(); d.setText("IF a THEN\n\n\n\nx<-1\nENDIF");
    d.setCaret({4, 3});
    CHECK(d.formatDocument(FormatOptions{}));
    CHECK_EQ(d.text(), std::string("IF a THEN\n\n    x <- 1\nENDIF\n"));
    CHECK_EQ(d.caret().line, 2); CHECK(d.caret().col >= 4);
    CHECK(!d.formatDocument(FormatOptions{}));                                            // already formatted
    d.setText("x <- 1 // c  \n"); d.trimTrailingWhitespace(); CHECK_EQ(d.line(0), std::string("x <- 1 // c"));
}

TEST(doc_view_helpers) {
    Document d = fresh(); d.setText("\tab\tc\nxyz");
    CHECK_EQ(d.displayColumn(0, 1), 4); CHECK_EQ(d.displayColumn(0, 3), 6); CHECK_EQ(d.displayColumn(0, 4), 8); CHECK_EQ(d.displayColumn(0, 5), 9);
    CHECK_EQ(d.colFromDisplay(0, 6), 3); CHECK_EQ(d.colFromDisplay(0, 100), 5);
    d.setText("f(a[1], (2))\nx");
    CHECK(d.matchingBracket({0, 1}) == (Pos{0, 11}));        // on the bracket
    CHECK(d.matchingBracket({0, 2}) == (Pos{0, 11}));        // just after it
    CHECK(d.matchingBracket({0, 11}) == (Pos{0, 1}));
    CHECK(d.matchingBracket({0, 3}) == (Pos{0, 5}));        // '[' -> ']'
    CHECK(d.matchingBracket({0, 4}) == (Pos{0, 5}));        // just after '['
    CHECK(d.matchingBracket({0, 9}) == (Pos{0, 10}));       // inner "(2)"
    CHECK(d.matchingBracket({1, 0}) == (Pos{-1, -1}));
    d.setText("OUTPUT \"(\" & x(1)"); CHECK(d.matchingBracket({0, 15}) == (Pos{0, 16}));   // brackets inside strings are ignored
    d.setText("(\n(\n)"); CHECK(d.matchingBracket({0, 0}) == (Pos{-1, -1}));                 // unmatched
    d.setText("word.other 1"); Pos st; CHECK_EQ(d.wordAt({0, 2}, &st), std::string("word")); CHECK(st == (Pos{0, 0}));
    d.setText("ab cd"); d.selectWordAt({0, 4}); CHECK_EQ(d.selectedText(), std::string("cd")); d.selectLineAt(0); CHECK_EQ(d.selectedText(), std::string("ab cd"));
}

// Random editing: the caret must always stay valid, and undoing everything must restore the original text exactly.
TEST(doc_random_editing_fuzz) {
    std::mt19937 rng(2024);
    const char* alphabet[] = {"a", "B", "x", " ", " ", "(", ")", "[", "]", "\"", "'", "<", "-", ":", ",", ".", "1", "\t", "\xE2\x86\x90", "\xC3\xA9", "if", "then", "end", "next ", "endif"};
    int ops = 0;
    for (int round = 0; round < 40; ++round) {
        Document d;
        d.options().autoCloseBlocks = (round % 2 == 0);
        d.options().insertSpaces = (round % 3 != 0);
        std::string initial = (round % 4 == 0) ? "IF a THEN\n    x <- 1\nELSE\n    y <- 2\nENDIF\n" : "";
        d.setText(initial);
        for (int i = 0; i < 400; ++i, ++ops) {
            switch (rng() % 22) {
                case 0: case 1: case 2: case 3: case 4: case 5: d.typeChar(alphabet[rng() % 25]); break;
                case 6: d.newline(); break;
                case 7: d.backspace(); break;
                case 8: d.deleteForward(); break;
                case 9: d.moveLeft(rng() % 2); break;
                case 10: d.moveRight(rng() % 2, rng() % 2); break;
                case 11: d.moveUp(rng() % 2); break;
                case 12: d.moveDown(rng() % 2); break;
                case 13: d.tab(rng() % 2); break;
                case 14: d.toggleComment(); break;
                case 15: d.duplicateLines(); break;
                case 16: d.moveLines(rng() % 2 ? 1 : -1); break;
                case 17: d.insertText("pa\nste\r\nx"); break;
                case 18: d.cut(); break;
                case 19: d.formatDocument(FormatOptions{}); break;
                case 20: d.selectAll(); break;
                default: d.deleteLines(); break;
            }
            Pos c = d.caret(), a = d.anchor();
            CHECK(c.line >= 0 && c.line < d.lineCount()); CHECK(c.col >= 0 && c.col <= static_cast<int>(d.line(c.line).size()));
            CHECK(a.line >= 0 && a.line < d.lineCount()); CHECK(a.col >= 0 && a.col <= static_cast<int>(d.line(a.line).size()));
            CHECK(d.lineCount() >= 1);
            if (T::failures() > 20) return;
        }
        while (d.undo()) {}
        CHECK_EQ(d.text(), splitLines(initial).size() ? joinLines(splitLines(initial)) : std::string());
        while (d.redo()) {}
        while (d.undo()) {}
        CHECK_EQ(d.text(), joinLines(splitLines(initial)));
    }
    std::cout << "   (" << ops << " random editing operations)\n";
}
