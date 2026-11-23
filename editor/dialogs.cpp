#include "dialogs.hpp"
#include <commctrl.h>
#include <algorithm>
#include <set>
#include "files.hpp"
#include "resource.h"
#include "scan.hpp"
#include "theme.hpp"
#include "win_util.hpp"

namespace win {

static const wchar_t* kModalClass = L"PCSModal";

// ======================================================================= Modal helper
Modal::Modal(HWND owner, const std::wstring& title, int clientW, int clientH) : owner_(owner) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW probe = {};
    if (!GetClassInfoExW(inst, kModalClass, &probe)) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Modal::proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
        wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
        wc.lpszClassName = kModalClass;
        RegisterClassExW(&wc);
    }
    dpi_ = dpiOf(owner);
    font_ = makeUiFont(dpi_);
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    DWORD ex = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT | (owner ? 0 : WS_EX_APPWINDOW);
    RECT r = {0, 0, D(clientW), D(clientH)};
    AdjustWindowRectExForDpi(&r, style, FALSE, ex, static_cast<UINT>(dpi_));
    int w = r.right - r.left, h = r.bottom - r.top;
    RECT area = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (owner && IsWindow(owner)) GetWindowRect(owner, &area);
    int x = area.left + (area.right - area.left - w) / 2, y = area.top + (area.bottom - area.top - h) / 2;
    hwnd_ = CreateWindowExW(ex, kModalClass, title.c_str(), style, x, std::max(0, y), w, h, owner, nullptr, inst, this);
}

Modal::~Modal() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (font_) DeleteObject(font_);
}

LRESULT CALLBACK Modal::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Modal* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<Modal*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<Modal*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (self) {
        switch (msg) {
            case WM_COMMAND: {
                int id = LOWORD(wp), code = HIWORD(wp);
                if (self->onCommand && self->onCommand(id, code)) return 0;
                if (id == IDCANCEL) { self->end(IDCANCEL); return 0; }
                break;
            }
            case WM_NOTIFY:
                if (self->onNotify) return self->onNotify(reinterpret_cast<NMHDR*>(lp));
                break;
            case WM_CLOSE:
                if (self->onCommand && self->onCommand(IDCANCEL, 0)) return 0;
                self->end(IDCANCEL);
                return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND Modal::control(const wchar_t* cls, const std::wstring& text, DWORD style, DWORD ex, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, D(x), D(y), D(w), D(h), hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    if (c) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return c;
}
HWND Modal::label(const std::wstring& text, int x, int y, int w, int h) { return control(L"STATIC", text, SS_LEFT | SS_NOPREFIX, 0, x, y, w, h, -1); }
HWND Modal::edit(const std::wstring& text, int x, int y, int w, int h, int id, DWORD extra) {
    return control(L"EDIT", text, WS_TABSTOP | ES_AUTOHSCROLL | extra, WS_EX_CLIENTEDGE, x, y, w, h, id);
}
HWND Modal::button(const std::wstring& text, int x, int y, int w, int h, int id, bool isDefault) {
    return control(L"BUTTON", text, WS_TABSTOP | (isDefault ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON), 0, x, y, w, h, id);
}
HWND Modal::check(const std::wstring& text, int x, int y, int w, int h, int id, bool on) {
    HWND c = control(L"BUTTON", text, WS_TABSTOP | BS_AUTOCHECKBOX, 0, x, y, w, h, id);
    SendMessageW(c, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    return c;
}
HWND Modal::combo(int x, int y, int w, int dropH, int id, bool editable) {
    return control(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | (editable ? CBS_DROPDOWN | CBS_AUTOHSCROLL : CBS_DROPDOWNLIST), 0, x, y, w, dropH, id);
}
HWND Modal::spin(int x, int y, int w, int id, int value, int lo, int hi, HWND* updownOut) {
    HWND e = edit(std::to_wstring(value), x, y, w, 22, id, ES_NUMBER);
    HWND ud = control(UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ALIGNRIGHT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0, 0, 0, 0, 0, -1);
    SendMessageW(ud, UDM_SETBUDDY, reinterpret_cast<WPARAM>(e), 0);
    SendMessageW(ud, UDM_SETRANGE32, static_cast<WPARAM>(lo), static_cast<LPARAM>(hi));
    SendMessageW(ud, UDM_SETPOS32, 0, static_cast<LPARAM>(value));
    if (updownOut) *updownOut = ud;
    return e;
}

int Modal::run() {
    if (!hwnd_) return IDCANCEL;
    if (owner_) EnableWindow(owner_, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    MSG m;
    while (!done_) {
        BOOL r = GetMessageW(&m, nullptr, 0, 0);
        if (r <= 0) { if (r == 0) PostQuitMessage(static_cast<int>(m.wParam)); break; }
        if (!IsDialogMessageW(hwnd_, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    if (owner_) EnableWindow(owner_, TRUE);
    ShowWindow(hwnd_, SW_HIDE);
    if (owner_) SetForegroundWindow(owner_);
    return result_;
}

static void waitCursor() { SetCursor(LoadCursorW(nullptr, IDC_WAIT)); }

// ======================================================================= accounts
static void fillUsers(HWND cb, studio::AccountStore& store, const std::string& select) {
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    int sel = 0, i = 0;
    for (const std::string& u : store.usernames()) {
        Modal::comboAdd(cb, widen(u));
        if (studio::toLower(u) == studio::toLower(select)) sel = i;
        ++i;
    }
    SendMessageW(cb, CB_SETCURSEL, static_cast<WPARAM>(i ? sel : -1), 0);
}

bool showCreateAccountDialog(HWND owner, studio::AccountStore& store, std::string* createdUser) {
    Modal m(owner, L"Create account", 380, 300);
    m.label(L"User name (3-24 letters, digits, _ - .)", 20, 14, 340, 18);
    HWND user = m.edit(L"", 20, 34, 340, 24, 101);
    m.label(L"Display name (optional)", 20, 66, 340, 18);
    HWND disp = m.edit(L"", 20, 86, 340, 24, 102);
    m.label(L"Password (at least 6 characters)", 20, 118, 340, 18);
    HWND pw = m.edit(L"", 20, 138, 340, 24, 103, ES_PASSWORD);
    m.label(L"Repeat password", 20, 170, 340, 18);
    HWND pw2 = m.edit(L"", 20, 190, 340, 24, 104, ES_PASSWORD);
    HWND err = m.label(L"", 20, 222, 340, 34);
    m.button(L"Create account", 150, 260, 120, 28, IDOK, true);
    m.button(L"Cancel", 280, 260, 80, 28, IDCANCEL);
    bool created = false;
    m.onCommand = [&](int id, int) {
        if (id != IDOK) return false;
        std::string u = windowTextUtf8(user), d = windowTextUtf8(disp), p = windowTextUtf8(pw), p2 = windowTextUtf8(pw2), e;
        if (p != p2) { setWindowText(err, "The two passwords are not the same."); return true; }
        waitCursor();
        if (store.create(u, p, d, &e)) { created = true; if (createdUser) *createdUser = u; m.end(IDOK); }
        else setWindowText(err, e);
        return true;
    };
    SetFocus(user);
    m.run();
    return created;
}

bool showChangePasswordDialog(HWND owner, studio::AccountStore& store) {
    if (store.isGuest() || !store.signedIn()) { info(owner, "Change password", "Guests do not have a password."); return false; }
    Modal m(owner, L"Change password", 380, 240);
    m.label(L"Current password", 20, 14, 340, 18);
    HWND cur = m.edit(L"", 20, 34, 340, 24, 101, ES_PASSWORD);
    m.label(L"New password (at least 6 characters)", 20, 66, 340, 18);
    HWND np = m.edit(L"", 20, 86, 340, 24, 102, ES_PASSWORD);
    m.label(L"Repeat new password", 20, 118, 340, 18);
    HWND np2 = m.edit(L"", 20, 138, 340, 24, 103, ES_PASSWORD);
    HWND err = m.label(L"", 20, 168, 340, 30);
    m.button(L"Change password", 140, 204, 130, 28, IDOK, true);
    m.button(L"Cancel", 280, 204, 80, 28, IDCANCEL);
    bool ok = false;
    m.onCommand = [&](int id, int) {
        if (id != IDOK) return false;
        std::string a = windowTextUtf8(cur), b = windowTextUtf8(np), c = windowTextUtf8(np2), e;
        if (b != c) { setWindowText(err, "The new passwords are not the same."); return true; }
        waitCursor();
        if (store.changePassword(store.currentUser(), a, b, &e)) { ok = true; m.end(IDOK); }
        else setWindowText(err, e);
        return true;
    };
    SetFocus(cur);
    m.run();
    if (ok) info(owner, "Change password", "Your password was changed.");
    return ok;
}

bool showDeleteAccountDialog(HWND owner, studio::AccountStore& store) {
    if (store.isGuest() || !store.signedIn()) { info(owner, "Delete account", "Guests cannot be deleted."); return false; }
    Modal m(owner, L"Delete account", 400, 220);
    m.label(L"This removes the account \"" + widen(store.currentUser()) + L"\" and its settings from this computer. "
            L"The programs saved in your folder are NOT deleted.", 20, 14, 360, 52);
    m.label(L"Type your password to confirm", 20, 76, 360, 18);
    HWND pw = m.edit(L"", 20, 96, 360, 24, 101, ES_PASSWORD);
    HWND err = m.label(L"", 20, 128, 360, 30);
    m.button(L"Delete account", 170, 176, 120, 28, IDOK, true);
    m.button(L"Cancel", 300, 176, 80, 28, IDCANCEL);
    bool deleted = false;
    m.onCommand = [&](int id, int) {
        if (id != IDOK) return false;
        std::string e;
        waitCursor();
        if (store.remove(store.currentUser(), windowTextUtf8(pw), &e)) { deleted = true; m.end(IDOK); }
        else setWindowText(err, e);
        return true;
    };
    SetFocus(pw);
    m.run();
    return deleted;
}

LoginResult showLoginDialog(HWND owner, studio::AccountStore& store, const std::string& lastUser) {
    Modal m(owner, L"PCScript Studio - Sign in", 380, 290);
    m.label(L"Choose who is using PCScript Studio.", 20, 14, 340, 20);
    m.label(L"User name", 20, 46, 340, 18);
    HWND cb = m.combo(20, 66, 340, 220, 101, false);
    m.label(L"Password", 20, 102, 340, 18);
    HWND pw = m.edit(L"", 20, 122, 340, 24, 102, ES_PASSWORD);
    HWND err = m.label(L"", 20, 152, 340, 34);
    m.button(L"Sign in", 20, 196, 100, 30, IDOK, true);
    m.button(L"Create account...", 130, 196, 130, 30, 103);
    m.button(L"Quit", 270, 196, 90, 30, IDCANCEL);
    m.button(L"Continue as Guest (no password, nothing is remembered between users)", 20, 238, 340, 30, 104);
    fillUsers(cb, store, lastUser);
    if (store.usernames().empty()) setWindowText(err, "There are no accounts yet. Create one, or continue as Guest.");
    LoginResult out = LoginResult::Quit;
    m.onCommand = [&](int id, int) {
        if (id == IDOK) {
            int sel = static_cast<int>(SendMessageW(cb, CB_GETCURSEL, 0, 0));
            if (sel < 0) { setWindowText(err, "Choose a user name, or create an account first."); return true; }
            wchar_t name[128] = L"";
            SendMessageW(cb, CB_GETLBTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(name));
            std::string e;
            waitCursor();
            if (store.login(narrow(name), windowTextUtf8(pw), &e)) { out = LoginResult::SignedIn; m.end(IDOK); }
            else { setWindowText(err, e); SetWindowTextW(pw, L""); SetFocus(pw); }
            return true;
        }
        if (id == 103) {
            std::string created;
            if (showCreateAccountDialog(m.hwnd(), store, &created)) {
                fillUsers(cb, store, created);
                setWindowText(err, "Account created. Enter your password to sign in.");
                SetFocus(pw);
            }
            return true;
        }
        if (id == 104) { store.loginGuest(); out = LoginResult::Guest; m.end(IDOK); return true; }
        if (id == IDCANCEL) { out = LoginResult::Quit; m.end(IDCANCEL); return true; }
        return false;
    };
    SetFocus(store.usernames().empty() ? GetDlgItem(m.hwnd(), 104) : pw);
    m.run();
    return out;
}

// ======================================================================= prompt
bool promptText(HWND owner, const std::wstring& title, const std::wstring& label, std::string& value) {
    Modal m(owner, title, 340, 120);
    m.label(label, 16, 14, 308, 18);
    HWND e = m.edit(widen(value), 16, 36, 308, 24, 101);
    m.button(L"OK", 154, 76, 80, 28, IDOK, true);
    m.button(L"Cancel", 244, 76, 80, 28, IDCANCEL);
    bool ok = false;
    m.onCommand = [&](int id, int) {
        if (id != IDOK) return false;
        value = windowTextUtf8(e);
        ok = true;
        m.end(IDOK);
        return true;
    };
    SetFocus(e);
    SendMessageW(e, EM_SETSEL, 0, -1);
    m.run();
    return ok;
}

// ======================================================================= settings
namespace {

struct Bind {
    enum Kind { Check, Spin, ComboList, ComboEdit } kind;
    HWND h;
    bool* b = nullptr;
    int* n = nullptr;
    std::string* s = nullptr;
    std::vector<std::wstring> items;
};

int CALLBACK collectFonts(const LOGFONTW* lf, const TEXTMETRICW*, DWORD, LPARAM param) {
    auto* set = reinterpret_cast<std::set<std::wstring>*>(param);
    if ((lf->lfPitchAndFamily & FIXED_PITCH) && lf->lfFaceName[0] != L'@') set->insert(lf->lfFaceName);
    return 1;
}

std::vector<std::wstring> monospaceFonts() {
    std::set<std::wstring> set;
    HDC dc = GetDC(nullptr);
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(dc, &lf, collectFonts, reinterpret_cast<LPARAM>(&set), 0);
    ReleaseDC(nullptr, dc);
    for (const wchar_t* f : {L"Consolas", L"Cascadia Mono", L"Courier New", L"Lucida Console"}) set.insert(f);
    return std::vector<std::wstring>(set.begin(), set.end());
}

} // namespace

bool showSettingsDialog(HWND owner, SettingsContext& ctx) {
    const std::string originalTheme = ctx.settings.theme;
    Modal m(owner, L"Settings", 580, 500);
    HWND tab = m.control(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, 0, 10, 10, 560, 400, 150);
    const wchar_t* names[] = {L"Editor", L"Typing", L"Files", L"Run", L"Theme"};
    for (int i = 0; i < 5; ++i) {
        TCITEMW it = {};
        it.mask = TCIF_TEXT;
        it.pszText = const_cast<LPWSTR>(names[i]);
        SendMessageW(tab, TCM_INSERTITEMW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&it));
    }
    std::vector<std::vector<HWND>> pages(5);
    std::vector<Bind> binds;
    int nextId = 200;
    studio::Settings& S = ctx.settings;

    auto addCheck = [&](int page, const wchar_t* text, int x, int y, bool* field) {
        HWND h = m.check(text, x, y, 250, 20, nextId++, *field);
        pages[static_cast<size_t>(page)].push_back(h);
        Bind b{Bind::Check, h}; b.b = field; binds.push_back(b);
    };
    auto addSpin = [&](int page, const wchar_t* label, int x, int y, int* field, int lo, int hi) {
        HWND l = m.label(label, x, y + 3, 210, 18);
        HWND ud = nullptr;
        HWND e = m.spin(x + 220, y, 70, nextId++, *field, lo, hi, &ud);
        for (HWND h : {l, e, ud}) pages[static_cast<size_t>(page)].push_back(h);
        Bind b{Bind::Spin, e}; b.n = field; binds.push_back(b);
    };
    auto addCombo = [&](int page, const wchar_t* label, int x, int y, std::string* field, std::vector<std::wstring> items, bool editable, int w) {
        HWND l = m.label(label, x, y + 3, 210, 18);
        HWND c = m.combo(x + 220, y, w, 220, nextId++, editable);
        for (const std::wstring& s : items) Modal::comboAdd(c, s);
        pages[static_cast<size_t>(page)].push_back(l);
        pages[static_cast<size_t>(page)].push_back(c);
        Bind b{editable ? Bind::ComboEdit : Bind::ComboList, c}; b.s = field; b.items = std::move(items); binds.push_back(b);
    };

    std::vector<std::wstring> fonts = monospaceFonts();
    // ---- Editor ----
    addCombo(0, L"Font", 24, 50, &S.fontFamily, fonts, true, 220);
    addSpin(0, L"Font size (points)", 24, 82, &S.fontSize, 8, 48);
    addSpin(0, L"Line spacing (% of font height)", 24, 114, &S.lineSpacing, 100, 250);
    addSpin(0, L"Tab size (columns)", 24, 146, &S.tabSize, 1, 8);
    addCombo(0, L"Caret style", 24, 178, &S.caretStyle, {L"line", L"block", L"underline"}, false, 120);
    addCheck(0, L"Tab key inserts spaces", 24, 214, &S.insertSpaces);
    addCheck(0, L"Show line numbers", 24, 238, &S.showLineNumbers);
    addCheck(0, L"Highlight the current line", 24, 262, &S.highlightCurrentLine);
    addCheck(0, L"Show indentation guides", 300, 214, &S.showIndentGuides);
    addCheck(0, L"Show spaces and tabs", 300, 238, &S.showWhitespace);
    addCheck(0, L"Highlight matching brackets", 300, 262, &S.matchBrackets);
    // ---- Typing ----
    addCheck(1, L"Auto-indent new lines", 24, 50, &S.autoIndent);
    addCheck(1, L"Close brackets and quotes automatically", 24, 76, &S.autoCloseBrackets);
    addCheck(1, L"Add the END keyword after IF / FOR / WHILE ... (Enter)", 24, 102, &S.autoCloseBlocks);
    addCheck(1, L"Capitalise keywords as you type (endif -> ENDIF)", 24, 128, &S.autoCapitalizeKeywords);
    addCheck(1, L"Show the suggestion drop-down (autocomplete)", 24, 154, &S.autoComplete);
    addSpin(1, L"Open the drop-down after N characters", 24, 186, &S.autoCompleteMinChars, 1, 5);
    addCheck(1, L"Check for errors while typing (red squiggles)", 24, 222, &S.checkWhileTyping);
    // ---- Files ----
    addCheck(2, L"Format the document when saving", 24, 50, &S.formatOnSave);
    addCheck(2, L"Remove trailing spaces when saving", 24, 76, &S.trimTrailingWhitespace);
    addCheck(2, L"End files with a blank line", 24, 102, &S.insertFinalNewline);
    addCheck(2, L"Reopen my files next time", 24, 128, &S.restoreSession);
    addSpin(2, L"Auto-save every N seconds (0 = off)", 24, 164, &S.autoSaveSeconds, 0, 600);
    // ---- Run ----
    addCheck(3, L"Save the file before running", 24, 50, &S.saveBeforeRun);
    addCheck(3, L"Clear the output before each run", 24, 76, &S.clearConsoleOnRun);
    addSpin(3, L"Stop programs after N seconds (0 = never)", 24, 112, &S.timeoutSeconds, 0, 3600);
    addSpin(3, L"Maximum nested calls (recursion depth)", 24, 144, &S.maxCallDepth, 100, 20000);
    addCombo(3, L"Output font", 24, 180, &S.consoleFontFamily, fonts, true, 220);
    addSpin(3, L"Output font size (points)", 24, 212, &S.consoleFontSize, 8, 40);

    // ---- Theme ----
    HWND tl = m.label(L"Colour theme (changes are previewed immediately)", 24, 50, 400, 18);
    HWND list = m.control(L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY, WS_EX_CLIENTEDGE, 24, 72, 300, 250, 190);
    for (const std::string& n : ctx.themeNames) SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(n).c_str()));
    auto selectTheme = [&](const std::string& name) {
        int idx = static_cast<int>(SendMessageW(list, LB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(widen(name).c_str())));
        SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(idx), 0);
    };
    selectTheme(S.theme);
    HWND importBtn = m.button(L"Import theme file...", 344, 72, 190, 28, 191);
    HWND folderBtn = m.button(L"Open themes folder", 344, 106, 190, 28, 192);
    HWND hint = m.label(L"A theme is a small .json file. Extensions can add themes too (Tools > Extensions).", 344, 148, 200, 70);
    for (HWND h : {tl, list, importBtn, folderBtn, hint}) pages[4].push_back(h);

    // ---- buttons ----
    m.button(L"Reset to defaults", 10, 460, 140, 30, 180);
    m.button(L"OK", 330, 460, 80, 30, IDOK, true);
    m.button(L"Cancel", 420, 460, 80, 30, IDCANCEL);
    m.button(L"Apply", 510, 460, 60, 30, 181);

    auto showPage = [&](int sel) {
        for (size_t p = 0; p < pages.size(); ++p)
            for (HWND h : pages[p]) ShowWindow(h, static_cast<int>(p) == sel ? SW_SHOW : SW_HIDE);
    };
    auto load = [&]() {
        for (Bind& b : binds) {
            switch (b.kind) {
                case Bind::Check: SendMessageW(b.h, BM_SETCHECK, *b.b ? BST_CHECKED : BST_UNCHECKED, 0); break;
                case Bind::Spin: SetWindowTextW(b.h, std::to_wstring(*b.n).c_str()); break;
                case Bind::ComboEdit: SetWindowTextW(b.h, widen(*b.s).c_str()); break;
                case Bind::ComboList: {
                    std::wstring cur = widen(*b.s);
                    for (size_t i = 0; i < b.items.size(); ++i) if (b.items[i] == cur) SendMessageW(b.h, CB_SETCURSEL, i, 0);
                    break;
                }
            }
        }
        selectTheme(S.theme);
    };
    auto collect = [&]() {
        for (Bind& b : binds) {
            switch (b.kind) {
                case Bind::Check: *b.b = SendMessageW(b.h, BM_GETCHECK, 0, 0) == BST_CHECKED; break;
                case Bind::Spin: *b.n = _wtoi(windowText(b.h).c_str()); break;
                case Bind::ComboEdit: { std::string t = windowTextUtf8(b.h); if (!t.empty()) *b.s = t; break; }
                case Bind::ComboList: {
                    int sel = static_cast<int>(SendMessageW(b.h, CB_GETCURSEL, 0, 0));
                    if (sel >= 0 && sel < static_cast<int>(b.items.size())) *b.s = narrow(b.items[static_cast<size_t>(sel)]);
                    break;
                }
            }
        }
        S.clamp();
    };
    load();
    showPage(0);

    bool ok = false;
    m.onNotify = [&](NMHDR* n) -> LRESULT {
        if (n->hwndFrom == tab && n->code == TCN_SELCHANGE) showPage(static_cast<int>(SendMessageW(tab, TCM_GETCURSEL, 0, 0)));
        return 0;
    };
    m.onCommand = [&](int id, int code) {
        if (id == 190 && code == LBN_SELCHANGE) {
            int sel = static_cast<int>(SendMessageW(list, LB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(ctx.themeNames.size())) {
                S.theme = ctx.themeNames[static_cast<size_t>(sel)];
                if (ctx.previewTheme) ctx.previewTheme(S.theme);
            }
            return true;
        }
        if (id == 191) {
            std::string name = ctx.importTheme ? ctx.importTheme(m.hwnd()) : std::string();
            if (!name.empty()) {
                bool have = std::find(ctx.themeNames.begin(), ctx.themeNames.end(), name) != ctx.themeNames.end();
                if (!have) { ctx.themeNames.push_back(name); SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(widen(name).c_str())); }
                S.theme = name;
                selectTheme(name);
                if (ctx.previewTheme) ctx.previewTheme(name);
            }
            return true;
        }
        if (id == 192) { if (ctx.openThemesFolder) ctx.openThemesFolder(); return true; }
        if (id == 180) { std::string keep = S.theme; S = studio::Settings{}; S.theme = keep; load(); return true; }
        if (id == 181) { collect(); if (ctx.applyNow) ctx.applyNow(S); return true; }
        if (id == IDOK) { collect(); ok = true; m.end(IDOK); return true; }
        if (id == IDCANCEL) { if (ctx.previewTheme && S.theme != originalTheme) ctx.previewTheme(originalTheme); m.end(IDCANCEL); return true; }
        return false;
    };
    m.run();
    if (!ok) S.theme = originalTheme;
    return ok;
}

// ======================================================================= extensions
void showExtensionsDialog(HWND owner, ExtensionsContext& ctx) {
    if (!ctx.manager) return;
    studio::ExtensionManager& mgr = *ctx.manager;
    Modal m(owner, L"Extensions", 660, 470);
    m.label(L"Extensions add themes, code snippets and program templates. They contain data only and cannot run code.", 16, 12, 628, 20);
    HWND lv = m.control(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, WS_EX_CLIENTEDGE, 16, 40, 628, 200, 101);
    SendMessageW(lv, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
    struct Col { const wchar_t* t; int w; } cols[] = {{L"Name", 250}, {L"Version", 80}, {L"Contains", 190}, {L"Source", 90}};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW c = {};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = const_cast<LPWSTR>(cols[i].t);
        c.cx = m.D(cols[i].w);
        SendMessageW(lv, LVM_INSERTCOLUMNW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(&c));
    }
    HWND details = m.control(L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_VSCROLL, WS_EX_CLIENTEDGE, 16, 250, 628, 160, 102);
    m.button(L"Install from folder...", 16, 424, 160, 30, 110);
    m.button(L"Uninstall", 186, 424, 100, 30, 111);
    m.button(L"Open extensions folder", 296, 424, 170, 30, 112);
    m.button(L"Close", 564, 424, 80, 30, IDCANCEL, true);

    std::vector<std::string> ids;
    bool populating = false;
    auto describe = [&](int row) {
        if (row < 0 || row >= static_cast<int>(mgr.all().size())) { SetWindowTextW(details, L""); return; }
        const studio::Extension& e = mgr.all()[static_cast<size_t>(row)];
        std::string t = e.name + "  (" + e.version + ")\r\n";
        if (!e.author.empty()) t += "Author: " + e.author + "\r\n";
        t += "Id: " + e.id + "\r\n\r\n" + e.description + "\r\n\r\n";
        t += "Themes: " + std::to_string(e.themes.size()) + "    Snippets: " + std::to_string(e.snippets.size()) + "    Templates: " + std::to_string(e.templates.size()) + "\r\n";
        if (!e.problems.empty()) { t += "\r\nProblems:\r\n"; for (const std::string& p : e.problems) t += "  - " + p + "\r\n"; }
        setWindowText(details, t);
    };
    auto populate = [&]() {
        populating = true;
        SendMessageW(lv, LVM_DELETEALLITEMS, 0, 0);
        int row = 0;
        for (const studio::Extension& e : mgr.all()) {
            std::wstring name = widen(e.name);
            LVITEMW it = {};
            it.mask = LVIF_TEXT;
            it.iItem = row;
            it.pszText = const_cast<LPWSTR>(name.c_str());
            SendMessageW(lv, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it));
            std::wstring ver = widen(e.version);
            std::wstring what;
            if (!e.themes.empty()) what += std::to_wstring(e.themes.size()) + L" theme(s) ";
            if (!e.snippets.empty()) what += std::to_wstring(e.snippets.size()) + L" snippet(s) ";
            if (!e.templates.empty()) what += std::to_wstring(e.templates.size()) + L" template(s)";
            if (!e.problems.empty() && what.empty()) what = L"(has problems)";
            std::wstring src = e.builtin ? L"built-in" : L"installed";
            std::wstring* texts[] = {&ver, &what, &src};
            for (int c = 0; c < 3; ++c) {
                LVITEMW sub = {};
                sub.iSubItem = c + 1;
                sub.pszText = const_cast<LPWSTR>(texts[c]->c_str());
                SendMessageW(lv, LVM_SETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&sub));
            }
            ListView_SetCheckState(lv, row, e.enabled ? TRUE : FALSE);
            ++row;
        }
        populating = false;
        describe(-1);
    };
    populate();

    m.onNotify = [&](NMHDR* n) -> LRESULT {
        if (n->hwndFrom == lv && n->code == LVN_ITEMCHANGED && !populating) {
            NMLISTVIEW* nm = reinterpret_cast<NMLISTVIEW*>(n);
            if (nm->iItem >= 0 && (nm->uChanged & LVIF_STATE)) {
                if ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK) {
                    bool on = ListView_GetCheckState(lv, nm->iItem) != 0;
                    const studio::Extension& e = mgr.all()[static_cast<size_t>(nm->iItem)];
                    if (e.id.rfind("invalid:", 0) == 0) { populate(); return 0; }
                    mgr.setEnabled(e.id, on);
                    if (ctx.changed) ctx.changed();
                }
                if (nm->uNewState & LVIS_SELECTED) describe(nm->iItem);
            }
        }
        return 0;
    };
    m.onCommand = [&](int id, int) {
        if (id == 110) {
            std::string folder;
            if (pickFolder(m.hwnd(), "Choose the extension folder (it must contain extension.json)", folder)) {
                std::string err;
                if (mgr.install(studio::files::fromUtf8(folder), &err)) { populate(); if (ctx.changed) ctx.changed(); }
                else warn(m.hwnd(), "Install extension", err);
            }
            return true;
        }
        if (id == 111) {
            int sel = static_cast<int>(SendMessageW(lv, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED));
            if (sel < 0) { info(m.hwnd(), "Uninstall", "Select an extension first."); return true; }
            const studio::Extension e = mgr.all()[static_cast<size_t>(sel)];
            if (e.builtin) { info(m.hwnd(), "Uninstall", "Built-in extensions cannot be uninstalled. Untick it to turn it off."); return true; }
            if (!confirm(m.hwnd(), "Uninstall", "Remove the extension \"" + e.name + "\"?")) return true;
            std::string err;
            if (mgr.uninstall(e.id, &err)) { populate(); if (ctx.changed) ctx.changed(); } else warn(m.hwnd(), "Uninstall", err);
            return true;
        }
        if (id == 112) { if (ctx.openFolder) ctx.openFolder(); return true; }
        return false;
    };
    m.run();
}

// ======================================================================= find / replace
static const wchar_t* kFindClass = L"PCSFindDialog";
enum { ID_FIND_EDIT = 1, ID_REPL_EDIT, ID_CASE, ID_WORD, ID_NEXT, ID_PREV, ID_REPLACE, ID_REPLACEALL, ID_CLOSE };

bool FindDialog::create(HWND owner) {
    owner_ = owner;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW probe = {};
    if (!GetClassInfoExW(inst, kFindClass, &probe)) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &FindDialog::proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
        wc.lpszClassName = kFindClass;
        RegisterClassExW(&wc);
    }
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kFindClass, L"Find and Replace", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                            100, 100, 440, 200, owner, nullptr, inst, this);
    return hwnd_ != nullptr;
}

LRESULT CALLBACK FindDialog::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    FindDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        self = static_cast<FindDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = h;
    } else {
        self = reinterpret_cast<FindDialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->handle(msg, wp, lp);
}

void FindDialog::setStatus(const std::wstring& s) { if (status_) SetWindowTextW(status_, s.c_str()); }

studio::FindOptions FindDialog::options() const {
    studio::FindOptions o;
    o.matchCase = SendMessageW(caseBox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    o.wholeWord = SendMessageW(wordBox_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return o;
}

void FindDialog::layout(bool replaceMode) {
    auto D = [&](int v) { return MulDiv(v, dpi_, 96); };
    ShowWindow(replaceLabel_, replaceMode ? SW_SHOW : SW_HIDE);
    ShowWindow(replaceEdit_, replaceMode ? SW_SHOW : SW_HIDE);
    ShowWindow(replaceBtn_, replaceMode ? SW_SHOW : SW_HIDE);
    ShowWindow(replaceAllBtn_, replaceMode ? SW_SHOW : SW_HIDE);
    int y = replaceMode ? 70 : 38;
    SetWindowPos(caseBox_, nullptr, D(14), D(y), D(150), D(20), SWP_NOZORDER);
    SetWindowPos(wordBox_, nullptr, D(170), D(y), D(150), D(20), SWP_NOZORDER);
    SetWindowPos(status_, nullptr, D(14), D(y + 26), D(300), D(18), SWP_NOZORDER);
    int ch = y + 56;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT r = {0, 0, D(440), D(ch)};
    AdjustWindowRectExForDpi(&r, style, FALSE, WS_EX_TOOLWINDOW, static_cast<UINT>(dpi_));
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

LRESULT FindDialog::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            dpi_ = dpiOf(owner_);
            font_ = makeUiFont(dpi_);
            auto D = [&](int v) { return MulDiv(v, dpi_, 96); };
            HINSTANCE inst = GetModuleHandleW(nullptr);
            auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, DWORD ex, int x, int y, int w, int h, int id) {
                HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, D(x), D(y), D(w), D(h), hwnd_,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
                SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
                return c;
            };
            mk(L"STATIC", L"Find", SS_LEFT, 0, 14, 12, 56, 18, -1);
            findEdit_ = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 76, 8, 220, 24, ID_FIND_EDIT);
            replaceLabel_ = mk(L"STATIC", L"Replace", SS_LEFT, 0, 14, 44, 56, 18, -1);
            replaceEdit_ = mk(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 76, 40, 220, 24, ID_REPL_EDIT);
            caseBox_ = mk(L"BUTTON", L"Match case", WS_TABSTOP | BS_AUTOCHECKBOX, 0, 14, 70, 150, 20, ID_CASE);
            wordBox_ = mk(L"BUTTON", L"Whole word", WS_TABSTOP | BS_AUTOCHECKBOX, 0, 170, 70, 150, 20, ID_WORD);
            status_ = mk(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, 0, 14, 96, 300, 18, -1);
            mk(L"BUTTON", L"Find next", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 306, 8, 118, 26, ID_NEXT);
            mk(L"BUTTON", L"Find previous", WS_TABSTOP | BS_PUSHBUTTON, 0, 306, 38, 118, 26, ID_PREV);
            replaceBtn_ = mk(L"BUTTON", L"Replace", WS_TABSTOP | BS_PUSHBUTTON, 0, 306, 68, 118, 26, ID_REPLACE);
            replaceAllBtn_ = mk(L"BUTTON", L"Replace all", WS_TABSTOP | BS_PUSHBUTTON, 0, 306, 98, 118, 26, ID_REPLACEALL);
            closeBtn_ = mk(L"BUTTON", L"Close", WS_TABSTOP | BS_PUSHBUTTON, 0, 306, 128, 118, 26, ID_CLOSE);
            return 0;
        }
        case WM_COMMAND: {
            int id = LOWORD(wp);
            studio::Document* doc = getDocument ? getDocument() : nullptr;
            switch (id) {
                case ID_NEXT: findNext(true); return 0;
                case ID_PREV: findNext(false); return 0;
                case ID_REPLACE:
                    if (doc) {
                        doc->replaceCurrent(windowTextUtf8(findEdit_), windowTextUtf8(replaceEdit_), options());
                        if (changed) changed();
                    }
                    return 0;
                case ID_REPLACEALL:
                    if (doc) {
                        int n = doc->replaceAll(windowTextUtf8(findEdit_), windowTextUtf8(replaceEdit_), options());
                        setStatus(n ? std::to_wstring(n) + L" replaced" : L"No matches");
                        if (changed) changed();
                    }
                    return 0;
                case ID_CLOSE: case IDCANCEL: ShowWindow(hwnd_, SW_HIDE); if (owner_) SetFocus(owner_); return 0;
                default: break;
            }
            break;
        }
        case WM_CLOSE: ShowWindow(hwnd_, SW_HIDE); return 0;
        case WM_DESTROY: if (font_) { DeleteObject(font_); font_ = nullptr; } break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void FindDialog::findNext(bool forward) {
    studio::Document* doc = getDocument ? getDocument() : nullptr;
    std::string needle = windowTextUtf8(findEdit_);
    if (!doc || needle.empty()) { setStatus(L""); return; }
    if (doc->findNext(needle, options(), forward, true)) { setStatus(L""); if (changed) changed(); }
    else setStatus(L"No matches");
}

void FindDialog::show(bool replaceMode, const std::string& initialText) {
    if (!hwnd_) return;
    layout(replaceMode);
    if (!initialText.empty() && initialText.find('\n') == std::string::npos) SetWindowTextW(findEdit_, widen(initialText).c_str());
    RECT o = {0, 0, 0, 0}, me = {0, 0, 0, 0};
    if (owner_) GetWindowRect(owner_, &o);
    GetWindowRect(hwnd_, &me);
    if (!IsWindowVisible(hwnd_)) SetWindowPos(hwnd_, nullptr, o.right - (me.right - me.left) - 30, o.top + 110, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwnd_, SW_SHOW);
    SetFocus(findEdit_);
    SendMessageW(findEdit_, EM_SETSEL, 0, -1);
    setStatus(L"");
}

} // namespace win
