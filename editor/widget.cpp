#include "widget.hpp"
#include <algorithm>
#include "resource.h"
#include "win_util.hpp"

namespace win {

using studio::Slot;

// ------------------------------------------------------------------ base
Widget::~Widget() {
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

void Widget::ensureClass(const wchar_t* name) {
    WNDCLASSEXW probe = {};
    if (GetClassInfoExW(GetModuleHandleW(nullptr), name, &probe)) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &Widget::thunk;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

bool Widget::create(HWND parent, const wchar_t* className, DWORD style, DWORD exStyle, int id) {
    ensureClass(className);
    HWND h = CreateWindowExW(exStyle, className, L"", style, 0, 0, 10, 10, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), this);
    return h != nullptr;
}

LRESULT CALLBACK Widget::thunk(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Widget* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<Widget*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = h;
    } else {
        self = reinterpret_cast<Widget*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    LRESULT r = self->handle(msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
    }
    return r;
}

LRESULT Widget::handle(UINT msg, WPARAM wp, LPARAM lp) { return DefWindowProcW(hwnd_, msg, wp, lp); }

// ------------------------------------------------------------------ helpers for double-buffered painting
namespace {
struct BufferedPaint {
    HWND h; PAINTSTRUCT ps; HDC dc; HDC mem; HBITMAP bmp; HBITMAP old; RECT rc;
    explicit BufferedPaint(HWND hwnd) : h(hwnd) {
        dc = BeginPaint(h, &ps);
        GetClientRect(h, &rc);
        mem = CreateCompatibleDC(dc);
        bmp = CreateCompatibleBitmap(dc, std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom));
        old = static_cast<HBITMAP>(SelectObject(mem, bmp));
    }
    ~BufferedPaint() {
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
    }
};
const studio::Theme& themeOrDefault(const studio::Theme* t) { return t ? *t : studio::defaultTheme(true); }
} // namespace

// ------------------------------------------------------------------ toolbar
void ToolbarView::setEnabled(int id, bool on) {
    for (ToolbarItem& it : items_) if (it.id == id) it.enabled = on;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

RECT ToolbarView::itemRect(HDC dc, size_t index) const {
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    int x = MulDiv(8, dpi, 96), pad = MulDiv(14, dpi, 96), gap = MulDiv(4, dpi, 96);
    RECT rc; GetClientRect(hwnd_, &rc);
    int top = MulDiv(5, dpi, 96), bottom = rc.bottom - MulDiv(5, dpi, 96);
    for (size_t i = 0; i < items_.size(); ++i) {
        int w = items_[i].separator ? MulDiv(9, dpi, 96) : textWidth(dc, items_[i].label) + 2 * pad;
        RECT r = {x, top, x + w, bottom};
        if (i == index) return r;
        x += w + gap;
    }
    return RECT{0, 0, 0, 0};
}

int ToolbarView::hitTest(int x, int y) const {
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = font_ ? SelectObject(dc, font_) : nullptr;
    int found = -1;
    POINT pt = {x, y};
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].separator) continue;
        RECT r = itemRect(dc, i);
        if (PtInRect(&r, pt)) { found = static_cast<int>(i); break; }
    }
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    return found;
}

LRESULT ToolbarView::handle(UINT msg, WPARAM wp, LPARAM lp) {
    const studio::Theme& t = themeOrDefault(theme_);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            BufferedPaint bp(hwnd_);
            fillRect(bp.mem, bp.rc, win::rgb(t[Slot::UiBg]));
            RECT line = {0, bp.rc.bottom - 1, bp.rc.right, bp.rc.bottom};
            fillRect(bp.mem, line, win::rgb(t[Slot::UiBorder]));
            HGDIOBJ old = font_ ? SelectObject(bp.mem, font_) : nullptr;
            for (size_t i = 0; i < items_.size(); ++i) {
                const ToolbarItem& it = items_[i];
                RECT r = itemRect(bp.mem, i);
                if (it.separator) {
                    RECT sep = {(r.left + r.right) / 2, r.top + 4, (r.left + r.right) / 2 + 1, r.bottom - 4};
                    fillRect(bp.mem, sep, win::rgb(t[Slot::UiBorder]));
                    continue;
                }
                COLORREF fg = win::rgb(t[Slot::UiFg]);
                if (it.enabled && (static_cast<int>(i) == down_ || static_cast<int>(i) == hot_)) {
                    fillRect(bp.mem, r, win::rgb(static_cast<int>(i) == down_ ? t[Slot::PopupSelBg] : t[Slot::CurrentLine]));
                    if (static_cast<int>(i) == down_) fg = win::rgb(t[Slot::PopupSelFg]);
                } else if (it.accent && it.enabled) {
                    fg = win::rgb(t[Slot::Accent]);
                }
                if (!it.enabled) fg = win::rgb(t[Slot::GutterFg]);
                drawLabel(bp.mem, it.label, r, fg, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            if (old) SelectObject(bp.mem, old);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int h = hitTest(static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
            if (h != hot_) { hot_ = h; InvalidateRect(hwnd_, nullptr, FALSE); }
            if (!tracking_) {
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                tracking_ = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; hot_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: {
            int h = hitTest(static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
            if (h >= 0 && items_[static_cast<size_t>(h)].enabled) { down_ = h; SetCapture(hwnd_); InvalidateRect(hwnd_, nullptr, FALSE); }
            return 0;
        }
        case WM_LBUTTONUP: {
            int was = down_;
            down_ = -1;
            if (GetCapture() == hwnd_) ReleaseCapture();
            InvalidateRect(hwnd_, nullptr, FALSE);
            int h = hitTest(static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp)));
            if (was >= 0 && h == was && items_[static_cast<size_t>(h)].enabled)
                SendMessageW(GetParent(hwnd_), WM_COMMAND, MAKEWPARAM(items_[static_cast<size_t>(h)].id, 0), reinterpret_cast<LPARAM>(hwnd_));
            return 0;
        }
    }
    return Widget::handle(msg, wp, lp);
}

// ------------------------------------------------------------------ tab bar
void TabBar::setTabs(std::vector<TabInfo> tabs, int active) {
    tabs_ = std::move(tabs);
    active_ = active;
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

TabBar::Layout TabBar::layout(int width) const {
    Layout L;
    HDC dc = GetDC(hwnd_);
    HGDIOBJ old = font_ ? SelectObject(dc, font_) : nullptr;
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    int pad = MulDiv(14, dpi, 96), closeW = MulDiv(22, dpi, 96), minW = MulDiv(80, dpi, 96), maxW = MulDiv(220, dpi, 96);
    int plusW = MulDiv(36, dpi, 96);
    RECT rc; GetClientRect(hwnd_, &rc);
    std::vector<int> w;
    int total = 0;
    for (const TabInfo& t : tabs_) {
        int tw = std::min(maxW, std::max(minW, textWidth(dc, t.title) + 2 * pad + closeW));
        w.push_back(tw); total += tw;
    }
    int avail = std::max(1, width - plusW);
    if (total > avail && !w.empty()) {   // shrink every tab evenly, down to a minimum
        int each = std::max(MulDiv(56, dpi, 96), avail / static_cast<int>(w.size()));
        for (int& x : w) x = std::min(x, each);
    }
    int x = -scroll_;
    for (size_t i = 0; i < w.size(); ++i) {
        RECT r = {x, MulDiv(4, dpi, 96), x + w[i], rc.bottom};
        L.tabs.push_back(r);
        L.closes.push_back(RECT{r.right - closeW, r.top, r.right - MulDiv(4, dpi, 96), r.bottom});
        x += w[i];
    }
    L.plus = RECT{x, MulDiv(4, dpi, 96), x + plusW, rc.bottom};
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd_, dc);
    return L;
}

LRESULT TabBar::handle(UINT msg, WPARAM wp, LPARAM lp) {
    const studio::Theme& t = themeOrDefault(theme_);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            BufferedPaint bp(hwnd_);
            fillRect(bp.mem, bp.rc, win::rgb(t[Slot::UiBg]));
            Layout L = layout(bp.rc.right);
            HGDIOBJ old = font_ ? SelectObject(bp.mem, font_) : nullptr;
            int dpi = GetDeviceCaps(bp.mem, LOGPIXELSX);
            for (size_t i = 0; i < tabs_.size(); ++i) {
                const RECT& r = L.tabs[i];
                bool act = static_cast<int>(i) == active_;
                fillRect(bp.mem, r, win::rgb(act ? t[Slot::TabActiveBg] : t[Slot::TabInactiveBg]));
                if (act) { RECT bar = {r.left, r.top, r.right, r.top + MulDiv(2, dpi, 96)}; fillRect(bp.mem, bar, win::rgb(t[Slot::Accent])); }
                RECT sep = {r.right - 1, r.top + 4, r.right, r.bottom - 4};
                if (!act) fillRect(bp.mem, sep, win::rgb(t[Slot::UiBorder]));
                RECT tr = {r.left + MulDiv(12, dpi, 96), r.top, L.closes[i].left, r.bottom};
                std::wstring title = tabs_[i].title;
                drawLabel(bp.mem, title, tr, win::rgb(t[Slot::TabFg]), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                // close button, or a dot when the document has unsaved changes
                const RECT& c = L.closes[i];
                bool hover = static_cast<int>(i) == hotClose_;
                if (hover) fillRect(bp.mem, RECT{c.left + 2, c.top + MulDiv(6, dpi, 96), c.right - 2, c.bottom - MulDiv(6, dpi, 96)}, win::rgb(t[Slot::CurrentLine]));
                const wchar_t* glyph = (tabs_[i].dirty && !hover) ? L"\x25CF" : L"\x2715";
                drawLabel(bp.mem, glyph, c, win::rgb(tabs_[i].dirty ? t[Slot::Accent] : t[Slot::GutterFg]), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            drawLabel(bp.mem, L"+", L.plus, win::rgb(t[Slot::UiFg]), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            RECT line = {0, bp.rc.bottom - 1, bp.rc.right, bp.rc.bottom};
            fillRect(bp.mem, line, win::rgb(t[Slot::UiBorder]));
            if (old) SelectObject(bp.mem, old);
            return 0;
        }
        case WM_MOUSEMOVE: {
            RECT rc; GetClientRect(hwnd_, &rc);
            Layout L = layout(rc.right);
            POINT p = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
            int hot = -1, hc = -1;
            for (size_t i = 0; i < L.tabs.size(); ++i) {
                if (PtInRect(&L.tabs[i], p)) { hot = static_cast<int>(i); if (PtInRect(&L.closes[i], p)) hc = static_cast<int>(i); }
            }
            if (hot != hot_ || hc != hotClose_) { hot_ = hot; hotClose_ = hc; InvalidateRect(hwnd_, nullptr, FALSE); }
            if (!tracking_) {
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                tracking_ = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE: tracking_ = false; hot_ = hotClose_ = -1; InvalidateRect(hwnd_, nullptr, FALSE); return 0;
        case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: case WM_MBUTTONUP: {
            RECT rc; GetClientRect(hwnd_, &rc);
            Layout L = layout(rc.right);
            POINT p = {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
            if (PtInRect(&L.plus, p)) {
                if (msg != WM_MBUTTONUP) SendMessageW(GetParent(hwnd_), WM_PCS_TABS, TAB_NEW, -1);
                return 0;
            }
            for (size_t i = 0; i < L.tabs.size(); ++i) {
                if (!PtInRect(&L.tabs[i], p)) continue;
                if (msg == WM_MBUTTONUP || PtInRect(&L.closes[i], p)) SendMessageW(GetParent(hwnd_), WM_PCS_TABS, TAB_CLOSE, static_cast<LPARAM>(i));
                else SendMessageW(GetParent(hwnd_), WM_PCS_TABS, TAB_SELECT, static_cast<LPARAM>(i));
                return 0;
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            RECT rc; GetClientRect(hwnd_, &rc);
            Layout L = layout(rc.right);
            int total = L.plus.right + scroll_;
            int maxScroll = std::max(0, total - static_cast<int>(rc.right));
            scroll_ = std::max(0, std::min(maxScroll, scroll_ - (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 60 : -60)));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
    }
    return Widget::handle(msg, wp, lp);
}

// ------------------------------------------------------------------ status bar
LRESULT StatusView::handle(UINT msg, WPARAM wp, LPARAM lp) {
    const studio::Theme& t = themeOrDefault(theme_);
    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            BufferedPaint bp(hwnd_);
            fillRect(bp.mem, bp.rc, win::rgb(t[Slot::StatusBg]));
            HGDIOBJ old = font_ ? SelectObject(bp.mem, font_) : nullptr;
            int dpi = GetDeviceCaps(bp.mem, LOGPIXELSX);
            int pad = MulDiv(12, dpi, 96);
            int x = bp.rc.right - pad;
            for (size_t i = right_.size(); i-- > 0;) {
                int w = textWidth(bp.mem, right_[i]);
                RECT r = {x - w, 0, x, bp.rc.bottom};
                drawLabel(bp.mem, right_[i], r, win::rgb(t[Slot::StatusFg]), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                x -= w + pad * 2;
            }
            RECT lr = {pad, 0, std::max(pad, x), bp.rc.bottom};
            drawLabel(bp.mem, left_, lr, win::rgb(t[Slot::StatusFg]), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            if (old) SelectObject(bp.mem, old);
            return 0;
        }
    }
    return Widget::handle(msg, wp, lp);
}

} // namespace win
