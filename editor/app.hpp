// PCScript Studio main application: windows, tabs, menus, files, running programs.
#pragma once
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "accounts.hpp"
#include "dialogs.hpp"
#include "document.hpp"
#include "editor_view.hpp"
#include "extensions.hpp"
#include "profile.hpp"
#include "runner.hpp"
#include "theme.hpp"
#include "widget.hpp"
#include "win_util.hpp"

struct AppOptions {
    std::string dataDir;                 // --data-dir: use this instead of %APPDATA%\PCScript Studio
    std::string docsDir;                 // --docs-dir: use this instead of Documents\PCScript Studio
    bool selfTest = false;               // --selftest: run built-in checks without showing windows, then exit
    std::string selfTestLog;             // --selftest-log <file>
    std::vector<std::string> files;      // files to open (command line / double-click)
};

class App {
public:
    explicit App(AppOptions opt);
    ~App();
    int run(int nCmdShow);

    // used by the window procedure thunk
    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp);

private:
    friend struct SelfTest;

    struct Tab {
        std::unique_ptr<studio::Document> doc;
        std::string path;                // empty = never saved
        std::string title;
        std::string recoveryId;
        int topLine = 0;
        std::vector<pcs::Diagnostic> diags;
        unsigned checkedVersion = ~0u;
        unsigned recoveredVersion = ~0u;
        unsigned autosavedVersion = ~0u;
    };

    struct ExplorerNode { std::string path; bool isDir = false; bool loaded = false; };

    struct WelcomeLink { RECT rc; int action; std::string path; };

    // ---- startup / session (app_core.cpp) ----
    bool signIn();
    void startSession();                                   // loads the signed-in user's profile, extensions and themes
    void adoptSession();                                   // pushes the loaded profile into the windows and reopens the tabs
    void autoSaveTick();
    std::wstring windowTitleText() const;
    bool endSession(bool askToSave);                       // saves/closes everything; false if the user cancelled
    void saveSessionSnapshot();
    void restoreTabs();
    void rebuildThemes();
    void rebuildSnippets();
    std::string userWorkspace() const;
    std::string baseDirFor(const Tab& t) const;

    // ---- tabs and files (app_core.cpp) ----
    Tab* activeTab() { return active_ >= 0 && active_ < static_cast<int>(tabs_.size()) ? tabs_[static_cast<size_t>(active_)].get() : nullptr; }
    Tab& addTab(std::unique_ptr<Tab> t);
    void newFile(const std::string& content = std::string(), const std::string& title = std::string());
    bool openPath(const std::string& path);
    void openExample(const std::string& path);
    bool saveTab(Tab& t, bool saveAs);
    bool saveAll();
    bool closeTab(int index, bool askToSave);
    void activateTab(int index);
    void applyDocOptions(studio::Document& d) const;
    int findTabByPath(const std::string& path) const;
    void refreshTabs();
    void updateTitle();
    void updateStatus();
    std::string uniqueUntitledName() const;

    // ---- window / layout / menus (app_ui.cpp) ----
    bool createWindows(int nCmdShow);
    void buildMenu();
    void refreshDynamicMenus(HMENU popup);
    void layoutChildren();
    void paintMain(HDC dc, const RECT& rc);
    void applyTheme();
    void applySettings(const studio::Settings& s, bool persist);
    void applyFonts();
    void showPanelTab(int tab);
    void refreshExplorer();
    void populateExplorerChildren(HTREEITEM parent, const std::string& dir);
    void onExplorerNotify(NMHDR* n);
    void onProblemsNotify(NMHDR* n);
    bool onCommand(int id);
    void onEditorEvent(int ev, LPARAM lp);
    void onTabEvent(int ev, int index);
    void showSettings();
    void showExtensions();
    void doSwitchUser();
    void zoom(int delta);
    void focusEditor();
    void showShortcuts();
    void onDropFiles(HDROP drop);
    bool handleMouseDown(int x, int y);
    void updateMenuStates();
    int runSelfTest();                                     // app_selftest.cpp

    // ---- running programs (app_run.cpp) ----
    void runProgram();
    void stopProgram();
    void checkNow(bool force);
    void updateProblems();
    void appendConsole(const std::wstring& text, COLORREF color);
    void appendConsoleUtf8(const std::string& text, COLORREF color) { appendConsole(win::widen(text), color); }
    void clearConsole();
    void flushOutput();
    void onRunFinished();
    void onInputRequested();
    void sendInput();
    void printDiagnostics(const std::vector<pcs::Diagnostic>& d);
    static LRESULT CALLBACK inputProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref);

    // ---- state ----
    AppOptions opt_;
    std::filesystem::path dataRoot_, docsRoot_, appDir_;
    std::unique_ptr<studio::AccountStore> accounts_;
    std::unique_ptr<studio::UserProfile> profile_;
    std::unique_ptr<studio::ExtensionManager> exts_;
    studio::Theme theme_;
    std::vector<studio::Theme> allThemes_;
    std::vector<studio::Snippet> snippets_;
    std::vector<std::unique_ptr<Tab>> tabs_;
    int active_ = -1;
    unsigned untitledCounter_ = 0;

    HWND hwnd_ = nullptr;
    HMENU menu_ = nullptr, recentMenu_ = nullptr, templateMenu_ = nullptr, themeMenu_ = nullptr, exampleMenu_ = nullptr, editMenu_ = nullptr, viewMenu_ = nullptr;
    HACCEL accel_ = nullptr;
    win::ToolbarView toolbar_;
    win::TabBar tabbar_;
    win::EditorView editor_;
    win::StatusView status_;
    win::FindDialog find_;
    HWND explorer_ = nullptr, console_ = nullptr, input_ = nullptr, sendBtn_ = nullptr, problems_ = nullptr;
    HFONT uiFont_ = nullptr, uiFontBold_ = nullptr, bigFont_ = nullptr, consoleFont_ = nullptr;
    int dpi_ = 96;
    std::vector<ExplorerNode> explorerNodes_;
    std::vector<WelcomeLink> welcomeLinks_;
    std::vector<std::string> inputHistory_;
    std::vector<std::string> examplePaths_;
    int historyPos_ = -1;

    // layout
    RECT rcEditor_ = {}, rcPanelHeader_ = {}, rcSplitV_ = {}, rcSplitH_ = {}, rcExplorerHeader_ = {}, rcTab0_ = {}, rcTab1_ = {};
    int explorerW_ = 240, consoleH_ = 220, panelTab_ = 0;
    int dragSplit_ = 0;      // 0 none, 1 vertical (explorer), 2 horizontal (panel)
    bool showExplorer_ = true, showPanel_ = true;

    // running
    studio::Runner runner_;
    bool running_ = false, waitingInput_ = false;
    std::mutex outMutex_;
    std::string pendingOut_;
    std::atomic<bool> outPosted_{false};
    size_t totalOut_ = 0;
    bool truncated_ = false;
    studio::RunResult lastResult_;
    DWORD lastEditTick_ = 0, lastAutosaveTick_ = 0;
    std::wstring statusMessage_;
    DWORD statusMessageTick_ = 0;
    ULONGLONG runStartTick_ = 0;
    std::string runTitle_, runSource_;
    bool shuttingDown_ = false;
    bool sessionActive_ = false;
};
