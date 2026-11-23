// PCScript Studio - startup, accounts/session, tabs and file handling.
#include <algorithm>
#include <fstream>
#include <set>
#include "app.hpp"
#include "files.hpp"
#include "resource.h"
#include "scan.hpp"
#include "win_util.hpp"

namespace fs = std::filesystem;
namespace files = studio::files;

App::App(AppOptions opt) : opt_(std::move(opt)) {}

App::~App() {
    shuttingDown_ = true;
    runner_.stop();
    runner_.wait();
    if (accel_) DestroyAcceleratorTable(accel_);
    for (HFONT f : {uiFont_, uiFontBold_, bigFont_, consoleFont_}) if (f) DeleteObject(f);
}

// ======================================================================= startup
int App::run(int nCmdShow) {
    std::string appData = opt_.dataDir.empty() ? win::knownFolderAppData() + "\\PCScript Studio" : opt_.dataDir;
    std::string docs = opt_.docsDir.empty() ? win::knownFolderDocuments() + "\\PCScript Studio" : opt_.docsDir;
    dataRoot_ = files::fromUtf8(appData);
    docsRoot_ = files::fromUtf8(docs);
    appDir_ = files::fromUtf8(win::moduleDirectory());
    std::string err;
    if (!files::ensureDir(dataRoot_, &err) || !files::ensureDir(docsRoot_, &err)) {
        win::warn(nullptr, "PCScript Studio", "Cannot create the data folders:\n" + err);
        return 1;
    }
    accounts_ = std::make_unique<studio::AccountStore>(dataRoot_);
    if (!accounts_->load(&err)) win::warn(nullptr, "PCScript Studio", "The accounts file could not be read:\n" + err + "\n\nYou can still continue as Guest.");

    if (!signIn()) return 0;
    startSession();
    if (!createWindows(nCmdShow)) return 1;
    if (opt_.selfTest) return runSelfTest();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (find_.hwnd() && IsWindow(find_.hwnd()) && IsDialogMessageW(find_.hwnd(), &msg)) continue;
        if (accel_ && TranslateAcceleratorW(hwnd_, accel_, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

bool App::signIn() {
    if (opt_.selfTest) { accounts_->loginGuest(); return true; }
    std::string last;
    {
        std::string text;
        if (files::readText(dataRoot_ / "last_user.txt", text)) last = text.substr(0, text.find_first_of("\r\n"));
    }
    win::LoginResult r = win::showLoginDialog(nullptr, *accounts_, last);
    if (r == win::LoginResult::Quit) return false;
    if (r == win::LoginResult::SignedIn) files::writeTextAtomic(dataRoot_ / "last_user.txt", accounts_->currentUser());
    return true;
}

std::string App::userWorkspace() const {
    return files::toUtf8(docsRoot_ / files::fromUtf8(accounts_->currentUser()));
}

void App::startSession() {
    profile_ = std::make_unique<studio::UserProfile>(accounts_->profileDir(accounts_->currentUser()));
    std::string warning;
    profile_->load(&warning);
    if (!warning.empty()) { statusMessage_ = win::widen(warning.substr(0, warning.find('\n'))); statusMessageTick_ = GetTickCount(); }
    exts_ = std::make_unique<studio::ExtensionManager>(appDir_ / "extensions", dataRoot_ / "extensions");
    exts_->setDisabled(profile_->disabledExtensions);
    exts_->scan();
    rebuildThemes();
    rebuildSnippets();
    untitledCounter_ = 0;
    files::ensureDir(files::fromUtf8(userWorkspace()));
    sessionActive_ = true;
}

void App::rebuildThemes() {
    allThemes_.clear();
    auto add = [&](const studio::Theme& t) {
        for (const studio::Theme& e : allThemes_) if (e.name == t.name) return;
        allThemes_.push_back(t);
    };
    for (const studio::Theme& t : studio::builtinThemes()) add(t);
    for (const studio::Theme& t : exts_->themes()) add(t);
    for (const studio::Theme& t : profile_->loadUserThemes()) add(t);
}

void App::rebuildSnippets() { snippets_ = exts_->snippets(); }

void App::restoreTabs() {
    std::vector<studio::RecoveryItem> recovery = profile_->loadRecovery();
    std::set<std::string> used;
    if (profile_->settings.restoreSession) {
        for (const studio::SessionTab& st : profile_->session.tabs) {
            if (!st.recoveryId.empty()) {
                for (const studio::RecoveryItem& r : recovery) {
                    if (r.id != st.recoveryId) continue;
                    auto t = std::make_unique<Tab>();
                    t->doc = std::make_unique<studio::Document>();
                    t->doc->setText(r.content);
                    applyDocOptions(*t->doc);
                    t->doc->markDirty();
                    t->path = r.originalPath;
                    t->title = t->path.empty() ? r.title : files::toUtf8(files::fromUtf8(t->path).filename());
                    t->recoveryId = r.id;
                    t->doc->setCaret({st.line, st.col});
                    t->topLine = st.topLine;
                    used.insert(r.id);
                    addTab(std::move(t));
                    break;
                }
            } else if (!st.path.empty() && findTabByPath(st.path) < 0) {
                std::error_code ec;
                if (fs::exists(files::fromUtf8(st.path), ec) && openPath(st.path)) {
                    Tab& t = *tabs_.back();
                    t.doc->setCaret({st.line, st.col});
                    t.topLine = st.topLine;
                }
            }
        }
    }
    for (const studio::RecoveryItem& r : recovery) if (!used.count(r.id)) profile_->clearRecovery(r.id);   // leftovers nobody refers to
    const bool hadFiles = !opt_.files.empty();
    for (const std::string& f : opt_.files) openPath(f);
    opt_.files.clear();
    if (tabs_.empty()) newFile();
    int want = profile_->session.activeTab;
    if (hadFiles) want = static_cast<int>(tabs_.size()) - 1;
    activateTab(std::max(0, std::min(want, static_cast<int>(tabs_.size()) - 1)));
}

void App::adoptSession() {
    // called once the windows exist (first start) and after switching users
    editor_.setSettings(&profile_->settings);
    editor_.setSnippets(&snippets_);
    showExplorer_ = profile_->settings.showExplorer;
    showPanel_ = profile_->settings.showConsole;
    explorerW_ = profile_->session.window.explorerWidth;
    consoleH_ = profile_->session.window.consoleHeight;
    applySettings(profile_->settings, false);
    refreshExplorer();
    restoreTabs();
    lastAutosaveTick_ = GetTickCount();
}

// ======================================================================= tabs
std::string App::baseDirFor(const Tab& t) const {
    if (!t.path.empty()) return files::toUtf8(files::fromUtf8(t.path).parent_path());
    return userWorkspace();
}

void App::applyDocOptions(studio::Document& d) const {
    const studio::Settings& s = profile_->settings;
    d.options().tabSize = s.tabSize;
    d.options().insertSpaces = s.insertSpaces;
    d.options().autoIndent = s.autoIndent;
    d.options().autoCloseBrackets = s.autoCloseBrackets;
    d.options().autoCloseBlocks = s.autoCloseBlocks;
    d.options().autoCapitalizeKeywords = s.autoCapitalizeKeywords;
}

App::Tab& App::addTab(std::unique_ptr<Tab> t) {
    tabs_.push_back(std::move(t));
    refreshTabs();
    return *tabs_.back();
}

std::string App::uniqueUntitledName() const {
    for (;;) {
        std::string n = "Untitled" + std::to_string(++const_cast<App*>(this)->untitledCounter_) + ".pcs";
        bool clash = false;
        for (const auto& t : tabs_) if (t->title == n) clash = true;
        std::error_code ec;
        if (!clash && !fs::exists(files::fromUtf8(userWorkspace()) / files::fromUtf8(n), ec)) return n;
    }
}

void App::newFile(const std::string& content, const std::string& title) {
    auto t = std::make_unique<Tab>();
    t->doc = std::make_unique<studio::Document>();
    t->doc->setText(content);
    applyDocOptions(*t->doc);
    t->title = title.empty() ? uniqueUntitledName() : title;
    addTab(std::move(t));
    activateTab(static_cast<int>(tabs_.size()) - 1);
}

int App::findTabByPath(const std::string& path) const {
    std::error_code ec;
    std::string want = studio::toLower(files::toUtf8(fs::absolute(files::fromUtf8(path), ec).lexically_normal()));
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i]->path.empty()) continue;
        std::string have = studio::toLower(files::toUtf8(fs::absolute(files::fromUtf8(tabs_[i]->path), ec).lexically_normal()));
        if (have == want) return static_cast<int>(i);
    }
    return -1;
}

bool App::openPath(const std::string& pathIn) {
    std::error_code ec;
    fs::path p = fs::absolute(files::fromUtf8(pathIn), ec).lexically_normal();
    std::string path = files::toUtf8(p);
    int existing = findTabByPath(path);
    if (existing >= 0) { activateTab(existing); return true; }
    if (fs::is_regular_file(p, ec) && fs::file_size(p, ec) > 5ull * 1024 * 1024) {
        win::warn(hwnd_, "Open", "That file is larger than 5 MB, which is too big for a program. It was not opened.");
        return false;
    }
    std::string text, err;
    if (!files::readText(p, text, &err)) {
        win::warn(hwnd_, "Open", err);
        profile_->removeRecent(path);
        return false;
    }
    if (text.find('\0') != std::string::npos) {
        win::warn(hwnd_, "Open", "That file looks like a binary file, not a text program. It was not opened.");
        return false;
    }
    auto t = std::make_unique<Tab>();
    t->doc = std::make_unique<studio::Document>();
    t->doc->setText(text);
    applyDocOptions(*t->doc);
    t->path = path;
    t->title = files::toUtf8(p.filename());
    addTab(std::move(t));
    profile_->addRecent(path);
    if (hwnd_) activateTab(static_cast<int>(tabs_.size()) - 1);
    return true;
}

void App::openExample(const std::string& path) {
    std::string text, err;
    if (!files::readText(files::fromUtf8(path), text, &err)) { win::warn(hwnd_, "Open example", err); return; }
    newFile(text, files::toUtf8(files::fromUtf8(path).filename()));
    statusMessage_ = L"Example opened as a new file. Use File > Save As to keep your own copy.";
    statusMessageTick_ = GetTickCount();
    updateStatus();
}

bool App::saveTab(Tab& t, bool saveAs) {
    std::string path = t.path;
    if (path.empty() || saveAs) {
        std::string initial = path.empty() ? userWorkspace() : files::toUtf8(files::fromUtf8(path).parent_path());
        std::string suggest = path.empty() ? t.title : files::toUtf8(files::fromUtf8(path).filename());
        if (!win::saveFileDialog(hwnd_, initial, suggest, path)) return false;
        if (files::fromUtf8(path).extension().empty()) path += ".pcs";
    }
    const studio::Settings& s = profile_->settings;
    studio::Document& d = *t.doc;
    if (s.formatOnSave) {
        studio::FormatOptions fo;
        fo.tabSize = s.tabSize; fo.insertSpaces = s.insertSpaces;
        d.formatDocument(fo);
    }
    if (s.trimTrailingWhitespace) d.trimTrailingWhitespace();
    if (s.insertFinalNewline && !d.line(d.lineCount() - 1).empty()) {
        studio::Pos c = d.caret();
        d.docEnd(false);
        d.insertText("\n");
        d.setCaret(c);
    }
    std::string err;
    if (!files::writeTextAtomic(files::fromUtf8(path), d.text(), &err)) {
        win::warn(hwnd_, "Save", "The file could not be saved.\n\n" + err);
        return false;
    }
    t.path = path;
    t.title = files::toUtf8(files::fromUtf8(path).filename());
    d.markSaved();
    t.autosavedVersion = d.version();
    if (!t.recoveryId.empty()) { profile_->clearRecovery(t.recoveryId); t.recoveryId.clear(); }
    profile_->addRecent(path);
    if (activeTab() == &t) editor_.refresh();
    refreshTabs();
    updateTitle();
    refreshExplorer();
    statusMessage_ = L"Saved " + win::widen(t.title);
    statusMessageTick_ = GetTickCount();
    updateStatus();
    return true;
}

bool App::saveAll() {
    for (auto& t : tabs_) if (t->doc->modified() && !saveTab(*t, false)) return false;
    return true;
}

bool App::closeTab(int index, bool askToSave) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return true;
    Tab& t = *tabs_[static_cast<size_t>(index)];
    if (askToSave && t.doc->modified()) {
        activateTab(index);
        int a = win::askSaveChanges(hwnd_, t.title);
        if (a == IDCANCEL) return false;
        if (a == IDYES && !saveTab(t, false)) return false;
    }
    if (!t.recoveryId.empty()) profile_->clearRecovery(t.recoveryId);
    bool wasActive = index == active_;
    if (wasActive) { editor_.attach(nullptr); editor_.setDiagnostics(nullptr); }
    tabs_.erase(tabs_.begin() + index);
    if (tabs_.empty()) active_ = -1;
    else if (wasActive) active_ = std::min(index, static_cast<int>(tabs_.size()) - 1);
    else if (index < active_) --active_;
    int next = active_;
    active_ = -2;   // force activateTab to re-attach
    activateTab(next);
    layoutChildren();
    return true;
}

void App::activateTab(int index) {
    if (index >= static_cast<int>(tabs_.size())) index = static_cast<int>(tabs_.size()) - 1;
    if (active_ >= 0 && active_ < static_cast<int>(tabs_.size()) && active_ != index) tabs_[static_cast<size_t>(active_)]->topLine = editor_.topLine();
    active_ = index;
    if (Tab* t = activeTab()) {
        editor_.attach(t->doc.get());
        editor_.setDiagnostics(&t->diags);
        editor_.setTopLine(t->topLine);
        editor_.ensureCaretVisible();
        ShowWindow(editor_.hwnd(), SW_SHOW);
        t->checkedVersion = ~0u;           // re-check soon
        lastEditTick_ = GetTickCount() - 400;
    } else {
        editor_.attach(nullptr);
        editor_.setDiagnostics(nullptr);
        if (editor_.hwnd()) ShowWindow(editor_.hwnd(), SW_HIDE);
    }
    refreshTabs();
    updateTitle();
    updateProblems();
    updateStatus();
    layoutChildren();
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    if (activeTab()) focusEditor();
}

void App::focusEditor() { if (editor_.hwnd() && IsWindowVisible(editor_.hwnd())) SetFocus(editor_.hwnd()); }

void App::refreshTabs() {
    if (!tabbar_.hwnd()) return;
    std::vector<win::TabInfo> v;
    for (const auto& t : tabs_) v.push_back({win::widen(t->title), t->doc->modified()});
    tabbar_.setTabs(std::move(v), active_);
}

std::wstring App::windowTitleText() const {
    std::wstring t;
    if (active_ >= 0 && active_ < static_cast<int>(tabs_.size())) {
        const Tab& tab = *tabs_[static_cast<size_t>(active_)];
        t = win::widen(tab.title) + (tab.doc->modified() ? L"*" : L"") + L" - ";
    }
    return t + L"PCScript Studio";
}

void App::updateTitle() { if (hwnd_) SetWindowTextW(hwnd_, windowTitleText().c_str()); }

void App::updateStatus() {
    if (!status_.hwnd()) return;
    std::wstring left;
    if (running_) left = waitingInput_ ? L"Waiting for input - type it in the Output panel and press Enter" : L"Running...";
    else if (!statusMessage_.empty() && GetTickCount() - statusMessageTick_ < 6000) left = statusMessage_;
    else if (Tab* t = activeTab()) {
        int line = t->doc->caret().line + 1;
        for (const pcs::Diagnostic& d : t->diags) if (d.line == line) { left = win::widen(d.code + ": " + d.message); break; }
        if (left.empty()) left = L"Ready";
    } else left = L"Ready";
    std::vector<std::wstring> right;
    if (Tab* t = activeTab()) {
        studio::Pos c = t->doc->caret();
        right.push_back(L"Ln " + std::to_wstring(c.line + 1) + L", Col " + std::to_wstring(t->doc->displayColumn(c.line, c.col) + 1));
        if (t->doc->hasSelection()) right.push_back(std::to_wstring(t->doc->selectedText().size()) + L" selected");
        right.push_back(profile_->settings.insertSpaces ? L"Spaces: " + std::to_wstring(profile_->settings.tabSize) : L"Tabs");
        right.push_back(L"UTF-8");
        right.push_back(t->doc->eol() == "\r\n" ? L"CRLF" : L"LF");
    }
    right.push_back(win::widen(accounts_->currentDisplayName().empty() ? accounts_->currentUser() : accounts_->currentDisplayName()));
    status_.setLeft(left);
    status_.setRight(std::move(right));
}

// ======================================================================= session end / snapshots
void App::saveSessionSnapshot() {
    if (!profile_ || !sessionActive_) return;
    studio::Session& s = profile_->session;
    s.tabs.clear();
    for (auto& tp : tabs_) {
        Tab& t = *tp;
        bool dirty = t.doc->modified() || (t.path.empty() && !(t.doc->lineCount() == 1 && t.doc->line(0).empty()));
        if (dirty && t.doc->version() != t.recoveredVersion) {
            if (t.recoveryId.empty()) t.recoveryId = studio::UserProfile::newRecoveryId();
            studio::RecoveryItem item{t.recoveryId, t.title, t.path, t.doc->text()};
            profile_->saveRecovery(item);
            t.recoveredVersion = t.doc->version();
        }
        studio::SessionTab st;
        st.path = t.path;
        st.recoveryId = dirty ? t.recoveryId : std::string();
        st.line = t.doc->caret().line; st.col = t.doc->caret().col;
        st.topLine = (&t == activeTab()) ? editor_.topLine() : t.topLine;
        if (st.path.empty() && st.recoveryId.empty()) continue;
        s.tabs.push_back(st);
    }
    s.activeTab = std::max(0, std::min(active_, static_cast<int>(s.tabs.size()) - 1));
    if (hwnd_) {
        WINDOWPLACEMENT wp = {sizeof(wp)};
        if (GetWindowPlacement(hwnd_, &wp)) {
            s.window.x = wp.rcNormalPosition.left; s.window.y = wp.rcNormalPosition.top;
            s.window.w = wp.rcNormalPosition.right - wp.rcNormalPosition.left; s.window.h = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
            s.window.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
        }
        s.window.explorerWidth = explorerW_; s.window.consoleHeight = consoleH_;
    }
    profile_->save();
}

bool App::endSession(bool askToSave) {
    if (!sessionActive_) return true;
    if (running_) { runner_.stop(); runner_.wait(); running_ = false; waitingInput_ = false; }
    if (askToSave) {
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            Tab& t = *tabs_[static_cast<size_t>(i)];
            if (!t.doc->modified()) continue;
            activateTab(i);
            int a = win::askSaveChanges(hwnd_, t.title);
            if (a == IDCANCEL) return false;
            if (a == IDYES && !saveTab(t, false)) return false;
        }
    }
    // everything is either saved or deliberately discarded: no crash-recovery data is needed any more
    for (auto& t : tabs_) if (!t->recoveryId.empty()) { profile_->clearRecovery(t.get()->recoveryId); t->recoveryId.clear(); }
    for (auto& t : tabs_) if (t->doc->modified()) t->doc->markSaved();
    saveSessionSnapshot();
    tabs_.clear();
    active_ = -1;
    editor_.attach(nullptr);
    sessionActive_ = false;
    return true;
}

void App::autoSaveTick() {
    int secs = profile_->settings.autoSaveSeconds;
    if (secs <= 0) return;
    if (GetTickCount() - lastAutosaveTick_ < static_cast<DWORD>(secs) * 1000) return;
    lastAutosaveTick_ = GetTickCount();
    for (auto& t : tabs_) {
        if (t->path.empty() || !t->doc->modified() || t->doc->version() == t->autosavedVersion) continue;
        std::string err;
        if (files::writeTextAtomic(files::fromUtf8(t->path), t->doc->text(), &err)) {
            t->doc->markSaved();
            t->autosavedVersion = t->doc->version();
        }
    }
    refreshTabs();
    updateTitle();
}
