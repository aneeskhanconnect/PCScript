// The code editor control: a custom-drawn window that is a view on a studio::Document.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "common/diagnostics.hpp"
#include "completion.hpp"
#include "document.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include "widget.hpp"

namespace win {

enum EditorEvent { ED_CHANGED = 0, ED_CARET = 1, ED_ZOOM = 2 /* lParam = +1 / -1 */, ED_FOCUS = 3 };

class EditorView;

// The suggestion drop-down. A separate top-level popup so it can extend beyond the editor, but it never takes focus.
class CompletionPopup : public Widget {
public:
    explicit CompletionPopup(EditorView& owner) : owner_(owner) {}
    bool createFor(HWND ownerWindow);
    void show(const std::vector<studio::CompletionItem>& items, int screenX, int screenYTop, int screenYBottom, int lineHeight);
    void hide();
    bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void move(int delta);
    const studio::CompletionItem* selected() const;
    void setTheme(const studio::Theme* t) { theme_ = t; }
    void setFont(HFONT f) { font_ = f; }
protected:
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) override;
private:
    EditorView& owner_;
    std::vector<studio::CompletionItem> items_;
    int sel_ = 0, top_ = 0, rowH_ = 20, docH_ = 22, rows_ = 0;
    const studio::Theme* theme_ = nullptr;
    HFONT font_ = nullptr;
    void ensureVisible();
    int rowAt(int y) const;
};

class EditorView : public Widget {
public:
    EditorView();
    bool createView(HWND parent);
    ~EditorView() override;

    void attach(studio::Document* d);
    studio::Document* document() const { return doc_; }
    void setSettings(const studio::Settings* s) { settings_ = s; }
    void setTheme(const studio::Theme* t);
    void setSnippets(const std::vector<studio::Snippet>* s) { snippets_ = s; }
    void setDiagnostics(const std::vector<pcs::Diagnostic>* d) { diags_ = d; if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void rebuildFonts();                       // after a font/size/DPI/spacing change
    void refresh();                            // content or options changed: repaint, update scroll bars and caret
    void documentChanged() { afterEdit(""); }  // the document was changed from outside (find/replace, undo from a menu ...)

    int topLine() const { return top_; }
    void setTopLine(int line);
    void ensureCaretVisible();
    int visibleRows() const;
    void revealLine(int line);                 // scroll so the line is in view and put the caret at its start

    // commands (also reachable from the menus)
    void cmdCut();
    void cmdCopy();
    void cmdPaste();
    void cmdUndo();
    void cmdRedo();
    void cmdSelectAll();
    void cmdFormat();
    void cmdToggleComment();
    void cmdDuplicate();
    void cmdDeleteLine();
    void cmdMoveLines(int dir);
    void cmdIndent(bool outdent);
    void cmdSuggest();

    // used by the popup
    void acceptCompletion(const studio::CompletionItem& item);
    const studio::Theme* theme() const { return theme_; }
    bool completionVisible() const { return popup_.visible(); }
    void hideCompletion() { popup_.hide(); }

protected:
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) override;

private:
    studio::Document* doc_ = nullptr;
    const studio::Settings* settings_ = nullptr;
    const studio::Theme* theme_ = nullptr;
    const std::vector<studio::Snippet>* snippets_ = nullptr;
    const std::vector<pcs::Diagnostic>* diags_ = nullptr;
    CompletionPopup popup_;

    HFONT font_ = nullptr, uiFont_ = nullptr;
    int charW_ = 8, lineH_ = 18, ascentPad_ = 0, dpi_ = 96;
    int top_ = 0, left_ = 0;                   // first visible line / first visible display column
    int gutterW_ = 0;
    int maxCols_ = 0;
    unsigned colsVersion_ = ~0u, lastVersion_ = ~0u;
    bool focused_ = false, dragging_ = false;
    DWORD lastClickTime_ = 0;
    int clickCount_ = 0;
    wchar_t pendingHigh_ = 0;
    studio::Pos lastCaret_{-1, -1}, brA_{-1, -1}, brB_{-1, -1};
    int caretYOff_ = 0;
    int compLine_ = -1, compStart_ = 0, compEnd_ = 0;

    // painting
    struct DisplayLine { std::wstring text; std::vector<int> byteToCol; };
    DisplayLine toDisplay(const std::string& line) const;
    void paint(HDC dc, const RECT& rc);
    void drawLine(HDC dc, int row, int y, const RECT& rc);
    void drawSquiggle(HDC dc, int x1, int x2, int y, COLORREF c);
    int textLeft() const { return gutterW_ + MulDiv(6, dpi_, 96); }
    void computeGutter();
    void updateScrollBars();
    void updateCaret();
    void createCaretShape();
    studio::Pos posFromPoint(int x, int y) const;
    bool caretPixel(int& x, int& y) const;

    // input
    bool onKeyDown(WPARAM vk);
    void onChar(wchar_t ch);
    void afterEdit(const std::string& typed, bool refilter = false);
    void refreshCompletion(bool forced);
    void showContextMenu(int sx, int sy);
    void scrollBy(int lines);
    void notify(EditorEvent e, LPARAM lp = 0);
    bool isPopup() const { return popup_.visible(); }
    void positionPopup();
};

} // namespace win
