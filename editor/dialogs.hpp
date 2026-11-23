// Dialog windows of PCScript Studio. They are built in code (no .rc dialog templates) on a small Modal helper.
#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include "accounts.hpp"
#include "document.hpp"
#include "extensions.hpp"
#include "settings.hpp"

namespace win {

// A modal window with a message loop of its own. Child controls are positioned in device-independent pixels (96 dpi).
class Modal {
public:
    Modal(HWND owner, const std::wstring& title, int clientW, int clientH);
    ~Modal();
    Modal(const Modal&) = delete;
    Modal& operator=(const Modal&) = delete;

    HWND hwnd() const { return hwnd_; }
    int D(int dip) const { return MulDiv(dip, dpi_, 96); }
    HFONT font() const { return font_; }

    HWND control(const wchar_t* cls, const std::wstring& text, DWORD style, DWORD ex, int x, int y, int w, int h, int id);
    HWND label(const std::wstring& text, int x, int y, int w, int h);
    HWND edit(const std::wstring& text, int x, int y, int w, int h, int id, DWORD extra = 0);
    HWND button(const std::wstring& text, int x, int y, int w, int h, int id, bool isDefault = false);
    HWND check(const std::wstring& text, int x, int y, int w, int h, int id, bool on);
    HWND combo(int x, int y, int w, int dropH, int id, bool editable);
    HWND spin(int x, int y, int w, int id, int value, int lo, int hi, HWND* updownOut = nullptr);   // edit + up/down; returns the edit
    static void comboAdd(HWND combo, const std::wstring& s) { SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str())); }

    std::function<bool(int id, int code)> onCommand;            // return true when handled
    std::function<LRESULT(NMHDR*)> onNotify;
    int run();                                                  // shows the window and returns the id passed to end()
    void end(int result) { result_ = result; done_ = true; }

private:
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    HWND owner_, hwnd_ = nullptr;
    int dpi_ = 96, result_ = 0;
    bool done_ = false;
    HFONT font_ = nullptr;
};

// ---- accounts ----
enum class LoginResult { SignedIn, Guest, Quit };
LoginResult showLoginDialog(HWND owner, studio::AccountStore& store, const std::string& lastUser);
bool showCreateAccountDialog(HWND owner, studio::AccountStore& store, std::string* createdUser);
bool showChangePasswordDialog(HWND owner, studio::AccountStore& store);
bool showDeleteAccountDialog(HWND owner, studio::AccountStore& store);   // true if the signed-in account was deleted

// ---- settings ----
struct SettingsContext {
    studio::Settings settings;                                   // edited copy
    std::vector<std::string> themeNames;
    std::function<void(const std::string&)> previewTheme;        // live preview while choosing
    std::function<std::string(HWND)> importTheme;                // returns the imported theme's name or ""
    std::function<void()> openThemesFolder;
    std::function<void(const studio::Settings&)> applyNow;       // the Apply button
};
bool showSettingsDialog(HWND owner, SettingsContext& ctx);       // true if OK was pressed (ctx.settings holds the result)

// ---- extensions ----
struct ExtensionsContext {
    studio::ExtensionManager* manager = nullptr;
    std::function<void()> changed;                               // enable/disable/install/uninstall happened
    std::function<void()> openFolder;
};
void showExtensionsDialog(HWND owner, ExtensionsContext& ctx);

// ---- small prompts ----
bool promptText(HWND owner, const std::wstring& title, const std::wstring& label, std::string& value);

// ---- find / replace (modeless) ----
class FindDialog {
public:
    bool create(HWND owner);
    void show(bool replaceMode, const std::string& initialText);
    HWND hwnd() const { return hwnd_; }
    bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void findNext(bool forward);
    std::function<studio::Document*()> getDocument;
    std::function<void()> changed;                               // the document was edited or the selection moved
private:
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void layout(bool replaceMode);
    studio::FindOptions options() const;
    void setStatus(const std::wstring& s);
    HWND hwnd_ = nullptr, owner_ = nullptr, findEdit_ = nullptr, replaceEdit_ = nullptr, caseBox_ = nullptr, wordBox_ = nullptr, status_ = nullptr;
    HWND replaceLabel_ = nullptr, replaceBtn_ = nullptr, replaceAllBtn_ = nullptr, closeBtn_ = nullptr;
    HFONT font_ = nullptr;
    int dpi_ = 96;
};

} // namespace win
