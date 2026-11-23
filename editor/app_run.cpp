// PCScript Studio - running programs, the Output/Problems panels and live error checking.
#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <algorithm>
#include "app.hpp"
#include "files.hpp"
#include "resource.h"
#include "win_util.hpp"

namespace files = studio::files;
using studio::Slot;

static const size_t kMaxProgramOutput = 8u * 1024u * 1024u;   // a runaway OUTPUT loop must not exhaust memory

// ======================================================================= console
void App::appendConsole(const std::wstring& text, COLORREF color) {
    if (text.empty() || !console_) return;
    std::wstring t;
    t.reserve(text.size());
    for (wchar_t c : text) {
        if (c == L'\r') continue;
        t += (c == L'\n') ? L'\r' : c;
    }
    SendMessageW(console_, EM_SETREADONLY, FALSE, 0);
    GETTEXTLENGTHEX gtl = {GTL_DEFAULT, 1200};
    LRESULT len = SendMessageW(console_, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gtl), 0);
    if (len > 3000000) {   // keep the panel responsive: drop the oldest half
        CHARRANGE cut = {0, static_cast<LONG>(len / 2)};
        SendMessageW(console_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cut));
        SendMessageW(console_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    }
    CHARRANGE end = {-1, -1};
    SendMessageW(console_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&end));
    CHARFORMAT2W cf = {};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_EFFECTS;
    cf.crTextColor = color;
    cf.dwEffects = 0;
    SendMessageW(console_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&cf));
    SendMessageW(console_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(t.c_str()));
    SendMessageW(console_, EM_SETREADONLY, TRUE, 0);
    SendMessageW(console_, WM_VSCROLL, SB_BOTTOM, 0);
}

void App::clearConsole() {
    if (!console_) return;
    SendMessageW(console_, EM_SETREADONLY, FALSE, 0);
    SetWindowTextW(console_, L"");
    SendMessageW(console_, EM_SETREADONLY, TRUE, 0);
}

void App::flushOutput() {
    std::string s;
    {
        std::lock_guard<std::mutex> lock(outMutex_);
        s.swap(pendingOut_);
        outPosted_.store(false);
        // never cut a multi-byte character in half: keep an incomplete tail for the next flush
        size_t i = s.size();
        int back = 0;
        while (i > 0 && back < 3 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) { --i; ++back; }
        if (i > 0) {
            unsigned char lead = static_cast<unsigned char>(s[i - 1]);
            int need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (need > 1 && back + 1 < need) { pendingOut_ = s.substr(i - 1) + pendingOut_; s.resize(i - 1); }
        }
    }
    appendConsoleUtf8(s, win::rgb(theme_[Slot::ConsoleFg]));
}

void App::printDiagnostics(const std::vector<pcs::Diagnostic>& diags) {
    for (const pcs::Diagnostic& d : diags) {
        bool err = d.sev == pcs::Severity::Error;
        appendConsoleUtf8(pcs::formatDiagnostic(d, runSource_), win::rgb(err ? theme_[Slot::ConsoleError] : theme_[Slot::Warning]));
        appendConsole(L"\n", win::rgb(theme_[Slot::ConsoleFg]));
    }
}

// ======================================================================= running
void App::runProgram() {
    if (running_) return;
    Tab* t = activeTab();
    if (!t) return;
    if (profile_->settings.saveBeforeRun && !t->path.empty() && t->doc->modified() && !saveTab(*t, false)) return;
    showPanelTab(0);
    if (profile_->settings.clearConsoleOnRun) clearConsole();
    {
        std::lock_guard<std::mutex> lock(outMutex_);
        pendingOut_.clear();
        totalOut_ = 0;
        truncated_ = false;
    }
    runTitle_ = t->title;
    runSource_ = t->doc->text();
    studio::RunRequest req;
    req.source = runSource_;
    req.fileName = t->title;
    req.baseDir = baseDirFor(*t);
    req.timeoutMs = static_cast<long long>(profile_->settings.timeoutSeconds) * 1000;
    req.maxDepth = profile_->settings.maxCallDepth;
    appendConsoleUtf8("> Running " + t->title + "\n", win::rgb(theme_[Slot::Accent]));

    HWND target = hwnd_;
    studio::RunEvents ev;
    ev.onOutput = [this, target](const std::string& s) {
        {
            std::lock_guard<std::mutex> lock(outMutex_);
            if (totalOut_ < kMaxProgramOutput) { pendingOut_ += s; totalOut_ += s.size(); }
            else truncated_ = true;
        }
        if (!outPosted_.exchange(true)) PostMessageW(target, WM_PCS_OUTPUT, 0, 0);
    };
    ev.onInputRequested = [target]() { PostMessageW(target, WM_PCS_INPUT_REQUEST, 0, 0); };
    ev.onFinished = [this, target](const studio::RunResult& r) {
        { std::lock_guard<std::mutex> lock(outMutex_); lastResult_ = r; }
        PostMessageW(target, WM_PCS_FINISHED, 0, 0);
    };
    running_ = true;
    waitingInput_ = false;
    runStartTick_ = GetTickCount64();
    toolbar_.setEnabled(IDM_RUN_RUN, false);
    toolbar_.setEnabled(IDM_RUN_STOP, true);
    if (!runner_.start(req, ev)) {
        running_ = false;
        toolbar_.setEnabled(IDM_RUN_RUN, true);
        toolbar_.setEnabled(IDM_RUN_STOP, false);
    }
    updateStatus();
    InvalidateRect(hwnd_, &rcPanelHeader_, FALSE);
}

void App::stopProgram() {
    if (!running_) return;
    runner_.stop();
    statusMessage_ = L"Stopping...";
    statusMessageTick_ = GetTickCount();
    updateStatus();
}

void App::onInputRequested() {
    if (!running_) return;
    waitingInput_ = true;
    if (!showPanel_ || panelTab_ != 0) showPanelTab(0);
    SetFocus(input_);
    updateStatus();
    InvalidateRect(hwnd_, &rcPanelHeader_, FALSE);
}

void App::sendInput() {
    if (!running_) {
        statusMessage_ = L"No program is running, so there is nothing to send the text to.";
        statusMessageTick_ = GetTickCount();
        updateStatus();
        return;
    }
    std::string text = win::windowTextUtf8(input_);
    SetWindowTextW(input_, L"");
    appendConsoleUtf8(text + "\n", win::rgb(theme_[Slot::ConsoleInput]));
    if (!text.empty() && (inputHistory_.empty() || inputHistory_.back() != text)) inputHistory_.push_back(text);
    historyPos_ = -1;
    runner_.provideInput(text);
    waitingInput_ = false;
    updateStatus();
    InvalidateRect(hwnd_, &rcPanelHeader_, FALSE);
}

LRESULT CALLBACK App::inputProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR ref) {
    App* self = reinterpret_cast<App*>(ref);
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) { self->sendInput(); return 0; }
        if ((wp == VK_UP || wp == VK_DOWN) && !self->inputHistory_.empty()) {
            int n = static_cast<int>(self->inputHistory_.size());
            int pos = self->historyPos_;
            if (wp == VK_UP) pos = pos < 0 ? n - 1 : std::max(0, pos - 1);
            else pos = pos < 0 ? -1 : (pos + 1 < n ? pos + 1 : -1);
            self->historyPos_ = pos;
            win::setWindowText(h, pos < 0 ? std::string() : self->inputHistory_[static_cast<size_t>(pos)]);
            SendMessageW(h, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
            return 0;
        }
    }
    if (msg == WM_CHAR && wp == VK_RETURN) return 0;      // no "ding" for Enter
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, &App::inputProc, 1);
    return DefSubclassProc(h, msg, wp, lp);
}

void App::onRunFinished() {
    runner_.wait();
    flushOutput();
    studio::RunResult r;
    std::string leftover;
    bool trunc;
    {
        std::lock_guard<std::mutex> lock(outMutex_);
        r = lastResult_;
        leftover.swap(pendingOut_);
        outPosted_.store(false);
        trunc = truncated_;
    }
    appendConsoleUtf8(leftover, win::rgb(theme_[Slot::ConsoleFg]));
    if (trunc) appendConsoleUtf8("\n[Output was cut off: the program printed more than 8 MB.]\n", win::rgb(theme_[Slot::Warning]));
    running_ = false;
    waitingInput_ = false;
    toolbar_.setEnabled(IDM_RUN_RUN, true);
    toolbar_.setEnabled(IDM_RUN_STOP, false);

    char secs[32];
    std::snprintf(secs, sizeof secs, "%.2f", static_cast<double>(r.elapsedMs) / 1000.0);
    COLORREF ok = win::rgb(theme_[Slot::Accent]), bad = win::rgb(theme_[Slot::ConsoleError]);
    switch (r.status) {
        case studio::RunStatus::Completed:
            printDiagnostics(r.diagnostics);
            appendConsoleUtf8(std::string("\n[Finished in ") + secs + " s]\n", ok);
            break;
        case studio::RunStatus::CompileError:
            printDiagnostics(r.diagnostics);
            appendConsoleUtf8("[Not run: please fix the errors above]\n", bad);
            break;
        case studio::RunStatus::RuntimeError:
            appendConsoleUtf8("\n", bad);
            printDiagnostics(r.diagnostics);
            appendConsoleUtf8("[The program stopped because of an error]\n", bad);
            break;
        case studio::RunStatus::Stopped:
            appendConsoleUtf8("\n[Stopped]\n", win::rgb(theme_[Slot::Warning]));
            break;
        case studio::RunStatus::Internal:
            appendConsoleUtf8("\n[Internal error: " + r.message + "]\n", bad);
            break;
    }
    lastResult_ = r;
    Tab* t = activeTab();
    if (t && t->title == runTitle_ && !r.diagnostics.empty()) {
        t->diags = r.diagnostics;
        t->checkedVersion = t->doc->version();       // keep these squiggles until the text changes
        InvalidateRect(editor_.hwnd(), nullptr, FALSE);
    }
    updateProblems();
    statusMessage_.clear();
    updateStatus();
    InvalidateRect(hwnd_, &rcPanelHeader_, FALSE);
    if (t && !waitingInput_) focusEditor();
}

// ======================================================================= problems
void App::checkNow(bool force) {
    Tab* t = activeTab();
    if (!t) return;
    if (!force) {
        if (!profile_->settings.checkWhileTyping) return;
        if (t->doc->version() == t->checkedVersion) return;
        if (GetTickCount() - lastEditTick_ < 500) return;
    }
    t->checkedVersion = t->doc->version();
    t->diags = studio::checkSource(t->doc->text(), t->title);
    updateProblems();
    InvalidateRect(editor_.hwnd(), nullptr, FALSE);
    if (force) {
        statusMessage_ = t->diags.empty() ? L"No problems found." : std::to_wstring(t->diags.size()) + L" problem(s) found.";
        statusMessageTick_ = GetTickCount();
    }
    updateStatus();
}

void App::updateProblems() {
    if (!problems_) return;
    SendMessageW(problems_, LVM_DELETEALLITEMS, 0, 0);
    if (Tab* t = activeTab()) {
        int row = 0;
        for (const pcs::Diagnostic& d : t->diags) {
            std::wstring mark = d.sev == pcs::Severity::Error ? L"\x25CF" : L"\x25B2";
            LVITEMW it = {};
            it.mask = LVIF_TEXT | LVIF_PARAM;
            it.iItem = row;
            it.pszText = const_cast<LPWSTR>(mark.c_str());
            it.lParam = row;
            SendMessageW(problems_, LVM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&it));
            std::wstring code = win::widen(d.code), line = std::to_wstring(d.line), msg = win::widen(d.message + (d.hint.empty() ? "" : "  (" + d.hint + ")"));
            std::wstring* cols[] = {&code, &line, &msg};
            for (int c = 0; c < 3; ++c) {
                LVITEMW sub = {};
                sub.iSubItem = c + 1;
                sub.pszText = const_cast<LPWSTR>(cols[c]->c_str());
                SendMessageW(problems_, LVM_SETITEMTEXTW, static_cast<WPARAM>(row), reinterpret_cast<LPARAM>(&sub));
            }
            ++row;
        }
    }
    if (hwnd_) InvalidateRect(hwnd_, &rcPanelHeader_, FALSE);
}

void App::onProblemsNotify(NMHDR* n) {
    if (n->code != NM_DBLCLK) return;
    Tab* t = activeTab();
    if (!t) return;
    int sel = static_cast<int>(SendMessageW(problems_, LVM_GETNEXTITEM, static_cast<WPARAM>(-1), LVNI_SELECTED));
    if (sel < 0 || sel >= static_cast<int>(t->diags.size())) return;
    const pcs::Diagnostic& d = t->diags[static_cast<size_t>(sel)];
    editor_.revealLine(std::max(0, d.line - 1));
    t->doc->setCaret({std::max(0, d.line - 1), std::max(0, d.col - 1)});
    editor_.documentChanged();
    focusEditor();
}
