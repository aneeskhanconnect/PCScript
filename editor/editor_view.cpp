#include "editor_view.hpp"
#include <algorithm>
#include "formatter.hpp"
#include "resource.h"
#include "scan.hpp"
#include "symbols.hpp"
#include "win_util.hpp"

namespace win {

using studio::Pos;
using studio::Slot;

static const wchar_t* kEditorClass = L"PCSEditorView";
static const wchar_t* kPopupClass = L"PCSCompletionPopup";

// ======================================================================= completion popup
bool CompletionPopup::createFor(HWND ownerWindow) {
    return create(ownerWindow, kPopupClass, WS_POPUP | WS_BORDER, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, 0);
}

void CompletionPopup::hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

void CompletionPopup::move(int delta) {
    if (items_.empty()) return;
    sel_ = std::max(0, std::min(static_cast<int>(items_.size()) - 1, sel_ + delta));
    ensureVisible();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

const studio::CompletionItem* CompletionPopup::selected() const {
    return (sel_ >= 0 && sel_ < static_cast<int>(items_.size())) ? &items_[static_cast<size_t>(sel_)] : nullptr;
}

void CompletionPopup::ensureVisible() {
    if (sel_ < top_) top_ = sel_;
    if (sel_ >= top_ + rows_) top_ = sel_ - rows_ + 1;
    top_ = std::max(0, top_);
}

int CompletionPopup::rowAt(int y) const {
    int r = top_ + y / std::max(1, rowH_);
    return (y >= 0 && r < static_cast<int>(items_.size()) && y < rows_ * rowH_) ? r : -1;
}

void CompletionPopup::show(const std::vector<studio::CompletionItem>& items, int sx, int syTop, int syBottom, int lineHeight) {
    if (!hwnd_ || items.empty()) { hide(); return; }
    // keep the selection when the list is only being re-filtered and the same item is still there
    std::string keep = (sel_ >= 0 && sel_ < static_cast<int>(items_.size())) ? items_[static_cast<size_t>(sel_)].label : std::string();
    items_ = items;
    sel_ = 0; top_ = 0;
    for (size_t i = 0; i < items_.size(); ++i) if (!keep.empty() && items_[i].label == keep && IsWindowVisible(hwnd_)) { sel_ = static_cast<int>(i); break; }

    int dpi = dpiOf(hwnd_);
    rowH_ = std::max(MulDiv(22, dpi, 96), lineHeight);
    docH_ = MulDiv(24, dpi, 96);
    rows_ = std::min(static_cast<int>(items_.size()), 10);
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = font_ ? SelectObject(dc, font_) : nullptr;
    int widest = 0;
    for (const auto& it : items_) {
        std::wstring a = widen(it.label), b = widen(it.detail);
        widest = std::max(widest, textWidth(dc, a) * 1 + (b.empty() ? 0 : MulDiv(24, dpi, 96) + std::min(textWidth(dc, b), MulDiv(260, dpi, 96))));
    }
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    int w = std::max(MulDiv(280, dpi, 96), std::min(MulDiv(640, dpi, 96), widest + MulDiv(64, dpi, 96)));
    int h = rows_ * rowH_ + docH_ + 2;
    RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    POINT pt = {sx, syBottom};
    if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) work = mi.rcWork;
    int x = std::min(sx, static_cast<int>(work.right) - w);
    x = std::max(x, static_cast<int>(work.left));
    int y = syBottom;
    if (y + h > work.bottom) y = syTop - h;       // not enough room below: open above the line
    ensureVisible();
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

static const wchar_t* badgeFor(studio::ItemKind k) {
    switch (k) {
        case studio::ItemKind::Keyword: return L"K"; case studio::ItemKind::Type: return L"T"; case studio::ItemKind::Builtin: return L"\x0192";
        case studio::ItemKind::Variable: return L"v"; case studio::ItemKind::Constant: return L"c"; case studio::ItemKind::Parameter: return L"p";
        case studio::ItemKind::Procedure: return L"P"; case studio::ItemKind::Function: return L"F"; case studio::ItemKind::Class: return L"C";
        case studio::ItemKind::Record: return L"R"; case studio::ItemKind::Enum: return L"E"; case studio::ItemKind::EnumValue: return L"e";
        case studio::ItemKind::Field: return L"f"; case studio::ItemKind::Method: return L"m"; case studio::ItemKind::Snippet: return L"\x2756";
    }
    return L"?";
}

static studio::Color badgeColor(const studio::Theme& t, studio::ItemKind k) {
    switch (k) {
        case studio::ItemKind::Keyword: return t[Slot::Keyword];
        case studio::ItemKind::Type: case studio::ItemKind::Class: case studio::ItemKind::Record: case studio::ItemKind::Enum: return t[Slot::Type];
        case studio::ItemKind::Builtin: case studio::ItemKind::Function: case studio::ItemKind::Procedure: case studio::ItemKind::Method: return t[Slot::Builtin];
        case studio::ItemKind::Constant: case studio::ItemKind::EnumValue: return t[Slot::Constant];
        case studio::ItemKind::Snippet: return t[Slot::Accent];
        default: return t[Slot::Number];
    }
}

LRESULT CompletionPopup::handle(UINT msg, WPARAM wp, LPARAM lp) {
    const studio::Theme& t = theme_ ? *theme_ : studio::defaultTheme(true);
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT rc; GetClientRect(hwnd_, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
            HGDIOBJ oldBmp = SelectObject(mem, bmp);
            HGDIOBJ oldFont = font_ ? SelectObject(mem, font_) : nullptr;
            int dpi = GetDeviceCaps(mem, LOGPIXELSX);
            fillRect(mem, rc, win::rgb(t[Slot::PopupBg]));
            for (int r = 0; r < rows_; ++r) {
                int idx = top_ + r;
                if (idx >= static_cast<int>(items_.size())) break;
                const auto& it = items_[static_cast<size_t>(idx)];
                RECT row = {0, r * rowH_, rc.right, (r + 1) * rowH_};
                bool sel = idx == sel_;
                if (sel) fillRect(mem, row, win::rgb(t[Slot::PopupSelBg]));
                COLORREF fg = win::rgb(sel ? t[Slot::PopupSelFg] : t[Slot::PopupFg]);
                RECT badge = {row.left + MulDiv(6, dpi, 96), row.top, row.left + MulDiv(26, dpi, 96), row.bottom};
                drawLabel(mem, badgeFor(it.kind), badge, sel ? fg : win::rgb(badgeColor(t, it.kind)), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                RECT lab = {row.left + MulDiv(32, dpi, 96), row.top, row.right - MulDiv(8, dpi, 96), row.bottom};
                std::wstring label = widen(it.label);
                int lw = textWidth(mem, label);
                drawLabel(mem, label, lab, fg);
                if (!it.detail.empty()) {
                    RECT det = {lab.left + lw + MulDiv(14, dpi, 96), row.top, lab.right, row.bottom};
                    if (det.left < det.right)
                        drawLabel(mem, widen(it.detail), det, sel ? fg : win::rgb(t[Slot::GutterFg]), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                }
            }
            // details strip for the highlighted entry
            RECT strip = {0, rows_ * rowH_, rc.right, rc.bottom};
            fillRect(mem, strip, win::rgb(t[Slot::UiBg]));
            RECT sep = {0, strip.top, rc.right, strip.top + 1};
            fillRect(mem, sep, win::rgb(t[Slot::UiBorder]));
            if (const studio::CompletionItem* s = selected()) {
                RECT tr = {MulDiv(8, dpi, 96), strip.top, rc.right - MulDiv(8, dpi, 96), strip.bottom};
                std::string text = s->doc.empty() ? s->detail : s->doc;
                drawLabel(mem, widen(text), tr, win::rgb(t[Slot::UiFg]), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            if (oldFont) SelectObject(mem, oldFont);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, oldBmp);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int r = rowAt(static_cast<short>(HIWORD(lp)));
            if (r >= 0) { sel_ = r; InvalidateRect(hwnd_, nullptr, FALSE); }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int r = rowAt(static_cast<short>(HIWORD(lp)));
            if (r >= 0) { sel_ = r; studio::CompletionItem it = items_[static_cast<size_t>(r)]; owner_.acceptCompletion(it); }
            return 0;
        }
        case WM_MOUSEWHEEL:
            move(GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -3 : 3);
            return 0;
    }
    return Widget::handle(msg, wp, lp);
}

// ======================================================================= editor
EditorView::EditorView() : popup_(*this) {}

bool EditorView::createView(HWND parent) {
    return create(parent, kEditorClass, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS, 0, IDC_EDITOR);
}

EditorView::~EditorView() {
    if (font_) DeleteObject(font_);
    if (uiFont_) DeleteObject(uiFont_);
}

void EditorView::attach(studio::Document* d) {
    doc_ = d;
    top_ = left_ = 0;
    lastVersion_ = ~0u;
    colsVersion_ = ~0u;
    popup_.hide();
    if (hwnd_) { refresh(); }
}

void EditorView::setTheme(const studio::Theme* t) {
    theme_ = t;
    popup_.setTheme(t);
    if (hwnd_) {
        themeControl(hwnd_, t && t->dark);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void EditorView::rebuildFonts() {
    if (!hwnd_ || !settings_) return;
    dpi_ = dpiOf(hwnd_);
    if (font_) DeleteObject(font_);
    if (uiFont_) DeleteObject(uiFont_);
    font_ = makeFont(settings_->fontFamily, settings_->fontSize, dpi_);
    uiFont_ = makeUiFont(dpi_);
    popup_.setFont(uiFont_);
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = SelectObject(dc, font_);
    TEXTMETRICW tm = {};
    GetTextMetricsW(dc, &tm);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, L"M", 1, &sz);
    SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    charW_ = std::max<int>(1, sz.cx);
    int h = std::max<int>(1, tm.tmHeight);
    lineH_ = std::max(h, h * settings_->lineSpacing / 100);
    ascentPad_ = (lineH_ - h) / 2;
    if (focused_) createCaretShape();
    refresh();
}

int EditorView::visibleRows() const {
    RECT rc; GetClientRect(hwnd_, &rc);
    return std::max(1, static_cast<int>(rc.bottom) / std::max(1, lineH_));
}

void EditorView::computeGutter() {
    if (!doc_ || !settings_ || !settings_->showLineNumbers) { gutterW_ = 0; return; }
    int digits = 3, n = doc_->lineCount();
    while (n >= 1000) { ++digits; n /= 10; }
    gutterW_ = (digits + 2) * charW_ + MulDiv(4, dpi_, 96);
}

void EditorView::notify(EditorEvent e, LPARAM lp) {
    HWND p = GetParent(hwnd_);
    if (p) SendMessageW(p, WM_PCS_EDITOR, static_cast<WPARAM>(e), lp);
}

void EditorView::refresh() {
    if (!hwnd_) return;
    computeGutter();
    updateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    updateCaret();
}

void EditorView::setTopLine(int line) {
    if (!doc_) return;
    top_ = std::max(0, std::min(line, doc_->lineCount() - 1));
    updateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    updateCaret();
}

void EditorView::revealLine(int line) {
    if (!doc_) return;
    line = std::max(0, std::min(line, doc_->lineCount() - 1));
    doc_->setCaret({line, 0});
    int rows = visibleRows();
    if (line < top_ || line >= top_ + rows) top_ = std::max(0, line - rows / 3);
    afterEdit("");
}

void EditorView::scrollBy(int lines) {
    if (!doc_) return;
    top_ = std::max(0, std::min(doc_->lineCount() - 1, top_ + lines));
    updateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    updateCaret();
    if (popup_.visible()) popup_.hide();
}

void EditorView::updateScrollBars() {
    if (!hwnd_ || !doc_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    int rows = visibleRows();
    int cols = std::max(1, (static_cast<int>(rc.right) - textLeft()) / std::max(1, charW_));
    if (colsVersion_ != doc_->version()) {
        colsVersion_ = doc_->version();
        maxCols_ = 0;
        if (doc_->lineCount() < 50000)
            for (int i = 0; i < doc_->lineCount(); ++i) maxCols_ = std::max(maxCols_, doc_->displayColumn(i, static_cast<int>(doc_->line(i).size())));
    }
    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0; si.nMax = doc_->lineCount() + rows - 2; si.nPage = static_cast<UINT>(rows); si.nPos = top_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    left_ = std::max(0, std::min(left_, std::max(0, maxCols_ + 2 - cols + 1)));
    si.nMax = maxCols_ + 2; si.nPage = static_cast<UINT>(cols); si.nPos = left_;
    SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
}

void EditorView::ensureCaretVisible() {
    if (!doc_ || !hwnd_) return;
    Pos c = doc_->caret();
    int rows = visibleRows();
    if (c.line < top_) top_ = c.line;
    else if (c.line >= top_ + rows) top_ = c.line - rows + 1;
    top_ = std::max(0, top_);
    RECT rc; GetClientRect(hwnd_, &rc);
    int cols = std::max(1, (static_cast<int>(rc.right) - textLeft()) / std::max(1, charW_));
    int col = doc_->displayColumn(c.line, c.col);
    if (col < left_) left_ = col;
    else if (col >= left_ + cols - 1) left_ = col - cols + 2;
    left_ = std::max(0, left_);
}

void EditorView::createCaretShape() {
    if (!hwnd_) return;
    int w = std::max(1, MulDiv(2, dpi_, 96)), h = lineH_ - 2 * ascentPad_;
    caretYOff_ = ascentPad_;
    std::string style = settings_ ? settings_->caretStyle : "line";
    if (style == "block") w = charW_;
    else if (style == "underline") { w = charW_; h = std::max(2, MulDiv(2, dpi_, 96)); caretYOff_ = lineH_ - ascentPad_ - h; }
    CreateCaret(hwnd_, nullptr, w, std::max(1, h));
    ShowCaret(hwnd_);
}

bool EditorView::caretPixel(int& x, int& y) const {
    if (!doc_) return false;
    Pos c = doc_->caret();
    int row = c.line - top_;
    if (row < 0 || row >= visibleRows() + 1) return false;
    int col = doc_->displayColumn(c.line, c.col);
    x = textLeft() + (col - left_) * charW_;
    y = row * lineH_;
    return x >= gutterW_;
}

void EditorView::updateCaret() {
    if (!focused_ || !hwnd_) return;
    int x, y;
    if (caretPixel(x, y)) SetCaretPos(x, y + caretYOff_);
    else SetCaretPos(-100, -100);
}

Pos EditorView::posFromPoint(int x, int y) const {
    int line = top_ + (y >= 0 ? y / std::max(1, lineH_) : -1 - (-y - 1) / std::max(1, lineH_));
    line = std::max(0, std::min(line, doc_->lineCount() - 1));
    int disp = std::max(0, (x - textLeft() + charW_ / 2) / std::max(1, charW_) + left_);
    return {line, doc_->colFromDisplay(line, disp)};
}

// ----------------------------------------------------------------------- painting
EditorView::DisplayLine EditorView::toDisplay(const std::string& line) const {
    DisplayLine d;
    d.byteToCol.assign(line.size() + 1, 0);
    int col = 0, tab = std::max(1, doc_->options().tabSize);
    size_t i = 0;
    while (i < line.size()) {
        unsigned char c = static_cast<unsigned char>(line[i]);
        if (c == '\t') {
            int n = tab - col % tab;
            d.byteToCol[i] = col;
            d.text.append(static_cast<size_t>(n), L' ');
            col += n; ++i;
            continue;
        }
        int len = studio::Document::charLen(line, static_cast<int>(i));
        if (len <= 0) len = 1;
        unsigned cp = c;
        if (c >= 0xF0 && len == 4) cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(line[i + 1]) & 0x3Fu) << 12) | ((static_cast<unsigned char>(line[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(line[i + 3]) & 0x3Fu);
        else if (c >= 0xE0 && len == 3) cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(line[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(line[i + 2]) & 0x3Fu);
        else if (c >= 0xC0 && len == 2) cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(line[i + 1]) & 0x3Fu);
        wchar_t wc = (cp < 0x20 || cp >= 0x10000) ? L'\xFFFD' : static_cast<wchar_t>(cp);
        for (int k = 0; k < len && i + static_cast<size_t>(k) < d.byteToCol.size(); ++k) d.byteToCol[i + static_cast<size_t>(k)] = col;
        d.text += wc;
        ++col; i += static_cast<size_t>(len);
    }
    d.byteToCol[line.size()] = col;
    return d;
}

void EditorView::drawSquiggle(HDC dc, int x1, int x2, int y, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old = SelectObject(dc, pen);
    int step = std::max(2, MulDiv(2, dpi_, 96));
    int up = 0;
    MoveToEx(dc, x1, y, nullptr);
    for (int x = x1 + step; x <= x2 + step; x += step) {
        up ^= 1;
        LineTo(dc, std::min(x, x2), y - (up ? step : 0));
    }
    SelectObject(dc, old);
    DeleteObject(pen);
}

static COLORREF hlColor(const studio::Theme& t, studio::Hl k) {
    switch (k) {
        case studio::Hl::Keyword: return rgb(t[Slot::Keyword]);
        case studio::Hl::Type: return rgb(t[Slot::Type]);
        case studio::Hl::Builtin: return rgb(t[Slot::Builtin]);
        case studio::Hl::String: return rgb(t[Slot::String]);
        case studio::Hl::Number: return rgb(t[Slot::Number]);
        case studio::Hl::Comment: return rgb(t[Slot::Comment]);
        case studio::Hl::Operator: return rgb(t[Slot::Operator]);
        case studio::Hl::Constant: return rgb(t[Slot::Constant]);
        case studio::Hl::Error: return rgb(t[Slot::Error]);
        default: return rgb(t[Slot::EditorFg]);
    }
}

void EditorView::drawLine(HDC dc, int row, int y, const RECT& rc) {
    const studio::Theme& t = *theme_;
    const std::string& s = doc_->line(row);
    DisplayLine dl = toDisplay(s);
    const int x0 = textLeft() - left_ * charW_;
    const Pos caret = doc_->caret();
    const bool hasSel = doc_->hasSelection();

    if (row == caret.line && settings_->highlightCurrentLine && !hasSel) {
        RECT r = {gutterW_, y, rc.right, y + lineH_};
        fillRect(dc, r, win::rgb(t[Slot::CurrentLine]));
    }
    if (hasSel) {
        Pos a = doc_->selStart(), b = doc_->selEnd();
        if (row >= a.line && row <= b.line) {
            int c1 = (row == a.line) ? dl.byteToCol[static_cast<size_t>(std::min<size_t>(static_cast<size_t>(a.col), s.size()))] : 0;
            int c2 = (row == b.line) ? dl.byteToCol[static_cast<size_t>(std::min<size_t>(static_cast<size_t>(b.col), s.size()))] : dl.byteToCol[s.size()] + 1;
            if (c2 > c1) {
                RECT r = {x0 + c1 * charW_, y, x0 + c2 * charW_, y + lineH_};
                fillRect(dc, r, win::rgb(t[Slot::Selection]));
            }
        }
    }
    if (settings_->matchBrackets) {
        for (const Pos& p : {brA_, brB_}) {
            if (p.line == row && p.col >= 0 && p.col < static_cast<int>(s.size())) {
                int c = dl.byteToCol[static_cast<size_t>(p.col)];
                RECT r = {x0 + c * charW_, y, x0 + (c + 1) * charW_, y + lineH_};
                fillRect(dc, r, win::rgb(t[Slot::BracketMatch]));
            }
        }
    }
    if (settings_->showIndentGuides) {
        size_t lead = s.find_first_not_of(" \t");
        if (lead != std::string::npos) {
            int cols = dl.byteToCol[lead], tab = std::max(1, doc_->options().tabSize);
            for (int c = tab; c < cols; c += tab) {
                RECT r = {x0 + c * charW_, y, x0 + c * charW_ + 1, y + lineH_};
                fillRect(dc, r, win::rgb(t[Slot::IndentGuide]));
            }
        }
    }

    // text, coloured by token class
    SetBkMode(dc, TRANSPARENT);
    int visibleCols = (rc.right - textLeft()) / std::max(1, charW_) + 2;
    auto drawRange = [&](size_t byteStart, size_t byteLen, COLORREF color) {
        size_t be = std::min(byteStart + byteLen, s.size());
        if (byteStart >= be) return;
        int c1 = dl.byteToCol[byteStart], c2 = dl.byteToCol[be];
        if (c2 <= c1 || c2 < left_ || c1 > left_ + visibleCols) return;
        std::wstring seg = dl.text.substr(static_cast<size_t>(c1), static_cast<size_t>(c2 - c1));
        std::vector<int> dx(seg.size(), charW_);       // force a fixed advance so every glyph stays on the grid
        SetTextColor(dc, color);
        ExtTextOutW(dc, x0 + c1 * charW_, y + ascentPad_, 0, nullptr, seg.c_str(), static_cast<UINT>(seg.size()), dx.data());
    };
    size_t cur = 0;
    COLORREF plain = win::rgb(t[Slot::EditorFg]);
    for (const studio::Span& sp : studio::highlightLine(s)) {
        if (static_cast<size_t>(sp.start) > cur) drawRange(cur, static_cast<size_t>(sp.start) - cur, plain);
        drawRange(static_cast<size_t>(sp.start), static_cast<size_t>(sp.len), hlColor(t, sp.kind));
        cur = static_cast<size_t>(sp.start + sp.len);
    }
    if (cur < s.size()) drawRange(cur, s.size() - cur, plain);

    if (settings_->showWhitespace) {
        COLORREF c = win::rgb(t[Slot::IndentGuide]);
        int dot = std::max(2, MulDiv(2, dpi_, 96));
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == ' ') {
                int cx = x0 + dl.byteToCol[i] * charW_ + charW_ / 2, cy = y + lineH_ / 2;
                RECT r = {cx - dot / 2, cy - dot / 2, cx - dot / 2 + dot, cy - dot / 2 + dot};
                fillRect(dc, r, c);
            } else if (s[i] == '\t') {
                RECT r = {x0 + dl.byteToCol[i] * charW_ + 2, y + lineH_ / 2, x0 + dl.byteToCol[i + 1] * charW_ - 2, y + lineH_ / 2 + 1};
                fillRect(dc, r, c);
            }
        }
    }

    // error / warning squiggles
    if (diags_) {
        for (const pcs::Diagnostic& d : *diags_) {
            if (d.line - 1 != row) continue;
            int startByte = std::max(0, std::min(d.col - 1, static_cast<int>(s.size())));
            int endByte = startByte + 1;
            for (const studio::LTok& tk : studio::scanLine(s)) {
                if (startByte >= tk.start && startByte < tk.end()) { startByte = tk.start; endByte = tk.end(); break; }
            }
            if (d.col <= 1 && startByte == 0) endByte = static_cast<int>(s.size());   // whole-line problems
            endByte = std::min(endByte, static_cast<int>(s.size()));
            if (endByte <= startByte) { startByte = std::max(0, static_cast<int>(s.size()) - 1); endByte = static_cast<int>(s.size()); }
            int c1 = dl.byteToCol[static_cast<size_t>(startByte)], c2 = dl.byteToCol[static_cast<size_t>(endByte)];
            if (c2 <= c1) c2 = c1 + 1;
            COLORREF col = win::rgb(d.sev == pcs::Severity::Error ? t[Slot::Error] : t[Slot::Warning]);
            drawSquiggle(dc, x0 + c1 * charW_, x0 + c2 * charW_, y + lineH_ - 2, col);
        }
    }
}

void EditorView::paint(HDC dc, const RECT& rc) {
    const studio::Theme& t = *theme_;
    fillRect(dc, rc, win::rgb(t[Slot::EditorBg]));
    HGDIOBJ oldFont = SelectObject(dc, font_);
    brA_ = brB_ = Pos{-1, -1};
    if (settings_->matchBrackets) {
        Pos c = doc_->caret();
        Pos m = doc_->matchingBracket(c);
        if (m.line >= 0) {
            // highlight the bracket next to the caret and its partner
            const std::string& l = doc_->line(c.line);
            bool onBracket = c.col < static_cast<int>(l.size()) && std::string("()[]").find(l[static_cast<size_t>(c.col)]) != std::string::npos;
            brA_ = onBracket ? c : Pos{c.line, c.col - 1};
            brB_ = m;
        }
    }
    int rows = static_cast<int>(rc.bottom) / std::max(1, lineH_) + 2;
    // text area (clipped so horizontal scrolling never draws over the gutter)
    int saved = SaveDC(dc);
    IntersectClipRect(dc, gutterW_, 0, rc.right, rc.bottom);
    for (int r = 0; r < rows; ++r) {
        int line = top_ + r;
        if (line >= doc_->lineCount()) break;
        drawLine(dc, line, r * lineH_, rc);
    }
    RestoreDC(dc, saved);
    // gutter
    if (gutterW_ > 0) {
        RECT g = {0, 0, gutterW_, rc.bottom};
        fillRect(dc, g, win::rgb(t[Slot::GutterBg]));
        for (int r = 0; r < rows; ++r) {
            int line = top_ + r;
            if (line >= doc_->lineCount()) break;
            bool active = line == doc_->caret().line;
            RECT nr = {0, r * lineH_, gutterW_ - 2 * charW_ + charW_ / 2 + MulDiv(2, dpi_, 96), (r + 1) * lineH_};
            nr.right = gutterW_ - charW_;
            drawLabel(dc, std::to_wstring(line + 1), nr, win::rgb(active ? t[Slot::GutterFgActive] : t[Slot::GutterFg]), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        RECT edge = {gutterW_ - 1, 0, gutterW_, rc.bottom};
        fillRect(dc, edge, win::rgb(t[Slot::IndentGuide]));
    }
    SelectObject(dc, oldFont);
}

// ----------------------------------------------------------------------- editing commands
void EditorView::afterEdit(const std::string& typed, bool refilter) {
    if (!doc_) return;
    Pos c = doc_->caret();
    bool changed = doc_->version() != lastVersion_;
    lastVersion_ = doc_->version();
    computeGutter();
    ensureCaretVisible();
    updateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    updateCaret();
    if (changed) notify(ED_CHANGED);
    if (changed || c != lastCaret_) { lastCaret_ = c; notify(ED_CARET); }

    if (!settings_ || !settings_->autoComplete) { popup_.hide(); return; }
    if (!typed.empty()) {
        if (typed.size() == 1 && studio::isCompletionTrigger(typed[0])) refreshCompletion(false);
        else popup_.hide();
    } else if (refilter && popup_.visible()) refreshCompletion(false);
    else if (popup_.visible() && (c.line != compLine_ || c.col < compStart_)) popup_.hide();
}

void EditorView::refreshCompletion(bool forced) {
    if (!doc_ || !settings_) return;
    Pos c = doc_->caret();
    studio::SymbolIndex idx = studio::SymbolIndex::build(doc_->lines());
    static const std::vector<studio::Snippet> none;
    studio::CompletionResult res = studio::complete(doc_->lines(), c.line, c.col, idx, snippets_ ? *snippets_ : none, forced);
    int prefix = res.replaceEnd - res.replaceStart;
    if (res.items.empty() || (!forced && !res.memberAccess && prefix < settings_->autoCompleteMinChars)) { popup_.hide(); return; }
    compLine_ = c.line; compStart_ = res.replaceStart; compEnd_ = res.replaceEnd;
    int col = doc_->displayColumn(c.line, compStart_);
    POINT pt = {textLeft() + (col - left_) * charW_, (c.line - top_) * lineH_};
    ClientToScreen(hwnd_, &pt);
    popup_.show(res.items, pt.x, pt.y, pt.y + lineH_, lineH_);
}

void EditorView::cmdSuggest() { if (doc_) refreshCompletion(true); }

void EditorView::acceptCompletion(const studio::CompletionItem& item) {
    if (!doc_) return;
    popup_.hide();
    doc_->applyCompletion(compLine_, compStart_, compEnd_, item.insert);
    afterEdit("");
    SetFocus(hwnd_);
}

void EditorView::cmdCut() {
    if (!doc_) return;
    if (!doc_->hasSelection()) doc_->selectLineAt(doc_->caret().line);
    std::string t = doc_->cut();
    if (!t.empty()) clipboardSet(hwnd_, t);
    afterEdit("");
}
void EditorView::cmdCopy() {
    if (!doc_) return;
    if (doc_->hasSelection()) clipboardSet(hwnd_, doc_->selectedText());
    else clipboardSet(hwnd_, doc_->line(doc_->caret().line) + "\n");
}
void EditorView::cmdPaste() {
    if (!doc_) return;
    std::string t = clipboardGet(hwnd_);
    if (t.empty()) return;
    doc_->insertText(t);
    afterEdit("");
}
void EditorView::cmdUndo() { if (doc_ && doc_->undo()) afterEdit(""); }
void EditorView::cmdRedo() { if (doc_ && doc_->redo()) afterEdit(""); }
void EditorView::cmdSelectAll() { if (doc_) { doc_->selectAll(); afterEdit(""); } }
void EditorView::cmdFormat() {
    if (!doc_ || !settings_) return;
    studio::FormatOptions fo;
    fo.tabSize = settings_->tabSize; fo.insertSpaces = settings_->insertSpaces; fo.trimTrailingWhitespace = true;
    doc_->formatDocument(fo);
    afterEdit("");
}
void EditorView::cmdToggleComment() { if (doc_) { doc_->toggleComment(); afterEdit(""); } }
void EditorView::cmdDuplicate() { if (doc_) { doc_->duplicateLines(); afterEdit(""); } }
void EditorView::cmdDeleteLine() { if (doc_) { doc_->deleteLines(); afterEdit(""); } }
void EditorView::cmdMoveLines(int dir) { if (doc_) { doc_->moveLines(dir); afterEdit(""); } }
void EditorView::cmdIndent(bool outdent) { if (doc_) { doc_->tab(outdent); afterEdit(""); } }

bool EditorView::onKeyDown(WPARAM vk) {
    if (!doc_) return false;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;

    if (popup_.visible()) {
        switch (vk) {
            case VK_UP: popup_.move(-1); return true;
            case VK_DOWN: popup_.move(1); return true;
            case VK_PRIOR: popup_.move(-8); return true;
            case VK_NEXT: popup_.move(8); return true;
            case VK_ESCAPE: popup_.hide(); return true;
            case VK_TAB: if (const studio::CompletionItem* s = popup_.selected()) { studio::CompletionItem it = *s; acceptCompletion(it); return true; } break;
            case VK_RETURN:
                if (const studio::CompletionItem* s = popup_.selected()) {
                    studio::CompletionItem it = *s;
                    std::string typed = doc_->line(compLine_).substr(static_cast<size_t>(compStart_), static_cast<size_t>(compEnd_ - compStart_));
                    if (it.insert == typed) { popup_.hide(); break; }       // nothing to complete: Enter is just a new line
                    acceptCompletion(it);
                    return true;
                }
                break;
            default: break;
        }
    }

    switch (vk) {
        case VK_LEFT: popup_.hide(); doc_->moveLeft(shift, ctrl); afterEdit(""); return true;
        case VK_RIGHT: popup_.hide(); doc_->moveRight(shift, ctrl); afterEdit(""); return true;
        case VK_UP:
            popup_.hide();
            if (alt && !shift && !ctrl) cmdMoveLines(-1); else { doc_->moveUp(shift); afterEdit(""); }
            return true;
        case VK_DOWN:
            popup_.hide();
            if (alt && !shift && !ctrl) cmdMoveLines(1); else { doc_->moveDown(shift); afterEdit(""); }
            return true;
        case VK_HOME: popup_.hide(); if (ctrl) doc_->docStart(shift); else doc_->home(shift); afterEdit(""); return true;
        case VK_END: popup_.hide(); if (ctrl) doc_->docEnd(shift); else doc_->end(shift); afterEdit(""); return true;
        case VK_PRIOR: popup_.hide(); doc_->moveUp(shift, std::max(1, visibleRows() - 1)); afterEdit(""); return true;
        case VK_NEXT: popup_.hide(); doc_->moveDown(shift, std::max(1, visibleRows() - 1)); afterEdit(""); return true;
        case VK_BACK: if (ctrl) doc_->deleteWordLeft(); else doc_->backspace(); afterEdit("", true); return true;
        case VK_DELETE:
            if (shift && !ctrl) { cmdCut(); return true; }
            if (ctrl) doc_->deleteWordRight(); else doc_->deleteForward();
            afterEdit("", true);
            return true;
        case VK_RETURN: popup_.hide(); doc_->newline(); afterEdit(""); return true;
        case VK_TAB: if (ctrl) return false; popup_.hide(); doc_->tab(shift); afterEdit(""); return true;
        case VK_ESCAPE: popup_.hide(); if (doc_->hasSelection()) { doc_->setCaret(doc_->caret()); afterEdit(""); } return true;
        case VK_INSERT: if (ctrl) { cmdCopy(); return true; } if (shift) { cmdPaste(); return true; } return false;
        case VK_SPACE: if (ctrl && !alt) { cmdSuggest(); return true; } return false;
        case VK_OEM_2: if (ctrl && !alt) { cmdToggleComment(); return true; } return false;
        default: break;
    }
    if (ctrl && !alt) {
        switch (vk) {
            case 'A': cmdSelectAll(); return true;
            case 'C': cmdCopy(); return true;
            case 'X': cmdCut(); return true;
            case 'V': cmdPaste(); return true;
            case 'Z': if (shift) cmdRedo(); else cmdUndo(); return true;
            case 'Y': cmdRedo(); return true;
            case 'D': cmdDuplicate(); return true;
            case 'K': if (shift) { cmdDeleteLine(); return true; } return false;
            case 'L': doc_->selectLineAt(doc_->caret().line); afterEdit(""); return true;
            case VK_OEM_6: cmdIndent(false); return true;    // Ctrl+]
            case VK_OEM_4: cmdIndent(true); return true;     // Ctrl+[
            default: break;
        }
    }
    return false;
}

void EditorView::onChar(wchar_t ch) {
    if (!doc_) return;
    if (ch < 32 || ch == 127) return;                     // Enter, Tab, Backspace and Ctrl+keys are handled in WM_KEYDOWN
    if ((GetKeyState(VK_CONTROL) < 0) && !(GetKeyState(VK_MENU) < 0)) return;
    std::wstring w;
    if (ch >= 0xD800 && ch <= 0xDBFF) { pendingHigh_ = ch; return; }
    if (ch >= 0xDC00 && ch <= 0xDFFF) {
        if (!pendingHigh_) return;
        w += pendingHigh_; w += ch; pendingHigh_ = 0;
    } else { pendingHigh_ = 0; w += ch; }
    std::string utf8 = narrow(w);
    doc_->typeChar(utf8);
    afterEdit(utf8);
}

void EditorView::showContextMenu(int sx, int sy) {
    HMENU m = CreatePopupMenu();
    UINT en = MF_STRING, dis = MF_STRING | MF_GRAYED;
    AppendMenuW(m, doc_ && doc_->canUndo() ? en : dis, IDM_EDIT_UNDO, L"Undo\tCtrl+Z");
    AppendMenuW(m, doc_ && doc_->canRedo() ? en : dis, IDM_EDIT_REDO, L"Redo\tCtrl+Y");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, en, IDM_EDIT_CUT, L"Cut\tCtrl+X");
    AppendMenuW(m, en, IDM_EDIT_COPY, L"Copy\tCtrl+C");
    AppendMenuW(m, en, IDM_EDIT_PASTE, L"Paste\tCtrl+V");
    AppendMenuW(m, en, IDM_EDIT_SELECTALL, L"Select All\tCtrl+A");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, en, IDM_EDIT_COMMENT, L"Toggle Comment\tCtrl+/");
    AppendMenuW(m, en, IDM_EDIT_FORMAT, L"Format Document\tShift+Alt+F");
    AppendMenuW(m, en, IDM_EDIT_SUGGEST, L"Show Suggestions\tCtrl+Space");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, sx, sy, 0, hwnd_, nullptr);
    DestroyMenu(m);
}

// ----------------------------------------------------------------------- window procedure
LRESULT EditorView::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            dpi_ = dpiOf(hwnd_);
            popup_.createFor(GetAncestor(GetParent(hwnd_), GA_ROOT));
            return 0;
        case WM_DESTROY:
            popup_.hide();
            break;
        case WM_ERASEBKGND: return 1;
        case WM_GETDLGCODE: return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT p; GetCursorPos(&p); ScreenToClient(hwnd_, &p);
                SetCursor(LoadCursorW(nullptr, p.x < gutterW_ ? IDC_ARROW : IDC_IBEAM));
                return TRUE;
            }
            break;
        case WM_SIZE:
            if (doc_) { ensureCaretVisible(); updateScrollBars(); updateCaret(); }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED_AFTERPARENT: rebuildFonts(); return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            RECT rc; GetClientRect(hwnd_, &rc);
            if (doc_ && settings_ && theme_ && font_) {
                HDC mem = CreateCompatibleDC(dc);
                HBITMAP bmp = CreateCompatibleBitmap(dc, std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom));
                HGDIOBJ old = SelectObject(mem, bmp);
                paint(mem, rc);
                BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteObject(bmp);
                DeleteDC(mem);
            } else {
                fillRect(dc, rc, theme_ ? win::rgb((*theme_)[Slot::EditorBg]) : RGB(30, 31, 34));
            }
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_SETFOCUS: focused_ = true; createCaretShape(); updateCaret(); notify(ED_FOCUS); return 0;
        case WM_KILLFOCUS: focused_ = false; DestroyCaret(); return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN:
            if (onKeyDown(wp)) return 0;
            break;
        case WM_CHAR: onChar(static_cast<wchar_t>(wp)); return 0;
        case WM_UNICHAR: if (wp == UNICODE_NOCHAR) return TRUE; break;
        case WM_COMMAND: SendMessageW(GetParent(hwnd_), WM_COMMAND, wp, lp); return 0;
        case WM_CONTEXTMENU: {
            int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
            if (x == -1 && y == -1) { POINT p = {textLeft(), lineH_}; ClientToScreen(hwnd_, &p); x = p.x; y = p.y; }
            SetFocus(hwnd_);
            showContextMenu(x, y);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!doc_) return 0;
            SetFocus(hwnd_);
            popup_.hide();
            int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
            DWORD now = GetMessageTime();
            Pos p = posFromPoint(x, y);
            if (x < gutterW_) { doc_->selectLineAt(p.line); }
            else if (clickCount_ == 2 && now - lastClickTime_ <= GetDoubleClickTime()) { doc_->selectLineAt(p.line); clickCount_ = 3; }
            else { clickCount_ = 1; doc_->setCaret(p, GetKeyState(VK_SHIFT) < 0); dragging_ = true; SetCapture(hwnd_); }
            lastClickTime_ = now;
            afterEdit("");
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (!doc_) return 0;
            int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
            if (x >= gutterW_) { doc_->selectWordAt(posFromPoint(x, y)); clickCount_ = 2; lastClickTime_ = GetMessageTime(); afterEdit(""); }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (dragging_ && doc_) {
                int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
                RECT rc; GetClientRect(hwnd_, &rc);
                if (y < 0) scrollBy(-1); else if (y > rc.bottom) scrollBy(1);
                doc_->setCaret(posFromPoint(x, y), true);
                afterEdit("");
            }
            return 0;
        }
        case WM_LBUTTONUP:
            if (dragging_) { dragging_ = false; if (GetCapture() == hwnd_) ReleaseCapture(); }
            return 0;
        case WM_CAPTURECHANGED: dragging_ = false; return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (LOWORD(wp) & MK_CONTROL) { notify(ED_ZOOM, delta > 0 ? 1 : -1); return 0; }
            if (LOWORD(wp) & MK_SHIFT) { left_ = std::max(0, left_ - (delta > 0 ? 6 : -6)); updateScrollBars(); InvalidateRect(hwnd_, nullptr, FALSE); updateCaret(); return 0; }
            scrollBy(delta > 0 ? -3 : 3);
            return 0;
        }
        case WM_VSCROLL: case WM_HSCROLL: {
            if (!doc_) return 0;
            bool vert = msg == WM_VSCROLL;
            SCROLLINFO si = {};
            si.cbSize = sizeof(si); si.fMask = SIF_ALL;
            GetScrollInfo(hwnd_, vert ? SB_VERT : SB_HORZ, &si);
            int pos = si.nPos, page = static_cast<int>(si.nPage);
            switch (LOWORD(wp)) {
                case SB_LINEUP: pos -= 1; break;
                case SB_LINEDOWN: pos += 1; break;
                case SB_PAGEUP: pos -= std::max(1, page - 1); break;
                case SB_PAGEDOWN: pos += std::max(1, page - 1); break;
                case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
                case SB_TOP: pos = 0; break;
                case SB_BOTTOM: pos = si.nMax; break;
                default: break;
            }
            if (vert) top_ = std::max(0, std::min(pos, doc_->lineCount() - 1));
            else left_ = std::max(0, pos);
            popup_.hide();
            updateScrollBars();
            InvalidateRect(hwnd_, nullptr, FALSE);
            updateCaret();
            return 0;
        }
    }
    return Widget::handle(msg, wp, lp);
}

} // namespace win
