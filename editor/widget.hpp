// Base class for the IDE's custom-drawn windows plus the small widgets built on it (toolbar, tab bar, status bar).
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "theme.hpp"

namespace win {

inline void fillRect(HDC dc, const RECT& r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
inline void drawLabel(HDC dc, const std::wstring& s, RECT r, COLORREF c, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX) {
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), static_cast<int>(s.size()), &r, flags);
}
inline int textWidth(HDC dc, const std::wstring& s) {
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, s.c_str(), static_cast<int>(s.size()), &sz);
    return sz.cx;
}

class Widget {
public:
    Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;
    virtual ~Widget();
    HWND hwnd() const { return hwnd_; }
    bool create(HWND parent, const wchar_t* className, DWORD style, DWORD exStyle, int id);

protected:
    virtual LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    HWND hwnd_ = nullptr;

private:
    static LRESULT CALLBACK thunk(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    static void ensureClass(const wchar_t* className);
};

// ------------------------------------------------------------------ toolbar
struct ToolbarItem { int id = 0; std::wstring label; bool enabled = true; bool accent = false; bool separator = false; };

class ToolbarView : public Widget {
public:
    void setItems(std::vector<ToolbarItem> items) { items_ = std::move(items); if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void setEnabled(int id, bool on);
    void setTheme(const studio::Theme* t) { theme_ = t; if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void setFont(HFONT f) { font_ = f; if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    int preferredHeight(int dpi) const { return MulDiv(36, dpi, 96); }
protected:
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) override;
private:
    std::vector<ToolbarItem> items_;
    const studio::Theme* theme_ = nullptr;
    HFONT font_ = nullptr;
    int hot_ = -1, down_ = -1;
    bool tracking_ = false;
    RECT itemRect(HDC dc, size_t index) const;
    int hitTest(int x, int y) const;
};

// ------------------------------------------------------------------ tab bar
struct TabInfo { std::wstring title; bool dirty = false; };
enum TabEvent { TAB_SELECT = 0, TAB_CLOSE = 1, TAB_NEW = 2 };

class TabBar : public Widget {
public:
    void setTabs(std::vector<TabInfo> tabs, int active);
    int active() const { return active_; }
    void setTheme(const studio::Theme* t) { theme_ = t; if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    void setFont(HFONT f) { font_ = f; if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    int preferredHeight(int dpi) const { return MulDiv(34, dpi, 96); }
protected:
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) override;
private:
    std::vector<TabInfo> tabs_;
    int active_ = -1, hot_ = -1, hotClose_ = -1;
    int scroll_ = 0;
    bool tracking_ = false;
    const studio::Theme* theme_ = nullptr;
    HFONT font_ = nullptr;
    struct Layout { std::vector<RECT> tabs; std::vector<RECT> closes; RECT plus; };
    Layout layout(int width) const;
};

// ------------------------------------------------------------------ status bar
class StatusView : public Widget {
public:
    void setLeft(const std::wstring& s) { left_ = s; refresh(); }
    void setRight(std::vector<std::wstring> parts) { right_ = std::move(parts); refresh(); }
    void setTheme(const studio::Theme* t) { theme_ = t; refresh(); }
    void setFont(HFONT f) { font_ = f; refresh(); }
    int preferredHeight(int dpi) const { return MulDiv(24, dpi, 96); }
protected:
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) override;
private:
    void refresh() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }
    std::wstring left_;
    std::vector<std::wstring> right_;
    const studio::Theme* theme_ = nullptr;
    HFONT font_ = nullptr;
};

} // namespace win
