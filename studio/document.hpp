// The editing model behind the code editor: text, caret and selection, undo/redo, and all the "smart" behaviour
// (auto-indent, auto-closing brackets and blocks, keyword capitalisation, comment toggling, snippets, find/replace).
// It has no GUI code at all, so it is fully unit tested; the Windows editor control is just a view on top of it.
#pragma once
#include <string>
#include <vector>
#include "formatter.hpp"

namespace studio {

struct Pos {
    int line = 0, col = 0;   // col is a byte offset into the UTF-8 line, always on a character boundary
    bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
    bool operator!=(const Pos& o) const { return !(*this == o); }
    bool operator<(const Pos& o) const { return line < o.line || (line == o.line && col < o.col); }
    bool operator<=(const Pos& o) const { return !(o < *this); }
};

struct EditorOptions {
    int tabSize = 4;
    bool insertSpaces = true;
    bool autoIndent = true;
    bool autoCloseBrackets = true;
    bool autoCloseBlocks = true;
    bool autoCapitalizeKeywords = true;
};

struct FindOptions { bool matchCase = false; bool wholeWord = false; };

class Document {
public:
    Document();
    explicit Document(const std::string& text) { setText(text); }

    // ---- text ----
    void setText(const std::string& utf8);          // replaces everything, clears undo history and the modified flag
    std::string text() const;                       // lines joined with eol()
    const std::string& eol() const { return eol_; }
    void setEol(const std::string& e) { eol_ = (e == "\r\n") ? "\r\n" : "\n"; }
    int lineCount() const { return static_cast<int>(lines_.size()); }
    const std::string& line(int i) const { return lines_[static_cast<size_t>(i)]; }
    const std::vector<std::string>& lines() const { return lines_; }
    unsigned version() const { return version_; }   // increases on every change
    bool modified() const { return version_ != savedVersion_; }
    void markSaved() { savedVersion_ = version_; }
    void markDirty() { savedVersion_ = version_ + 1; }   // e.g. text restored from crash recovery that was never saved

    EditorOptions& options() { return opt_; }
    const EditorOptions& options() const { return opt_; }
    std::string indentUnit() const;

    // ---- caret & selection ----
    Pos caret() const { return caret_; }
    Pos anchor() const { return anchor_; }
    bool hasSelection() const { return caret_ != anchor_; }
    Pos selStart() const { return caret_ < anchor_ ? caret_ : anchor_; }
    Pos selEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }
    void setCaret(Pos p, bool extend = false);
    void moveLeft(bool extend, bool byWord = false);
    void moveRight(bool extend, bool byWord = false);
    void moveUp(bool extend, int n = 1);
    void moveDown(bool extend, int n = 1);
    void home(bool extend);                         // first non-blank, then column 0
    void end(bool extend);
    void docStart(bool extend);
    void docEnd(bool extend);
    void selectAll();
    void selectWordAt(Pos p);
    void selectLineAt(int line);
    std::string selectedText() const;
    std::string wordAt(Pos p, Pos* start = nullptr) const;   // identifier under/just before p

    // ---- editing (each public call is one undo step unless noted) ----
    void insertText(const std::string& s);          // paste: replaces the selection, no smart behaviour
    void typeChar(const std::string& utf8Char);     // one typed character with all smart behaviour
    void typeText(const std::string& utf8);         // several typed characters (e.g. from an IME)
    void newline();
    void backspace();
    void deleteForward();
    void deleteWordLeft();
    void deleteWordRight();
    void tab(bool shift);
    void deleteLines();
    void duplicateLines();
    void moveLines(int direction);                  // -1 up, +1 down
    void toggleComment();
    std::string cut();                              // returns the removed text
    // Replaces columns [startCol, endCol) of `line` with an item chosen from the completion list. "$0" in `text` marks
    // the caret position; extra lines are indented to match.
    void applyCompletion(int line, int startCol, int endCol, const std::string& text);
    // Pretty-prints the document (one undo step). Returns false if nothing changed or the formatter refused.
    bool formatDocument(const FormatOptions& fo);
    void trimTrailingWhitespace();                  // one undo step
    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // ---- search ----
    bool find(const std::string& needle, const FindOptions& fo, Pos from, bool forward, bool wrap, Pos& start, Pos& end) const;
    bool findNext(const std::string& needle, const FindOptions& fo, bool forward, bool wrap);   // selects the match
    bool replaceCurrent(const std::string& needle, const std::string& with, const FindOptions& fo);   // replaces if selected, then finds next
    int replaceAll(const std::string& needle, const std::string& with, const FindOptions& fo);

    // ---- helpers for views ----
    int displayColumn(int line, int col) const;     // tabs expanded to tab stops
    int colFromDisplay(int line, int displayCol) const;
    Pos matchingBracket(Pos p) const;               // {-1,-1} when p is not next to a bracket or it has no partner
    Pos clamp(Pos p) const;
    static int charLen(const std::string& s, int col);          // byte length of the character starting at col
    static int prevCharStart(const std::string& s, int col);
    // Re-indents a line that starts with a closing keyword (ENDIF, ELSE, NEXT ...) to its block's level.
    void reindentLineIfCloser(int line);

private:
    struct Edit { Pos at; std::string removed, inserted; };
    struct Step { std::vector<Edit> edits; Pos caretBefore, anchorBefore, caretAfter, anchorAfter; bool typing = false; };

    std::vector<std::string> lines_{std::string()};
    std::string eol_ = "\n";
    EditorOptions opt_;
    Pos caret_, anchor_;
    int desiredCol_ = -1;
    unsigned version_ = 0, savedVersion_ = 0;
    std::vector<Step> undo_, redo_;
    int depth_ = 0;
    bool noCoalesce_ = true;

    struct Guard { Document& d; bool typing; Guard(Document& doc, bool t = false) : d(doc), typing(t) { d.beginStep(t); } ~Guard() { d.endStep(); } };
    void beginStep(bool typing);
    void endStep();
    std::string textBetween(Pos a, Pos b) const;
    static Pos advance(Pos start, const std::string& s);
    Pos rawReplace(Pos a, Pos b, const std::string& ins);    // records an undo edit; returns the end of the inserted text
    Pos deleteSelection();
    void setSel(Pos anchor, Pos caret) { anchor_ = anchor; caret_ = caret; }
    void resetDesired() { desiredCol_ = -1; noCoalesce_ = true; }
    bool capitalizeWordBeforeCaret(char nextChar);
    bool inStringOrComment(Pos p) const;
    void setLineIndent(int line, int level);
    std::string indentString(int level) const;
    int leadingWhitespaceBytes(int line) const;
    bool tryAutoCloseBlock(int line);
    void indentLines(int first, int last, bool outdent);
    void reindentLine(int line);
};

} // namespace studio
