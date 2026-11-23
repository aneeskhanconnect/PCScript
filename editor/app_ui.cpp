// PCScript Studio - main window, layout, menus, explorer, theming and command dispatch.
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <richedit.h>
#include <algorithm>
#include <set>
#include "app.hpp"
#include "files.hpp"
#include "resource.h"
#include "win_util.hpp"

namespace fs = std::filesystem;
namespace files = studio::files;
using studio::Slot;

static const wchar_t* kMainClass = L"PCSStudioMain";

static LRESULT CALLBACK mainThunk(HWND h, UINT m, WPARAM w, LPARAM l) {
    App* self = nullptr;
    if (m == WM_NCCREATE) {
        self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(h, m, w, l);
    return self->handle(h, m, w, l);
}

// ======================================================================= creation
bool App::createWindows(int nCmdShow) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = mainThunk;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);

    const studio::WindowState& ws = profile_->session.window;
    int x = ws.x < 0 ? CW_USEDEFAULT : ws.x, y = ws.y < 0 ? CW_USEDEFAULT : ws.y;
    hwnd_ = CreateWindowExW(WS_EX_ACCEPTFILES, kMainClass, L"PCScript Studio", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            x, y, ws.w, ws.h, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    adoptSession();
    if (opt_.selfTest) return true;
    ShowWindow(hwnd_, ws.maximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(hwnd_);
    focusEditor();
    return true;
}

static HWND makeChild(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int id) {
    return CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
}

static void addCol(HWND lv, int i, const wchar_t* text, int w) {
    LVCOLUMNW c = {};
    c.mask = LVCF_TEXT | LVCF_WIDTH;
    c.pszText = const_cast<LPWSTR>(text);
    c.cx = w;
    SendMessageW(lv, LVM_INSERTCOLUMNW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&c));
}

LRESULT App::handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) hwnd_ = h;
    switch (msg) {
        case WM_CREATE: {
            dpi_ = win::dpiOf(h);
            uiFont_ = win::makeUiFont(dpi_);
            toolbar_.create(h, L"PCSToolbar", WS_CHILD | WS_VISIBLE, 0, IDC_TOOLBAR);
            tabbar_.create(h, L"PCSTabBar", WS_CHILD | WS_VISIBLE, 0, IDC_TABS);
            status_.create(h, L"PCSStatus", WS_CHILD | WS_VISIBLE, 0, IDC_STATUS);
            editor_.createView(h);
            explorer_ = makeChild(h, WC_TREEVIEWW, L"", TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_NOHSCROLL | WS_CLIPSIBLINGS, 0, IDC_EXPLORER);
            LoadLibraryW(L"Msftedit.dll");
            console_ = makeChild(h, L"RICHEDIT50W", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS, 0, IDC_CONSOLE);
            SendMessageW(console_, EM_EXLIMITTEXT, 0, 64 * 1024 * 1024);
            input_ = makeChild(h, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS, WS_EX_CLIENTEDGE, IDC_INPUT);
            SetWindowSubclass(input_, &App::inputProc, 1, reinterpret_cast<DWORD_PTR>(this));
            sendBtn_ = makeChild(h, L"BUTTON", L"Send", BS_PUSHBUTTON | WS_TABSTOP | WS_CLIPSIBLINGS, 0, IDC_SENDINPUT);
            problems_ = makeChild(h, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_CLIPSIBLINGS, 0, IDC_PROBLEMS);
            SendMessageW(problems_, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            addCol(problems_, 0, L"", win::scale(26, dpi_));
            addCol(problems_, 1, L"Code", win::scale(70, dpi_));
            addCol(problems_, 2, L"Line", win::scale(60, dpi_));
            addCol(problems_, 3, L"Message", win::scale(900, dpi_));
            ShowWindow(problems_, SW_HIDE);

            std::vector<win::ToolbarItem> items;
            auto it = [&](int id, const wchar_t* t, bool accent = false) { win::ToolbarItem x; x.id = id; x.label = t; x.accent = accent; items.push_back(x); };
            auto sep = [&]() { win::ToolbarItem x; x.separator = true; items.push_back(x); };
            it(IDM_RUN_RUN, L"\x25B6  Run", true);
            it(IDM_RUN_STOP, L"\x25A0  Stop");
            sep();
            it(IDM_FILE_NEW, L"New"); it(IDM_FILE_OPEN, L"Open"); it(IDM_FILE_SAVE, L"Save");
            sep();
            it(IDM_EDIT_FIND, L"Find"); it(IDM_EDIT_FORMAT, L"Format"); it(IDM_RUN_CHECK, L"Check");
            sep();
            it(IDM_TOOLS_SETTINGS, L"Settings");
            toolbar_.setItems(items);
            toolbar_.setEnabled(IDM_RUN_STOP, false);

            find_.create(h);
            find_.getDocument = [this]() -> studio::Document* { Tab* t = activeTab(); return t ? t->doc.get() : nullptr; };
            find_.changed = [this]() { editor_.documentChanged(); };

            buildMenu();
            ACCEL keys[] = {
                {FVIRTKEY | FCONTROL, 'N', IDM_FILE_NEW}, {FVIRTKEY | FCONTROL, 'O', IDM_FILE_OPEN}, {FVIRTKEY | FCONTROL, 'S', IDM_FILE_SAVE},
                {FVIRTKEY | FCONTROL | FSHIFT, 'S', IDM_FILE_SAVEAS}, {FVIRTKEY | FCONTROL, 'W', IDM_FILE_CLOSE},
                {FVIRTKEY | FCONTROL, 'F', IDM_EDIT_FIND}, {FVIRTKEY | FCONTROL, 'H', IDM_EDIT_REPLACE}, {FVIRTKEY | FCONTROL, 'G', IDM_EDIT_GOTO},
                {FVIRTKEY, VK_F3, IDM_EDIT_FINDNEXT}, {FVIRTKEY | FSHIFT, VK_F3, IDM_EDIT_FINDPREV},
                {FVIRTKEY, VK_F5, IDM_RUN_RUN}, {FVIRTKEY | FSHIFT, VK_F5, IDM_RUN_STOP}, {FVIRTKEY, VK_F7, IDM_RUN_CHECK}, {FVIRTKEY, VK_F1, IDM_HELP_REFERENCE},
                {FVIRTKEY | FCONTROL, VK_OEM_COMMA, IDM_TOOLS_SETTINGS},
                {FVIRTKEY | FCONTROL, VK_OEM_PLUS, IDM_VIEW_ZOOMIN}, {FVIRTKEY | FCONTROL, VK_ADD, IDM_VIEW_ZOOMIN},
                {FVIRTKEY | FCONTROL, VK_OEM_MINUS, IDM_VIEW_ZOOMOUT}, {FVIRTKEY | FCONTROL, VK_SUBTRACT, IDM_VIEW_ZOOMOUT},
                {FVIRTKEY | FCONTROL, '0', IDM_VIEW_ZOOMRESET},
                {FVIRTKEY | FSHIFT | FALT, 'F', IDM_EDIT_FORMAT}, {FVIRTKEY | FCONTROL, VK_OEM_3, IDM_VIEW_OUTPUT},
                {FVIRTKEY | FCONTROL, 'B', IDM_VIEW_EXPLORER},
                {FVIRTKEY | FCONTROL, VK_TAB, IDM_TAB_NEXT}, {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, IDM_TAB_PREV}};
            accel_ = CreateAcceleratorTableW(keys, static_cast<int>(sizeof(keys) / sizeof(keys[0])));
            SetTimer(h, TIMER_CHECK, 400, nullptr);
            SetTimer(h, TIMER_AUTOSAVE, 1000, nullptr);
            SetTimer(h, TIMER_RECOVERY, 5000, nullptr);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) layoutChildren();
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = win::scale(640, dpi_); mm->ptMinTrackSize.y = win::scale(420, dpi_);
            return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            applyFonts();
            layoutChildren();
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc; GetClientRect(h, &rc);
            HDC mem = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom));
            HGDIOBJ old = SelectObject(mem, bmp);
            paintMain(mem, rc);
            BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_SETFOCUS: focusEditor(); return 0;
        case WM_LBUTTONDOWN: {
            int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
            if (handleMouseDown(x, y)) return 0;
            break;
        }
        case WM_MOUSEMOVE:
            if (dragSplit_) {
                RECT rc; GetClientRect(h, &rc);
                int x = static_cast<short>(LOWORD(lp)), y = static_cast<short>(HIWORD(lp));
                if (dragSplit_ == 1) explorerW_ = std::max(win::scale(120, dpi_), std::min(x, static_cast<int>(rc.right) / 2));
                else consoleH_ = std::max(win::scale(80, dpi_), static_cast<int>(rc.bottom) - y - (profile_->settings.showStatusBar ? status_.preferredHeight(dpi_) : 0));
                layoutChildren();
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (dragSplit_) { dragSplit_ = 0; ReleaseCapture(); return 0; }
            break;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
                if (showExplorer_ && PtInRect(&rcSplitV_, p)) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
                if (showPanel_ && PtInRect(&rcSplitH_, p)) { SetCursor(LoadCursorW(nullptr, IDC_SIZENS)); return TRUE; }
                for (const WelcomeLink& l : welcomeLinks_) if (PtInRect(&l.rc, p)) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
            }
            break;
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lp) == input_) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetTextColor(dc, win::rgb(theme_[Slot::ConsoleInput]));
                SetBkColor(dc, win::rgb(theme_[Slot::ConsoleBg]));
                SetDCBrushColor(dc, win::rgb(theme_[Slot::ConsoleBg]));
                return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
            }
            break;
        case WM_NOTIFY: {
            NMHDR* n = reinterpret_cast<NMHDR*>(lp);
            if (n->hwndFrom == explorer_) { onExplorerNotify(n); return 0; }
            if (n->hwndFrom == problems_) { onProblemsNotify(n); return 0; }
            break;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDC_SENDINPUT) { sendInput(); return 0; }
            if (id == IDC_INPUT || id == IDC_CONSOLE) return 0;
            if (onCommand(id)) return 0;
            break;
        }
        case WM_INITMENUPOPUP: refreshDynamicMenus(reinterpret_cast<HMENU>(wp)); return 0;
        case WM_TIMER:
            if (wp == TIMER_CHECK) { checkNow(false); updateStatus(); }
            else if (wp == TIMER_AUTOSAVE) autoSaveTick();
            else if (wp == TIMER_RECOVERY) saveSessionSnapshot();
            return 0;
        case WM_PCS_OUTPUT: flushOutput(); return 0;
        case WM_PCS_INPUT_REQUEST: onInputRequested(); return 0;
        case WM_PCS_FINISHED: onRunFinished(); return 0;
        case WM_PCS_EDITOR: onEditorEvent(static_cast<int>(wp), lp); return 0;
        case WM_PCS_TABS: onTabEvent(static_cast<int>(wp), static_cast<int>(lp)); return 0;
        case WM_DROPFILES: onDropFiles(reinterpret_cast<HDROP>(wp)); return 0;
        case WM_CLOSE:
            if (!endSession(true)) return 0;
            shuttingDown_ = true;
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            KillTimer(h, TIMER_CHECK); KillTimer(h, TIMER_AUTOSAVE); KillTimer(h, TIMER_RECOVERY);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ======================================================================= menus
static void mItem(HMENU m, UINT id, const wchar_t* t) { AppendMenuW(m, MF_STRING, id, t); }
static void mSep(HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); }
static HMENU mSub(HMENU parent, const wchar_t* t) {
    HMENU s = CreatePopupMenu();
    AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(s), t);
    return s;
}

void App::buildMenu() {
    menu_ = CreateMenu();
    HMENU f = mSub(menu_, L"&File");
    mItem(f, IDM_FILE_NEW, L"&New File\tCtrl+N");
    templateMenu_ = mSub(f, L"New From &Template");
    mItem(f, IDM_FILE_OPEN, L"&Open...\tCtrl+O");
    recentMenu_ = mSub(f, L"Open &Recent");
    exampleMenu_ = mSub(f, L"Open E&xample");
    mItem(f, IDM_FILE_OPENFOLDER, L"Open &Folder in Explorer Panel...");
    mSep(f);
    mItem(f, IDM_FILE_SAVE, L"&Save\tCtrl+S");
    mItem(f, IDM_FILE_SAVEAS, L"Save &As...\tCtrl+Shift+S");
    mItem(f, IDM_FILE_SAVEALL, L"Save A&ll");
    mSep(f);
    mItem(f, IDM_FILE_CLOSE, L"&Close Tab\tCtrl+W");
    mItem(f, IDM_FILE_CLOSEALL, L"Close All Tabs");
    mSep(f);
    mItem(f, IDM_FILE_EXIT, L"E&xit");

    editMenu_ = mSub(menu_, L"&Edit");
    HMENU e = editMenu_;
    mItem(e, IDM_EDIT_UNDO, L"&Undo\tCtrl+Z"); mItem(e, IDM_EDIT_REDO, L"&Redo\tCtrl+Y");
    mSep(e);
    mItem(e, IDM_EDIT_CUT, L"Cu&t\tCtrl+X"); mItem(e, IDM_EDIT_COPY, L"&Copy\tCtrl+C"); mItem(e, IDM_EDIT_PASTE, L"&Paste\tCtrl+V");
    mItem(e, IDM_EDIT_SELECTALL, L"Select &All\tCtrl+A");
    mSep(e);
    mItem(e, IDM_EDIT_FIND, L"&Find...\tCtrl+F"); mItem(e, IDM_EDIT_REPLACE, L"R&eplace...\tCtrl+H");
    mItem(e, IDM_EDIT_FINDNEXT, L"Find Ne&xt\tF3"); mItem(e, IDM_EDIT_FINDPREV, L"Find Pre&vious\tShift+F3");
    mItem(e, IDM_EDIT_GOTO, L"&Go to Line...\tCtrl+G");
    mSep(e);
    mItem(e, IDM_EDIT_FORMAT, L"&Format Document (pretty print)\tShift+Alt+F");
    mItem(e, IDM_EDIT_COMMENT, L"Toggle Co&mment\tCtrl+/");
    mItem(e, IDM_EDIT_INDENT, L"&Indent\tTab"); mItem(e, IDM_EDIT_OUTDENT, L"&Outdent\tShift+Tab");
    mItem(e, IDM_EDIT_DUPLICATE, L"&Duplicate Line\tCtrl+D"); mItem(e, IDM_EDIT_DELETELINE, L"Delete Lin&e\tCtrl+Shift+K");
    mItem(e, IDM_EDIT_MOVEUP, L"Move Line &Up\tAlt+Up"); mItem(e, IDM_EDIT_MOVEDOWN, L"Move Line Dow&n\tAlt+Down");
    mSep(e);
    mItem(e, IDM_EDIT_SUGGEST, L"Show &Suggestions\tCtrl+Space");

    viewMenu_ = mSub(menu_, L"&View");
    HMENU v = viewMenu_;
    mItem(v, IDM_VIEW_EXPLORER, L"&Explorer Panel\tCtrl+B"); mItem(v, IDM_VIEW_OUTPUT, L"&Output Panel\tCtrl+`");
    mItem(v, IDM_VIEW_PROBLEMS, L"&Problems"); mItem(v, IDM_VIEW_TOOLBAR, L"&Toolbar"); mItem(v, IDM_VIEW_STATUSBAR, L"&Status Bar");
    mSep(v);
    mItem(v, IDM_VIEW_LINENUMBERS, L"Line &Numbers"); mItem(v, IDM_VIEW_INDENTGUIDES, L"Indentation &Guides"); mItem(v, IDM_VIEW_WHITESPACE, L"Show &Whitespace");
    mSep(v);
    mItem(v, IDM_VIEW_ZOOMIN, L"Zoom &In\tCtrl+="); mItem(v, IDM_VIEW_ZOOMOUT, L"Zoom &Out\tCtrl+-"); mItem(v, IDM_VIEW_ZOOMRESET, L"&Reset Zoom\tCtrl+0");
    mSep(v);
    themeMenu_ = mSub(v, L"&Colour Theme");

    HMENU r = mSub(menu_, L"&Run");
    mItem(r, IDM_RUN_RUN, L"&Run Program\tF5"); mItem(r, IDM_RUN_STOP, L"&Stop\tShift+F5"); mItem(r, IDM_RUN_CHECK, L"&Check for Errors\tF7");
    mSep(r);
    mItem(r, IDM_RUN_CLEAR, L"C&lear Output");

    HMENU a = mSub(menu_, L"&Account");
    AppendMenuW(a, MF_STRING | MF_GRAYED, 0, L"Signed in as ...");
    mSep(a);
    mItem(a, IDM_ACCOUNT_SWITCH, L"&Switch User / Sign Out...");
    mItem(a, IDM_ACCOUNT_CREATE, L"&Create Account...");
    mItem(a, IDM_ACCOUNT_PASSWORD, L"Change &Password...");
    mItem(a, IDM_ACCOUNT_DELETE, L"&Delete This Account...");

    HMENU t = mSub(menu_, L"&Tools");
    mItem(t, IDM_TOOLS_SETTINGS, L"&Settings...\tCtrl+,"); mItem(t, IDM_TOOLS_EXTENSIONS, L"&Extensions...");
    mSep(t);
    mItem(t, IDM_TOOLS_DATAFOLDER, L"Open My Settings &Folder"); mItem(t, IDM_TOOLS_THEMESFOLDER, L"Open My &Themes Folder");

    HMENU hp = mSub(menu_, L"&Help");
    mItem(hp, IDM_HELP_REFERENCE, L"PCScript &Language Reference\tF1"); mItem(hp, IDM_HELP_CAMBRIDGE, L"&Cambridge Compatibility Notes");
    mItem(hp, IDM_HELP_SHORTCUTS, L"&Keyboard Shortcuts"); mSep(hp);
    mItem(hp, IDM_HELP_ABOUT, L"&About PCScript Studio");
    SetMenu(hwnd_, menu_);
}

static void clearMenu(HMENU m) { while (GetMenuItemCount(m) > 0) DeleteMenu(m, 0, MF_BYPOSITION); }

void App::refreshDynamicMenus(HMENU popup) {
    if (!popup) return;
    UINT en = MF_STRING, dis = MF_STRING | MF_GRAYED;
    if (popup == recentMenu_) {
        clearMenu(recentMenu_);
        const auto& rec = profile_->session.recentFiles;
        for (size_t i = 0; i < rec.size() && i < 12; ++i) AppendMenuW(recentMenu_, en, IDM_RECENT_BASE + static_cast<UINT>(i), win::widen(rec[i]).c_str());
        if (rec.empty()) AppendMenuW(recentMenu_, dis, 0, L"(no recent files)");
    } else if (popup == templateMenu_) {
        clearMenu(templateMenu_);
        auto tpls = exts_->templates();
        for (size_t i = 0; i < tpls.size() && i < 40; ++i) AppendMenuW(templateMenu_, en, IDM_TEMPLATE_BASE + static_cast<UINT>(i), win::widen(tpls[i].name).c_str());
        if (tpls.empty()) AppendMenuW(templateMenu_, dis, 0, L"(no templates - enable the Starter Templates extension)");
    } else if (popup == exampleMenu_) {
        clearMenu(exampleMenu_);
        examplePaths_.clear();
        fs::path docsExamples = files::fromUtf8(win::knownFolderDocuments()) / "PCScript" / "Examples";
        std::error_code ec;
        for (const fs::path& dir : {docsExamples, appDir_ / "examples"}) {
            if (!examplePaths_.empty() || !fs::is_directory(dir, ec)) continue;
            for (const files::DirEntry& d : files::listDir(dir))
                if (!d.isDir && files::hasExtension(d.full, ".pcs") && examplePaths_.size() < 60) examplePaths_.push_back(files::toUtf8(d.full));
        }
        for (size_t i = 0; i < examplePaths_.size(); ++i) AppendMenuW(exampleMenu_, en, IDM_EXAMPLE_BASE + static_cast<UINT>(i), win::widen(files::toUtf8(files::fromUtf8(examplePaths_[i]).filename())).c_str());
        if (examplePaths_.empty()) AppendMenuW(exampleMenu_, dis, 0, L"(no examples found)");
    } else if (popup == themeMenu_) {
        clearMenu(themeMenu_);
        for (size_t i = 0; i < allThemes_.size() && i < 60; ++i)
            AppendMenuW(themeMenu_, MF_STRING | (allThemes_[i].name == profile_->settings.theme ? MF_CHECKED : 0), IDM_THEME_BASE + static_cast<UINT>(i), win::widen(allThemes_[i].name).c_str());
    } else {
        updateMenuStates();
    }
}

void App::updateMenuStates() {
    Tab* t = activeTab();
    auto enable = [&](UINT id, bool on) { EnableMenuItem(menu_, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    auto check = [&](UINT id, bool on) { CheckMenuItem(menu_, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); };
    bool has = t != nullptr;
    enable(IDM_EDIT_UNDO, has && t->doc->canUndo()); enable(IDM_EDIT_REDO, has && t->doc->canRedo());
    for (UINT id : {IDM_EDIT_CUT, IDM_EDIT_COPY, IDM_EDIT_PASTE, IDM_EDIT_SELECTALL, IDM_EDIT_FIND, IDM_EDIT_REPLACE, IDM_EDIT_FINDNEXT, IDM_EDIT_FINDPREV, IDM_EDIT_GOTO,
                    IDM_EDIT_FORMAT, IDM_EDIT_COMMENT, IDM_EDIT_INDENT, IDM_EDIT_OUTDENT, IDM_EDIT_DUPLICATE, IDM_EDIT_DELETELINE, IDM_EDIT_MOVEUP, IDM_EDIT_MOVEDOWN,
                    IDM_EDIT_SUGGEST, IDM_FILE_SAVE, IDM_FILE_SAVEAS, IDM_FILE_CLOSE, IDM_RUN_CHECK})
        enable(id, has);
    enable(IDM_RUN_RUN, has && !running_); enable(IDM_RUN_STOP, running_);
    check(IDM_VIEW_EXPLORER, showExplorer_); check(IDM_VIEW_OUTPUT, showPanel_);
    check(IDM_VIEW_TOOLBAR, profile_->settings.showToolbar); check(IDM_VIEW_STATUSBAR, profile_->settings.showStatusBar);
    check(IDM_VIEW_LINENUMBERS, profile_->settings.showLineNumbers); check(IDM_VIEW_INDENTGUIDES, profile_->settings.showIndentGuides);
    check(IDM_VIEW_WHITESPACE, profile_->settings.showWhitespace);
    std::wstring who = L"Signed in as " + win::widen(accounts_->currentDisplayName().empty() ? accounts_->currentUser() : accounts_->currentDisplayName());
    HMENU acct = GetSubMenu(menu_, 4);
    if (acct) ModifyMenuW(acct, 0, MF_BYPOSITION | MF_STRING | MF_GRAYED, 0, who.c_str());
    bool real = accounts_->signedIn() && !accounts_->isGuest();
    enable(IDM_ACCOUNT_PASSWORD, real); enable(IDM_ACCOUNT_DELETE, real);
}

// ======================================================================= layout and painting
void App::showPanelTab(int tab) {
    panelTab_ = tab;
    showPanel_ = true;
    layoutChildren();
}

void App::layoutChildren() {
    if (!hwnd_ || !toolbar_.hwnd() || !profile_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    int W = rc.right, H = rc.bottom;
    auto D = [&](int v) { return MulDiv(v, dpi_, 96); };
    const studio::Settings& s = profile_->settings;
    int y = 0, tbH = toolbar_.preferredHeight(dpi_), tabH = tabbar_.preferredHeight(dpi_), stH = status_.preferredHeight(dpi_);
    if (s.showToolbar) { MoveWindow(toolbar_.hwnd(), 0, 0, W, tbH, TRUE); ShowWindow(toolbar_.hwnd(), SW_SHOW); y = tbH; }
    else ShowWindow(toolbar_.hwnd(), SW_HIDE);
    int bottom = H;
    if (s.showStatusBar) { MoveWindow(status_.hwnd(), 0, H - stH, W, stH, TRUE); ShowWindow(status_.hwnd(), SW_SHOW); bottom = H - stH; }
    else ShowWindow(status_.hwnd(), SW_HIDE);

    int split = D(5);
    int exW = showExplorer_ ? std::max(D(120), std::min(explorerW_, W / 2)) : 0;
    if (showExplorer_) {
        rcExplorerHeader_ = RECT{0, y, exW, y + D(26)};
        MoveWindow(explorer_, 0, y + D(26), exW, std::max(1, bottom - y - D(26)), TRUE);
        ShowWindow(explorer_, SW_SHOW);
        rcSplitV_ = RECT{exW, y, exW + split, bottom};
    } else { ShowWindow(explorer_, SW_HIDE); rcExplorerHeader_ = RECT{}; rcSplitV_ = RECT{}; }
    int x0 = showExplorer_ ? exW + split : 0, rw = std::max(1, W - x0);

    MoveWindow(tabbar_.hwnd(), x0, y, rw, tabH, TRUE);
    int top2 = y + tabH;
    int availH = std::max(0, bottom - top2);
    int ph = showPanel_ ? std::max(D(80), std::min(consoleH_, availH - D(100))) : 0;
    int panelTop = bottom - ph;
    int editorBottom = showPanel_ ? panelTop - split : bottom;
    rcEditor_ = RECT{x0, top2, x0 + rw, std::max(top2 + 1, editorBottom)};

    if (showPanel_) {
        rcSplitH_ = RECT{x0, panelTop - split, x0 + rw, panelTop};
        int hh = D(28);
        rcPanelHeader_ = RECT{x0, panelTop, x0 + rw, panelTop + hh};
        rcTab0_ = RECT{x0 + D(8), panelTop, x0 + D(98), panelTop + hh};
        rcTab1_ = RECT{rcTab0_.right + D(4), panelTop, rcTab0_.right + D(150), panelTop + hh};
        int cTop = panelTop + hh, cH = std::max(1, ph - hh);
        if (panelTab_ == 0) {
            int inH = D(30);
            MoveWindow(console_, x0, cTop, rw, std::max(1, cH - inH), TRUE);
            MoveWindow(input_, x0 + D(6), cTop + cH - inH + D(3), std::max(D(40), rw - D(92)), inH - D(6), TRUE);
            MoveWindow(sendBtn_, x0 + rw - D(78), cTop + cH - inH + D(3), D(72), inH - D(6), TRUE);
            ShowWindow(console_, SW_SHOW); ShowWindow(input_, SW_SHOW); ShowWindow(sendBtn_, SW_SHOW); ShowWindow(problems_, SW_HIDE);
        } else {
            MoveWindow(problems_, x0, cTop, rw, cH, TRUE);
            ShowWindow(problems_, SW_SHOW); ShowWindow(console_, SW_HIDE); ShowWindow(input_, SW_HIDE); ShowWindow(sendBtn_, SW_HIDE);
        }
    } else {
        rcSplitH_ = rcPanelHeader_ = rcTab0_ = rcTab1_ = RECT{};
        for (HWND c : {console_, input_, sendBtn_, problems_}) ShowWindow(c, SW_HIDE);
    }
    bool haveDoc = !tabs_.empty();
    if (haveDoc) { MoveWindow(editor_.hwnd(), rcEditor_.left, rcEditor_.top, rcEditor_.right - rcEditor_.left, rcEditor_.bottom - rcEditor_.top, TRUE); ShowWindow(editor_.hwnd(), SW_SHOW); }
    else ShowWindow(editor_.hwnd(), SW_HIDE);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::paintMain(HDC dc, const RECT& rc) {
    auto D = [&](int v) { return MulDiv(v, dpi_, 96); };
    win::fillRect(dc, rc, win::rgb(theme_[Slot::UiBg]));
    HGDIOBJ oldFont = SelectObject(dc, uiFont_);
    // explorer header
    if (showExplorer_ && rcExplorerHeader_.right > rcExplorerHeader_.left) {
        win::fillRect(dc, rcExplorerHeader_, win::rgb(theme_[Slot::UiBg]));
        RECT t = rcExplorerHeader_; t.left += D(10);
        std::string root = profile_->session.explorerRoot.empty() ? userWorkspace() : profile_->session.explorerRoot;
        std::wstring label = L"EXPLORER   " + win::widen(files::toUtf8(files::fromUtf8(root).filename()));
        win::drawLabel(dc, label, t, win::rgb(theme_[Slot::GutterFgActive]), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    // splitters
    for (const RECT* r : {&rcSplitV_, &rcSplitH_}) {
        if (r->right <= r->left) continue;
        win::fillRect(dc, *r, win::rgb(theme_[Slot::UiBg]));
        RECT line = (r == &rcSplitV_) ? RECT{r->left + (r->right - r->left) / 2, r->top, r->left + (r->right - r->left) / 2 + 1, r->bottom}
                                      : RECT{r->left, r->top + (r->bottom - r->top) / 2, r->right, r->top + (r->bottom - r->top) / 2 + 1};
        win::fillRect(dc, line, win::rgb(theme_[Slot::UiBorder]));
    }
    // output / problems header
    if (showPanel_ && rcPanelHeader_.right > rcPanelHeader_.left) {
        win::fillRect(dc, rcPanelHeader_, win::rgb(theme_[Slot::PanelBg]));
        Tab* t = activeTab();
        std::wstring p = L"PROBLEMS" + (t && !t->diags.empty() ? L" (" + std::to_wstring(t->diags.size()) + L")" : std::wstring());
        const std::pair<const RECT*, std::wstring> tabs[] = {{&rcTab0_, L"OUTPUT"}, {&rcTab1_, p}};
        for (int i = 0; i < 2; ++i) {
            bool act = panelTab_ == i;
            win::drawLabel(dc, tabs[i].second, *tabs[i].first, win::rgb(act ? theme_[Slot::GutterFgActive] : theme_[Slot::GutterFg]), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (act) win::fillRect(dc, RECT{tabs[i].first->left + D(6), tabs[i].first->bottom - D(2), tabs[i].first->right - D(6), tabs[i].first->bottom}, win::rgb(theme_[Slot::Accent]));
        }
        if (running_) {
            RECT r = rcPanelHeader_; r.right -= D(12);
            win::drawLabel(dc, waitingInput_ ? L"waiting for input" : L"running...", r, win::rgb(theme_[Slot::Accent]), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    // welcome page when no file is open
    welcomeLinks_.clear();
    if (tabs_.empty()) {
        win::fillRect(dc, rcEditor_, win::rgb(theme_[Slot::EditorBg]));
        int x = rcEditor_.left + D(60), y = rcEditor_.top + D(50);
        SelectObject(dc, bigFont_ ? bigFont_ : uiFont_);
        win::drawLabel(dc, L"PCScript Studio", RECT{x, y, rcEditor_.right - D(20), y + D(44)}, win::rgb(theme_[Slot::Keyword]));
        SelectObject(dc, uiFont_);
        win::drawLabel(dc, L"Pseudocode programming - write, run and debug Cambridge-style programs.", RECT{x, y + D(48), rcEditor_.right - D(20), y + D(72)}, win::rgb(theme_[Slot::Comment]));
        int ly = y + D(100);
        auto link = [&](const std::wstring& text, int action, const std::string& path) {
            RECT r = {x, ly, std::min(static_cast<int>(rcEditor_.right) - D(20), x + D(520)), ly + D(26)};
            win::drawLabel(dc, text, r, win::rgb(theme_[Slot::Accent]));
            welcomeLinks_.push_back({r, action, path});
            ly += D(28);
        };
        link(L"New file            Ctrl+N", 1, "");
        link(L"Open file...        Ctrl+O", 2, "");
        link(L"Open an example     File > Open Example", 3, "");
        ly += D(14);
        const auto& rec = profile_->session.recentFiles;
        if (!rec.empty()) {
            win::drawLabel(dc, L"Recent", RECT{x, ly, x + D(300), ly + D(24)}, win::rgb(theme_[Slot::GutterFgActive]));
            ly += D(28);
            for (size_t i = 0; i < rec.size() && i < 8; ++i) link(win::widen(rec[i]), 10, rec[i]);
        }
    }
    SelectObject(dc, oldFont);
}

bool App::handleMouseDown(int x, int y) {
    POINT p = {x, y};
    if (showExplorer_ && PtInRect(&rcSplitV_, p)) { dragSplit_ = 1; SetCapture(hwnd_); return true; }
    if (showPanel_ && PtInRect(&rcSplitH_, p)) { dragSplit_ = 2; SetCapture(hwnd_); return true; }
    if (showPanel_ && PtInRect(&rcTab0_, p)) { showPanelTab(0); return true; }
    if (showPanel_ && PtInRect(&rcTab1_, p)) { showPanelTab(1); return true; }
    for (const WelcomeLink& l : welcomeLinks_) {
        if (!PtInRect(&l.rc, p)) continue;
        if (l.action == 1) newFile();
        else if (l.action == 2) onCommand(IDM_FILE_OPEN);
        else if (l.action == 3) {
            POINT sp = {l.rc.left, l.rc.bottom};
            ClientToScreen(hwnd_, &sp);
            refreshDynamicMenus(exampleMenu_);
            TrackPopupMenu(exampleMenu_, TPM_LEFTALIGN, sp.x, sp.y, 0, hwnd_, nullptr);
        } else if (l.action == 10) openPath(l.path);
        return true;
    }
    return false;
}

// ======================================================================= fonts, theme, settings
void App::applyFonts() {
    dpi_ = win::dpiOf(hwnd_);
    for (HFONT* f : {&uiFont_, &uiFontBold_, &bigFont_, &consoleFont_}) if (*f) { DeleteObject(*f); *f = nullptr; }
    uiFont_ = win::makeUiFont(dpi_);
    uiFontBold_ = win::makeUiFont(dpi_, true);
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(22, dpi_, 72);
    lf.lfWeight = FW_SEMIBOLD;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
    bigFont_ = CreateFontIndirectW(&lf);
    consoleFont_ = win::makeFont(profile_->settings.consoleFontFamily, profile_->settings.consoleFontSize, dpi_);
    for (HWND c : {explorer_, problems_, sendBtn_}) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    SendMessageW(input_, WM_SETFONT, reinterpret_cast<WPARAM>(consoleFont_), TRUE);
    toolbar_.setFont(uiFont_); tabbar_.setFont(uiFont_); status_.setFont(uiFont_);
    editor_.rebuildFonts();
    // output panel font
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE;
    cf.yHeight = profile_->settings.consoleFontSize * 20;
    std::wstring face = win::widen(profile_->settings.consoleFontFamily);
    wcsncpy_s(cf.szFaceName, LF_FACESIZE, face.c_str(), _TRUNCATE);
    SendMessageW(console_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(console_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
}

void App::applyTheme() {
    theme_ = studio::defaultTheme(true);
    bool found = false;
    for (const studio::Theme& t : allThemes_) if (t.name == profile_->settings.theme) { theme_ = t; found = true; break; }
    if (!found) { profile_->settings.theme = theme_.name; }
    toolbar_.setTheme(&theme_); tabbar_.setTheme(&theme_); status_.setTheme(&theme_); editor_.setTheme(&theme_);
    SendMessageW(explorer_, TVM_SETBKCOLOR, 0, win::rgb(theme_[Slot::PanelBg]));
    SendMessageW(explorer_, TVM_SETTEXTCOLOR, 0, win::rgb(theme_[Slot::PanelFg]));
    SendMessageW(explorer_, TVM_SETLINECOLOR, 0, win::rgb(theme_[Slot::UiBorder]));
    SendMessageW(problems_, LVM_SETBKCOLOR, 0, win::rgb(theme_[Slot::PanelBg]));
    SendMessageW(problems_, LVM_SETTEXTBKCOLOR, 0, win::rgb(theme_[Slot::PanelBg]));
    SendMessageW(problems_, LVM_SETTEXTCOLOR, 0, win::rgb(theme_[Slot::PanelFg]));
    SendMessageW(console_, EM_SETBKGNDCOLOR, 0, win::rgb(theme_[Slot::ConsoleBg]));
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR;
    cf.crTextColor = win::rgb(theme_[Slot::ConsoleFg]);
    SendMessageW(console_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&cf));
    for (HWND c : {explorer_, problems_, console_, input_, sendBtn_}) win::themeControl(c, theme_.dark);
    win::themeTitleBar(hwnd_, theme_.dark);
    InvalidateRect(hwnd_, nullptr, TRUE);
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_ALLCHILDREN | RDW_INVALIDATE | RDW_UPDATENOW);
}

void App::applySettings(const studio::Settings& in, bool persist) {
    studio::Settings s = in;
    s.clamp();
    profile_->settings = s;
    for (auto& t : tabs_) applyDocOptions(*t->doc);
    showExplorer_ = s.showExplorer;
    showPanel_ = s.showConsole;
    applyFonts();
    applyTheme();
    layoutChildren();
    editor_.refresh();
    updateProblems();
    refreshTabs();
    updateStatus();
    if (persist) profile_->save();
}

void App::zoom(int delta) {
    studio::Settings s = profile_->settings;
    s.fontSize = delta == 0 ? 14 : s.fontSize + delta;
    applySettings(s, true);
}

void App::showSettings() {
    win::SettingsContext ctx;
    ctx.settings = profile_->settings;
    for (const studio::Theme& t : allThemes_) ctx.themeNames.push_back(t.name);
    ctx.previewTheme = [this](const std::string& name) { profile_->settings.theme = name; applyTheme(); };
    ctx.importTheme = [this](HWND owner) -> std::string {
        std::string path;
        if (!win::openFileDialog(owner, "", path)) return "";
        std::string text, err; studio::json::Value v; studio::Theme th;
        if (!files::readText(files::fromUtf8(path), text, &err) || !studio::json::parse(text, v, &err) || !studio::themeFromJson(v, th, &err)) {
            win::warn(owner, "Import theme", "That file is not a valid theme.\n\n" + err);
            return "";
        }
        if (!profile_->saveUserTheme(th, &err)) { win::warn(owner, "Import theme", err); return ""; }
        rebuildThemes();
        return th.name;
    };
    ctx.openThemesFolder = [this]() { files::ensureDir(profile_->themesDir()); win::shellOpen(hwnd_, files::toUtf8(profile_->themesDir())); };
    ctx.applyNow = [this](const studio::Settings& s) { applySettings(s, true); };
    bool ok = win::showSettingsDialog(hwnd_, ctx);
    if (ok) applySettings(ctx.settings, true);
    else applySettings(profile_->settings, false);   // drop any theme preview that was left over
}

void App::showExtensions() {
    win::ExtensionsContext ctx;
    ctx.manager = exts_.get();
    ctx.changed = [this]() {
        profile_->disabledExtensions = exts_->disabled();
        profile_->save();
        rebuildThemes();
        rebuildSnippets();
        applyTheme();
    };
    ctx.openFolder = [this]() { files::ensureDir(dataRoot_ / "extensions"); win::shellOpen(hwnd_, files::toUtf8(dataRoot_ / "extensions")); };
    win::showExtensionsDialog(hwnd_, ctx);
    ctx.changed();
}

void App::doSwitchUser() {
    if (!endSession(true)) return;
    accounts_->logout();
    if (find_.hwnd()) ShowWindow(find_.hwnd(), SW_HIDE);
    if (!signIn()) { shuttingDown_ = true; DestroyWindow(hwnd_); return; }
    startSession();
    adoptSession();
    updateTitle();
}

// ======================================================================= explorer
void App::populateExplorerChildren(HTREEITEM parent, const std::string& dir) {
    for (const files::DirEntry& e : files::listDir(files::fromUtf8(dir))) {
        explorerNodes_.push_back({files::toUtf8(e.full), e.isDir, false});
        std::wstring name = win::widen(e.name);
        TVINSERTSTRUCTW tvi = {};
        tvi.hParent = parent;
        tvi.hInsertAfter = TVI_LAST;
        tvi.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        tvi.item.pszText = const_cast<LPWSTR>(name.c_str());
        tvi.item.lParam = static_cast<LPARAM>(explorerNodes_.size() - 1);
        tvi.item.cChildren = e.isDir ? 1 : 0;
        SendMessageW(explorer_, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&tvi));
    }
}

void App::refreshExplorer() {
    if (!explorer_) return;
    SendMessageW(explorer_, TVM_DELETEITEM, 0, reinterpret_cast<LPARAM>(TVI_ROOT));
    explorerNodes_.clear();
    std::string root = profile_->session.explorerRoot.empty() ? userWorkspace() : profile_->session.explorerRoot;
    std::error_code ec;
    if (!fs::is_directory(files::fromUtf8(root), ec)) { files::ensureDir(files::fromUtf8(root)); }
    explorerNodes_.push_back({root, true, true});
    std::wstring name = win::widen(files::toUtf8(files::fromUtf8(root).filename()));
    if (name.empty()) name = win::widen(root);
    TVINSERTSTRUCTW tvi = {};
    tvi.hParent = TVI_ROOT;
    tvi.hInsertAfter = TVI_LAST;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    tvi.item.pszText = const_cast<LPWSTR>(name.c_str());
    tvi.item.lParam = 0;
    tvi.item.cChildren = 1;
    HTREEITEM rootItem = reinterpret_cast<HTREEITEM>(SendMessageW(explorer_, TVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&tvi)));
    populateExplorerChildren(rootItem, root);
    SendMessageW(explorer_, TVM_EXPAND, TVE_EXPAND, reinterpret_cast<LPARAM>(rootItem));
}

void App::onExplorerNotify(NMHDR* n) {
    auto nodeOf = [&](HTREEITEM it) -> ExplorerNode* {
        if (!it) return nullptr;
        TVITEMW item = {};
        item.mask = TVIF_PARAM; item.hItem = it;
        if (!SendMessageW(explorer_, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item))) return nullptr;
        size_t idx = static_cast<size_t>(item.lParam);
        return idx < explorerNodes_.size() ? &explorerNodes_[idx] : nullptr;
    };
    auto selected = [&]() { return reinterpret_cast<HTREEITEM>(SendMessageW(explorer_, TVM_GETNEXTITEM, TVGN_CARET, 0)); };
    if (n->code == TVN_ITEMEXPANDINGW) {
        NMTREEVIEWW* nm = reinterpret_cast<NMTREEVIEWW*>(n);
        if (nm->action == TVE_EXPAND) {
            ExplorerNode* node = nodeOf(nm->itemNew.hItem);
            if (node && node->isDir && !node->loaded) {
                node->loaded = true;
                std::string path = node->path;          // populate may reallocate explorerNodes_
                populateExplorerChildren(nm->itemNew.hItem, path);
            }
        }
    } else if (n->code == NM_DBLCLK || (n->code == TVN_KEYDOWN && reinterpret_cast<NMTVKEYDOWN*>(n)->wVKey == VK_RETURN)) {
        ExplorerNode* node = nodeOf(selected());
        if (node && !node->isDir) { std::string p = node->path; openPath(p); }
    } else if (n->code == NM_RCLICK) {
        POINT pt; GetCursorPos(&pt);
        TVHITTESTINFO ht = {};
        ht.pt = pt;
        ScreenToClient(explorer_, &ht.pt);
        HTREEITEM hit = reinterpret_cast<HTREEITEM>(SendMessageW(explorer_, TVM_HITTEST, 0, reinterpret_cast<LPARAM>(&ht)));
        if (hit) SendMessageW(explorer_, TVM_SELECTITEM, TVGN_CARET, reinterpret_cast<LPARAM>(hit));
        ExplorerNode* node = nodeOf(hit ? hit : selected());
        HMENU m = CreatePopupMenu();
        if (node && !node->isDir) AppendMenuW(m, MF_STRING, IDM_EXPL_OPEN, L"Open");
        AppendMenuW(m, MF_STRING, IDM_EXPL_NEWFILE, L"New File Here...");
        AppendMenuW(m, MF_STRING, IDM_EXPL_REVEAL, L"Show in Windows Explorer");
        AppendMenuW(m, MF_STRING, IDM_EXPL_REFRESH, L"Refresh");
        TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd_, nullptr);
        DestroyMenu(m);
    }
}

// ======================================================================= events and commands
void App::onEditorEvent(int ev, LPARAM lp) {
    switch (ev) {
        case win::ED_CHANGED: lastEditTick_ = GetTickCount(); refreshTabs(); updateTitle(); break;
        case win::ED_CARET: updateStatus(); break;
        case win::ED_ZOOM: zoom(static_cast<int>(lp)); break;
        default: break;
    }
}

void App::onTabEvent(int ev, int index) {
    switch (ev) {
        case win::TAB_SELECT: activateTab(index); break;
        case win::TAB_CLOSE: closeTab(index, true); break;
        case win::TAB_NEW: newFile(); break;
        default: break;
    }
}

void App::onDropFiles(HDROP drop) {
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; ++i) {
        wchar_t buf[MAX_PATH * 2] = L"";
        DragQueryFileW(drop, i, buf, static_cast<UINT>(sizeof(buf) / sizeof(buf[0])));
        openPath(win::narrow(buf));
    }
    DragFinish(drop);
}

void App::showShortcuts() {
    win::info(hwnd_, "Keyboard shortcuts",
        "File\n  Ctrl+N  New      Ctrl+O  Open      Ctrl+S  Save      Ctrl+Shift+S  Save As      Ctrl+W  Close tab\n\n"
        "Run\n  F5  Run      Shift+F5  Stop      F7  Check for errors\n\n"
        "Editing\n  Ctrl+Space  Suggestions      Tab / Enter  Accept a suggestion      Esc  Close the list\n"
        "  Ctrl+/  Comment line      Ctrl+D  Duplicate line      Ctrl+Shift+K  Delete line      Alt+Up/Down  Move line\n"
        "  Tab / Shift+Tab  Indent / outdent      Shift+Alt+F  Format (pretty print)      Ctrl+Z / Ctrl+Y  Undo / redo\n\n"
        "Search\n  Ctrl+F  Find      Ctrl+H  Replace      F3 / Shift+F3  Next / previous      Ctrl+G  Go to line\n\n"
        "View\n  Ctrl+=  Zoom in      Ctrl+-  Zoom out      Ctrl+0  Reset zoom (or Ctrl+mouse wheel)\n  Ctrl+B  Explorer      Ctrl+`  Output panel      Ctrl+Tab  Next tab");
}

bool App::onCommand(int id) {
    Tab* t = activeTab();
    auto routeEdit = [&](UINT wmMsg, void (win::EditorView::*fn)()) {
        HWND f = GetFocus();
        if (f == console_ || f == input_ || f == problems_) SendMessageW(f, wmMsg, 0, 0);
        else if (t) (editor_.*fn)();
    };
    if (id >= IDM_RECENT_BASE && id < IDM_RECENT_BASE + 12) {
        size_t i = static_cast<size_t>(id - IDM_RECENT_BASE);
        if (i < profile_->session.recentFiles.size()) { std::string p = profile_->session.recentFiles[i]; openPath(p); }
        return true;
    }
    if (id >= IDM_TEMPLATE_BASE && id < IDM_TEMPLATE_BASE + 40) {
        auto tpls = exts_->templates();
        size_t i = static_cast<size_t>(id - IDM_TEMPLATE_BASE);
        if (i < tpls.size()) newFile(tpls[i].body);
        return true;
    }
    if (id >= IDM_THEME_BASE && id < IDM_THEME_BASE + 60) {
        size_t i = static_cast<size_t>(id - IDM_THEME_BASE);
        if (i < allThemes_.size()) { studio::Settings s = profile_->settings; s.theme = allThemes_[i].name; applySettings(s, true); }
        return true;
    }
    if (id >= IDM_EXAMPLE_BASE && id < IDM_EXAMPLE_BASE + 60) {
        size_t i = static_cast<size_t>(id - IDM_EXAMPLE_BASE);
        if (i < examplePaths_.size()) openExample(examplePaths_[i]);
        return true;
    }
    switch (id) {
        case IDM_FILE_NEW: newFile(); return true;
        case IDM_FILE_OPEN: { std::string p; if (win::openFileDialog(hwnd_, userWorkspace(), p)) openPath(p); return true; }
        case IDM_FILE_SAVE: if (t) saveTab(*t, false); return true;
        case IDM_FILE_SAVEAS: if (t) saveTab(*t, true); return true;
        case IDM_FILE_SAVEALL: saveAll(); return true;
        case IDM_FILE_CLOSE: closeTab(active_, true); return true;
        case IDM_FILE_CLOSEALL: while (!tabs_.empty()) if (!closeTab(0, true)) break; return true;
        case IDM_FILE_EXIT: PostMessageW(hwnd_, WM_CLOSE, 0, 0); return true;
        case IDM_FILE_OPENFOLDER: case IDM_VIEW_CHOOSEFOLDER: {
            std::string p;
            if (win::pickFolder(hwnd_, "Choose the folder to show in the Explorer panel", p)) { profile_->session.explorerRoot = p; refreshExplorer(); showExplorer_ = true; layoutChildren(); }
            return true;
        }
        case IDM_EDIT_UNDO: if (t) editor_.cmdUndo(); return true;
        case IDM_EDIT_REDO: if (t) editor_.cmdRedo(); return true;
        case IDM_EDIT_CUT: routeEdit(WM_CUT, &win::EditorView::cmdCut); return true;
        case IDM_EDIT_COPY: routeEdit(WM_COPY, &win::EditorView::cmdCopy); return true;
        case IDM_EDIT_PASTE: routeEdit(WM_PASTE, &win::EditorView::cmdPaste); return true;
        case IDM_EDIT_SELECTALL: {
            HWND f = GetFocus();
            if (f == console_ || f == input_) SendMessageW(f, EM_SETSEL, 0, -1); else if (t) editor_.cmdSelectAll();
            return true;
        }
        case IDM_EDIT_FIND: case IDM_EDIT_REPLACE:
            if (t) find_.show(id == IDM_EDIT_REPLACE, t->doc->hasSelection() ? t->doc->selectedText() : t->doc->wordAt(t->doc->caret()));
            return true;
        case IDM_EDIT_FINDNEXT: if (t) find_.findNext(true); return true;
        case IDM_EDIT_FINDPREV: if (t) find_.findNext(false); return true;
        case IDM_EDIT_GOTO: {
            if (!t) return true;
            std::string v;
            if (win::promptText(hwnd_, L"Go to line", L"Line number (1 - " + std::to_wstring(t->doc->lineCount()) + L"):", v)) {
                int n = atoi(v.c_str());
                if (n >= 1) editor_.revealLine(n - 1);
            }
            return true;
        }
        case IDM_EDIT_FORMAT: if (t) editor_.cmdFormat(); return true;
        case IDM_EDIT_COMMENT: if (t) editor_.cmdToggleComment(); return true;
        case IDM_EDIT_INDENT: if (t) editor_.cmdIndent(false); return true;
        case IDM_EDIT_OUTDENT: if (t) editor_.cmdIndent(true); return true;
        case IDM_EDIT_DUPLICATE: if (t) editor_.cmdDuplicate(); return true;
        case IDM_EDIT_DELETELINE: if (t) editor_.cmdDeleteLine(); return true;
        case IDM_EDIT_MOVEUP: if (t) editor_.cmdMoveLines(-1); return true;
        case IDM_EDIT_MOVEDOWN: if (t) editor_.cmdMoveLines(1); return true;
        case IDM_EDIT_SUGGEST: if (t) editor_.cmdSuggest(); return true;

        case IDM_VIEW_EXPLORER: showExplorer_ = !showExplorer_; profile_->settings.showExplorer = showExplorer_; layoutChildren(); return true;
        case IDM_VIEW_OUTPUT: showPanel_ = !showPanel_; profile_->settings.showConsole = showPanel_; layoutChildren(); return true;
        case IDM_VIEW_PROBLEMS: showPanelTab(1); return true;
        case IDM_VIEW_TOOLBAR: case IDM_VIEW_STATUSBAR: case IDM_VIEW_LINENUMBERS: case IDM_VIEW_INDENTGUIDES: case IDM_VIEW_WHITESPACE: {
            studio::Settings s = profile_->settings;
            if (id == IDM_VIEW_TOOLBAR) s.showToolbar = !s.showToolbar;
            else if (id == IDM_VIEW_STATUSBAR) s.showStatusBar = !s.showStatusBar;
            else if (id == IDM_VIEW_LINENUMBERS) s.showLineNumbers = !s.showLineNumbers;
            else if (id == IDM_VIEW_INDENTGUIDES) s.showIndentGuides = !s.showIndentGuides;
            else s.showWhitespace = !s.showWhitespace;
            applySettings(s, true);
            return true;
        }
        case IDM_VIEW_ZOOMIN: zoom(1); return true;
        case IDM_VIEW_ZOOMOUT: zoom(-1); return true;
        case IDM_VIEW_ZOOMRESET: zoom(0); return true;

        case IDM_RUN_RUN: runProgram(); return true;
        case IDM_RUN_STOP: stopProgram(); return true;
        case IDM_RUN_CHECK: checkNow(true); showPanelTab(1); return true;
        case IDM_RUN_CLEAR: clearConsole(); return true;

        case IDM_ACCOUNT_SWITCH: doSwitchUser(); return true;
        case IDM_ACCOUNT_CREATE: {
            std::string created;
            if (win::showCreateAccountDialog(hwnd_, *accounts_, &created)) win::info(hwnd_, "Create account", "The account \"" + created + "\" was created. Use Switch User to sign in with it.");
            return true;
        }
        case IDM_ACCOUNT_PASSWORD: win::showChangePasswordDialog(hwnd_, *accounts_); return true;
        case IDM_ACCOUNT_DELETE:
            if (win::showDeleteAccountDialog(hwnd_, *accounts_)) doSwitchUser();
            return true;

        case IDM_TOOLS_SETTINGS: showSettings(); return true;
        case IDM_TOOLS_EXTENSIONS: showExtensions(); return true;
        case IDM_TOOLS_DATAFOLDER: files::ensureDir(profile_->dir()); win::shellOpen(hwnd_, files::toUtf8(profile_->dir())); return true;
        case IDM_TOOLS_THEMESFOLDER: files::ensureDir(profile_->themesDir()); win::shellOpen(hwnd_, files::toUtf8(profile_->themesDir())); return true;

        case IDM_HELP_REFERENCE: case IDM_HELP_CAMBRIDGE: {
            fs::path p = appDir_ / "docs" / (id == IDM_HELP_REFERENCE ? "LANGUAGE_REFERENCE.md" : "CAMBRIDGE_COMPATIBILITY.md");
            std::error_code ec;
            if (fs::exists(p, ec)) openExample(files::toUtf8(p));
            else win::info(hwnd_, "Help", "The documentation files were not found next to the program:\n" + files::toUtf8(p));
            return true;
        }
        case IDM_HELP_SHORTCUTS: showShortcuts(); return true;
        case IDM_HELP_ABOUT:
            win::info(hwnd_, "About PCScript Studio",
                "PCScript Studio 2.0\n\nAn integrated editor and runner for PCScript, a Cambridge-style pseudocode language.\n\n"
                "Programs are saved as plain .pcs text files in your own folder:\n" + userWorkspace());
            return true;
        case IDM_TAB_NEXT: if (!tabs_.empty()) activateTab((active_ + 1) % static_cast<int>(tabs_.size())); return true;
        case IDM_TAB_PREV: if (!tabs_.empty()) activateTab((active_ + static_cast<int>(tabs_.size()) - 1) % static_cast<int>(tabs_.size())); return true;

        case IDM_EXPL_REFRESH: refreshExplorer(); return true;
        case IDM_EXPL_OPEN: case IDM_EXPL_REVEAL: case IDM_EXPL_NEWFILE: {
            HTREEITEM sel = reinterpret_cast<HTREEITEM>(SendMessageW(explorer_, TVM_GETNEXTITEM, TVGN_CARET, 0));
            TVITEMW item = {};
            item.mask = TVIF_PARAM; item.hItem = sel;
            std::string path = profile_->session.explorerRoot.empty() ? userWorkspace() : profile_->session.explorerRoot;
            bool isDir = true;
            if (sel && SendMessageW(explorer_, TVM_GETITEMW, 0, reinterpret_cast<LPARAM>(&item)) && static_cast<size_t>(item.lParam) < explorerNodes_.size()) {
                path = explorerNodes_[static_cast<size_t>(item.lParam)].path; isDir = explorerNodes_[static_cast<size_t>(item.lParam)].isDir;
            }
            if (id == IDM_EXPL_OPEN) { if (!isDir) openPath(path); }
            else if (id == IDM_EXPL_REVEAL) win::revealInExplorer(hwnd_, path);
            else {
                std::string dir = isDir ? path : files::toUtf8(files::fromUtf8(path).parent_path());
                std::string name = "NewProgram.pcs";
                if (win::promptText(hwnd_, L"New file", L"File name:", name) && !name.empty()) {
                    if (files::fromUtf8(name).extension().empty()) name += ".pcs";
                    fs::path full = files::fromUtf8(dir) / files::fromUtf8(name);
                    std::error_code ec;
                    if (fs::exists(full, ec)) win::warn(hwnd_, "New file", "A file with that name already exists.");
                    else { std::string err; if (files::writeTextAtomic(full, "", &err)) { refreshExplorer(); openPath(files::toUtf8(full)); } else win::warn(hwnd_, "New file", err); }
                }
            }
            return true;
        }
        default: break;
    }
    return false;
}
