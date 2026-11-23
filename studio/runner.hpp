// Runs a PCScript program on a worker thread and connects its INPUT / OUTPUT to callbacks (the IDE console).
// All callbacks are invoked on the worker thread; the GUI must hand them over to its own thread (PostMessage).
#pragma once
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "common/diagnostics.hpp"

namespace studio {

// Parse + static analysis only. Never throws; returns every error and warning found (empty = clean).
std::vector<pcs::Diagnostic> checkSource(const std::string& source, const std::string& fileName);

struct RunRequest {
    std::string source;
    std::string fileName;          // for messages, e.g. "program.pcs"
    std::string baseDir;           // relative file names in OPENFILE etc. are resolved against this folder
    long long timeoutMs = 0;       // 0 = unlimited
    int maxDepth = 5000;
    bool hasSeed = false;
    unsigned long long seed = 0;
};

enum class RunStatus { Completed, CompileError, RuntimeError, Stopped, Internal };

struct RunResult {
    RunStatus status = RunStatus::Completed;
    int exitCode = 0;                          // same convention as the command line: 0 ok, 1 compile error, 2 runtime error
    std::vector<pcs::Diagnostic> diagnostics;  // compile errors and warnings, or the single runtime error
    std::string message;                       // internal error text
    long long elapsedMs = 0;
};

struct RunEvents {
    std::function<void(const std::string&)> onOutput;     // text produced by OUTPUT (may arrive in pieces)
    std::function<void()> onInputRequested;               // the program is blocked in INPUT waiting for a line
    std::function<void(const RunResult&)> onFinished;     // always called exactly once per start()
};

class Runner {
public:
    Runner();
    ~Runner();
    Runner(const Runner&) = delete;
    Runner& operator=(const Runner&) = delete;

    bool start(RunRequest req, RunEvents events);   // false if a program is already running
    void provideInput(const std::string& line);     // answer to INPUT (without the line ending); may be called before it is requested
    void endInput();                                // no more input will come: a waiting INPUT fails with R309 (like Ctrl+Z / end of file)
    void stop();                                    // asks the program to end; returns immediately
    bool running() const { return state_.load() == 1; }
    void wait();                                    // blocks until the program has finished and the thread is joined

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::atomic<int> state_{0};   // 0 idle, 1 running, 2 finished (thread not yet joined)
    friend struct RunnerAccess;
};

} // namespace studio
