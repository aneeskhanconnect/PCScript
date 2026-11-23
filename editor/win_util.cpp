#include "win_util.hpp"
#include <commdlg.h>
#include <dwmapi.h>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

namespace win {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

int dpiOf(HWND hwnd) {
    UINT d = hwnd ? GetDpiForWindow(hwnd) : 0;
    if (d == 0) {
        HDC dc = GetDC(nullptr);
        d = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSY));
        ReleaseDC(nullptr, dc);
    }
    return d ? static_cast<int>(d) : 96;
}

HFONT makeFont(const std::string& family, int points, int dpi, bool bold) {
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(points, dpi, 72);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    std::wstring w = widen(family);
    wcsncpy_s(lf.lfFaceName, LF_FACESIZE, w.c_str(), _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

HFONT makeUiFont(int dpi, bool bold) {
    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(9, dpi, 72);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, LF_FACESIZE, L"Segoe UI");
    return CreateFontIndirectW(&lf);
}

std::wstring windowText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring w(static_cast<size_t>(n) + 1, L'\0');
    int got = GetWindowTextW(h, &w[0], n + 1);
    w.resize(static_cast<size_t>(got));
    return w;
}
void setWindowText(HWND h, const std::string& utf8) { SetWindowTextW(h, widen(utf8).c_str()); }
std::string windowTextUtf8(HWND h) { return narrow(windowText(h)); }

bool openFileDialog(HWND owner, const std::string& initialDir, std::string& pathOut) {
    wchar_t file[MAX_PATH * 4] = L"";
    std::wstring dir = widen(initialDir);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PCScript programs (*.pcs)\0*.pcs\0Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrTitle = L"Open";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    pathOut = narrow(file);
    return true;
}

bool saveFileDialog(HWND owner, const std::string& initialDir, const std::string& suggestedName, std::string& pathOut) {
    wchar_t file[MAX_PATH * 4] = L"";
    wcsncpy_s(file, widen(suggestedName).c_str(), _TRUNCATE);
    std::wstring dir = widen(initialDir);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"PCScript programs (*.pcs)\0*.pcs\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(sizeof(file) / sizeof(file[0]));
    ofn.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    ofn.lpstrDefExt = L"pcs";
    ofn.lpstrTitle = L"Save As";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return false;
    pathOut = narrow(file);
    return true;
}

bool pickFolder(HWND owner, const std::string& title, std::string& pathOut) {
    std::wstring t = widen(title);
    BROWSEINFOW bi = {};
    bi.hwndOwner = owner;
    bi.lpszTitle = t.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t buf[MAX_PATH * 2] = L"";
    bool ok = SHGetPathFromIDListW(pidl, buf) != FALSE;
    CoTaskMemFree(pidl);
    if (ok) pathOut = narrow(buf);
    return ok;
}

static void box(HWND owner, const std::string& title, const std::string& text, UINT icon) {
    MessageBoxW(owner, widen(text).c_str(), widen(title).c_str(), MB_OK | icon);
}
void info(HWND owner, const std::string& title, const std::string& text) { box(owner, title, text, MB_ICONINFORMATION); }
void warn(HWND owner, const std::string& title, const std::string& text) { box(owner, title, text, MB_ICONWARNING); }
bool confirm(HWND owner, const std::string& title, const std::string& text) {
    return MessageBoxW(owner, widen(text).c_str(), widen(title).c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES;
}
int askSaveChanges(HWND owner, const std::string& fileName) {
    std::wstring msg = L"Do you want to save the changes to \"" + widen(fileName) + L"\"?";
    return MessageBoxW(owner, msg.c_str(), L"PCScript Studio", MB_YESNOCANCEL | MB_ICONQUESTION);
}

bool clipboardSet(HWND owner, const std::string& utf8) {
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    std::wstring w = widen(utf8);
    // the clipboard expects Windows line endings
    std::wstring crlf;
    crlf.reserve(w.size() + w.size() / 16);
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) crlf += L'\r';
        crlf += w[i];
    }
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (crlf.size() + 1) * sizeof(wchar_t));
    if (!mem) { CloseClipboard(); return false; }
    void* p = GlobalLock(mem);
    memcpy(p, crlf.c_str(), (crlf.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(mem);
    SetClipboardData(CF_UNICODETEXT, mem);
    CloseClipboard();
    return true;
}

std::string clipboardGet(HWND owner) {
    std::string out;
    if (!OpenClipboard(owner)) return out;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p) { out = narrow(p); GlobalUnlock(h); }
    }
    CloseClipboard();
    return out;
}

static std::string knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) out = narrow(p);
    CoTaskMemFree(p);
    return out;
}
std::string knownFolderDocuments() { return knownFolder(FOLDERID_Documents); }
std::string knownFolderAppData() { return knownFolder(FOLDERID_RoamingAppData); }

std::string moduleDirectory() {
    wchar_t buf[MAX_PATH * 2] = L"";
    GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    std::wstring w = buf;
    size_t slash = w.find_last_of(L"\\/");
    return narrow(slash == std::wstring::npos ? w : w.substr(0, slash));
}

void shellOpen(HWND owner, const std::string& pathOrUrl) {
    ShellExecuteW(owner, L"open", widen(pathOrUrl).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void revealInExplorer(HWND owner, const std::string& path) {
    std::wstring args = L"/select,\"" + widen(path) + L"\"";
    ShellExecuteW(owner, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

bool isDarkSystem() {
    DWORD v = 1, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return true;
    return v == 0;
}

void themeControl(HWND h, bool dark) { SetWindowTheme(h, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr); }

void themeTitleBar(HWND top, bool dark) {
    BOOL v = dark ? TRUE : FALSE;
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (19 on early Windows 10 builds); harmless if unsupported
    DwmSetWindowAttribute(top, 20, &v, sizeof(v));
    DwmSetWindowAttribute(top, 19, &v, sizeof(v));
}

} // namespace win
