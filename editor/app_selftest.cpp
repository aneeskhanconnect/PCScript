// "pcscript-studio.exe --selftest": drives the real GUI code without showing it (windows exist but stay hidden) and
// writes PASS/FAIL lines to a log. Used by CI on Windows; it is the only automated check of the GUI layer.
#include <windows.h>
#include <commctrl.h>
#include <fstream>
#include <functional>
#include "app.hpp"
#include "files.hpp"
#include "resource.h"
#include "win_util.hpp"

namespace fs = std::filesystem;
namespace files = studio::files;

int App::runSelfTest() {
    std::string logPath = opt_.selfTestLog.empty() ? files::toUtf8(dataRoot_ / "selftest.log") : opt_.selfTestLog;
    std::ofstream log(files::fromUtf8(logPath), std::ios::trunc);
    int failures = 0, passes = 0;
    auto check = [&](bool ok, const std::string& what) {
        log << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        log.flush();
        if (ok) ++passes; else ++failures;
    };
    auto pump = [&](int ms, const std::function<bool()>& until) {
        ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(ms);
        MSG m;
        for (;;) {
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
            if (until && until()) return true;
            if (GetTickCount64() >= end) return until ? until() : true;
            Sleep(10);
        }
    };
    auto consoleText = [&]() { return win::windowTextUtf8(console_); };
    auto typeInto = [&](const std::string& text) {
        std::wstring w = win::widen(text);
        for (wchar_t c : w) {
            if (c == L'\n') SendMessageW(editor_.hwnd(), WM_KEYDOWN, VK_RETURN, 0);
            else SendMessageW(editor_.hwnd(), WM_CHAR, static_cast<WPARAM>(c), 0);
        }
    };
    auto runAndWait = [&](const std::string& src) {
        Tab* t = activeTab();
        t->doc->setText(src);
        editor_.refresh();
        runProgram();
        return pump(15000, [&] { return !running_; });
    };

    check(hwnd_ != nullptr && IsWindow(hwnd_), "main window was created");
    check(!tabs_.empty() && activeTab() != nullptr, "a first tab exists");
    check(editor_.hwnd() != nullptr && explorer_ && console_ && problems_, "child windows exist");
    check(exts_ && exts_->all().size() >= 3, "bundled extensions were found next to the program");
    check(allThemes_.size() >= 6, "themes are available (6 built in + extensions)");

    // ---- typing, auto-capitalisation, autocomplete ----
    {
        Tab* t = activeTab();
        t->doc->setText("");
        editor_.attach(t->doc.get());
        typeInto("output 1+2");
        check(t->doc->text() == "OUTPUT 1+2", "typing 'output 1+2' becomes 'OUTPUT 1+2' (got '" + t->doc->text() + "')");
        t->doc->setText("");
        editor_.attach(t->doc.get());
        typeInto("OUT");
        check(editor_.completionVisible(), "the suggestion drop-down opens while typing");
        SendMessageW(editor_.hwnd(), WM_KEYDOWN, VK_ESCAPE, 0);
        check(!editor_.completionVisible(), "Esc closes the drop-down");
        t->doc->setText("");
        editor_.attach(t->doc.get());
        typeInto("if x then\n");
        check(t->doc->text().rfind("IF x THEN\n    ", 0) == 0, "Enter after IF ... THEN auto-indents (got '" + t->doc->text() + "')");
    }

    // ---- running programs ----
    check(runAndWait("OUTPUT 1 + 2\n"), "a simple program finishes");
    check(lastResult_.status == studio::RunStatus::Completed, "run status is Completed");
    check(consoleText().find("3") != std::string::npos, "the Output panel shows the program's output");

    {
        Tab* t = activeTab();
        t->doc->setText("DECLARE x : INTEGER\nOUTPUT \"Number?\"\nINPUT x\nOUTPUT x * 2\n");
        runProgram();
        bool asked = pump(15000, [&] { return waitingInput_; });
        check(asked, "a program waiting for INPUT is detected");
        SetWindowTextW(input_, L"21");
        sendInput();
        bool done = pump(15000, [&] { return !running_; });
        check(done && consoleText().find("42") != std::string::npos, "typed input reaches the program and its answer is shown");
    }
    check(runAndWait("OUTPUT undeclared\n"), "a program with an error finishes");
    check(lastResult_.status == studio::RunStatus::CompileError && !activeTab()->diags.empty(), "compile errors are reported and marked");
    {
        Tab* t = activeTab();
        t->doc->setText("WHILE TRUE DO\nENDWHILE\n");
        runProgram();
        pump(300, nullptr);
        bool wasRunning = running_;
        stopProgram();
        bool stopped = pump(10000, [&] { return !running_; });
        check(wasRunning && stopped && lastResult_.status == studio::RunStatus::Stopped, "an infinite loop can be stopped");
    }

    // ---- live checking, problems list ----
    {
        Tab* t = activeTab();
        t->doc->setText("IF TRUE THEN\n");
        checkNow(true);
        int rows = static_cast<int>(SendMessageW(problems_, LVM_GETITEMCOUNT, 0, 0));
        check(!t->diags.empty() && rows == static_cast<int>(t->diags.size()), "the Problems list shows the diagnostics");
        t->doc->setText("OUTPUT 1\n");
        checkNow(true);
        check(t->diags.empty(), "a correct program has no problems");
    }

    // ---- formatting ----
    {
        Tab* t = activeTab();
        t->doc->setText("if x then\noutput 1\nendif");
        editor_.attach(t->doc.get());
        editor_.cmdFormat();
        check(t->doc->text() == "IF x THEN\n    OUTPUT 1\nENDIF\n", "Format Document pretty-prints (got '" + t->doc->text() + "')");
    }

    // ---- saving, tabs, recovery ----
    {
        Tab* t = activeTab();
        fs::path file = files::fromUtf8(userWorkspace()) / "selftest_program.pcs";
        t->path = files::toUtf8(file);
        t->title = "selftest_program.pcs";
        t->doc->setText("OUTPUT \"saved\"");
        bool saved = saveTab(*t, false);
        std::string onDisk;
        files::readText(file, onDisk);
        check(saved && onDisk == "OUTPUT \"saved\"\n", "Save writes the file (got '" + onDisk + "')");
        check(!t->doc->modified(), "the document is marked as saved");
        size_t before = tabs_.size();
        newFile("OUTPUT 1\n");
        newFile();
        check(tabs_.size() == before + 2, "New File adds tabs");
        tabs_.back()->doc->typeChar("x");
        saveSessionSnapshot();
        check(!profile_->loadRecovery().empty(), "unsaved text is kept in the crash-recovery store");
        check(closeTab(static_cast<int>(tabs_.size()) - 1, false), "a tab can be closed");
        check(openPath(files::toUtf8(file)) && findTabByPath(files::toUtf8(file)) >= 0, "a saved file can be reopened");
    }

    // ---- settings and themes ----
    {
        studio::Settings s = profile_->settings;
        s.fontSize = 20; s.lineSpacing = 160; s.tabSize = 2; s.showIndentGuides = false;
        applySettings(s, true);
        check(profile_->settings.fontSize == 20 && activeTab()->doc->options().tabSize == 2, "settings apply to the editor");
        bool allOk = true;
        for (const studio::Theme& th : allThemes_) {
            studio::Settings t = profile_->settings;
            t.theme = th.name;
            applySettings(t, false);
            if (theme_.name != th.name) allOk = false;
        }
        check(allOk, "every theme can be applied");
        studio::UserProfile again(profile_->dir());
        again.load();
        check(again.settings.fontSize == 20, "settings were saved to disk");
        zoom(1);
        check(profile_->settings.fontSize == 21, "zoom changes the font size");
    }

    log << "\n" << passes << " passed, " << failures << " failed\n";
    log.close();
    saveSessionSnapshot();
    for (auto& t : tabs_) t->doc->markSaved();
    endSession(false);
    shuttingDown_ = true;
    DestroyWindow(hwnd_);
    return failures == 0 ? 0 : 1;
}
