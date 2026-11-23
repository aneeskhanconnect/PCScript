#include "runner.hpp"
#include <chrono>
#include <istream>
#include <ostream>
#include <streambuf>
#include <thread>
#include "parser/parser.hpp"
#include "runtime/interpreter.hpp"
#include "semantic/semantic.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace studio {

std::vector<pcs::Diagnostic> checkSource(const std::string& source, const std::string& fileName) {
    try {
        pcs::Program p = pcs::parseSource(source, fileName);
        return pcs::analyze(p);
    } catch (const pcs::PcsError& e) {
        return {e.d};
    } catch (const std::exception& e) {
        pcs::Diagnostic d; d.code = "E000"; d.category = "Internal error"; d.message = e.what(); d.line = 1; d.col = 1; d.file = fileName;
        return {d};
    }
}

namespace {

class InBuf : public std::streambuf {
public:
    std::function<void()> onNeed;
    void push(const std::string& line) { { std::lock_guard<std::mutex> l(m_); q_.push_back(line); } cv_.notify_all(); }
    void close() { { std::lock_guard<std::mutex> l(m_); closed_ = true; } cv_.notify_all(); }
protected:
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        std::unique_lock<std::mutex> lk(m_);
        if (q_.empty() && !closed_) {
            auto cb = onNeed;
            lk.unlock();
            if (cb) cb();
            lk.lock();
            cv_.wait(lk, [&] { return !q_.empty() || closed_; });
        }
        if (q_.empty()) return traits_type::eof();
        cur_ = q_.front() + "\n";
        q_.pop_front();
        setg(&cur_[0], &cur_[0], &cur_[0] + cur_.size());
        return traits_type::to_int_type(cur_[0]);
    }
private:
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<std::string> q_;
    bool closed_ = false;
    std::string cur_;
};

class OutBuf : public std::streambuf {
public:
    std::function<void(const std::string&)> sink;
    void flushNow() { if (!pending_.empty()) { std::string s; s.swap(pending_); if (sink) sink(s); } }
protected:
    int_type overflow(int_type c) override {
        if (c != traits_type::eof()) { pending_ += static_cast<char>(c); if (pending_.size() > 8192) flushNow(); }
        return traits_type::not_eof(c);
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        pending_.append(s, static_cast<size_t>(n));
        if (pending_.size() > 8192) flushNow();
        return n;
    }
    int sync() override { flushNow(); return 0; }
private:
    std::string pending_;
};

} // namespace

struct Runner::Impl {
    RunRequest req;
    RunEvents ev;
    InBuf in;
    OutBuf out;
    std::atomic<bool> cancel{false};
    Runner* owner = nullptr;
#ifdef _WIN32
    HANDLE thread = nullptr;
#else
    pthread_t thread{};
    bool hasThread = false;
#endif

    void body() {
        RunResult res;
        auto t0 = std::chrono::steady_clock::now();
        out.sink = ev.onOutput;
        in.onNeed = ev.onInputRequested;
        try {
            pcs::Program prog = pcs::parseSource(req.source, req.fileName);
            auto diags = pcs::analyze(prog);
            res.diagnostics = diags;
            if (pcs::hasErrors(diags)) { res.status = RunStatus::CompileError; res.exitCode = 1; }
            else {
                pcs::Options opt;
                opt.timeoutMs = req.timeoutMs; opt.maxDepth = req.maxDepth; opt.hasSeed = req.hasSeed; opt.seed = req.seed;
                opt.baseDir = req.baseDir; opt.cancel = &cancel; opt.autoFlush = true;
                std::istream is(&in); std::ostream os(&out);
                try {
                    pcs::Interpreter it(prog, is, os, opt);
                    it.run();
                } catch (...) { os.flush(); throw; }
                os.flush();
            }
        } catch (const pcs::PcsError& e) {
            out.flushNow();
            if (cancel.load()) { res.status = RunStatus::Stopped; res.exitCode = 2; }
            else {
                res.diagnostics.push_back(e.d);
                bool runtime = e.d.category == "Runtime error";
                res.status = runtime ? RunStatus::RuntimeError : RunStatus::CompileError;
                res.exitCode = runtime ? 2 : 1;
            }
        } catch (const std::exception& e) {
            out.flushNow();
            res.status = RunStatus::Internal; res.exitCode = 2; res.message = e.what();
        } catch (...) {
            out.flushNow();
            res.status = RunStatus::Internal; res.exitCode = 2; res.message = "unknown error";
        }
        if (cancel.load() && res.status == RunStatus::Completed) res.status = RunStatus::Stopped;
        res.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        out.flushNow();
        owner->state_.store(2);
        if (ev.onFinished) ev.onFinished(res);
    }

    // The interpreter recurses deeply, so the worker needs a big stack (the same 256 MB the command line tool uses).
    bool launch() {
        const size_t stack = static_cast<size_t>(256) << 20;
#ifdef _WIN32
        thread = CreateThread(nullptr, stack, [](LPVOID p) -> DWORD { static_cast<Impl*>(p)->body(); return 0; }, this, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        return thread != nullptr;
#else
        pthread_attr_t attr; pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, stack);
        int rc = pthread_create(&thread, &attr, [](void* p) -> void* { static_cast<Impl*>(p)->body(); return nullptr; }, this);
        pthread_attr_destroy(&attr);
        hasThread = rc == 0;
        return rc == 0;
#endif
    }
    void join() {
#ifdef _WIN32
        if (thread) { WaitForSingleObject(thread, INFINITE); CloseHandle(thread); thread = nullptr; }
#else
        if (hasThread) { pthread_join(thread, nullptr); hasThread = false; }
#endif
    }
};

Runner::Runner() : d_(new Impl) { d_->owner = this; }
Runner::~Runner() { stop(); wait(); }

bool Runner::start(RunRequest req, RunEvents events) {
    if (state_.load() == 1) return false;
    d_->join();
    d_.reset(new Impl);
    d_->owner = this;
    d_->req = std::move(req); d_->ev = std::move(events);
    state_.store(1);
    if (!d_->launch()) {
        state_.store(0);
        RunResult r; r.status = RunStatus::Internal; r.exitCode = 2; r.message = "could not start the worker thread";
        if (d_->ev.onFinished) d_->ev.onFinished(r);
        return false;
    }
    return true;
}

void Runner::provideInput(const std::string& line) { d_->in.push(line); }

void Runner::endInput() { d_->in.close(); }

void Runner::stop() {
    if (state_.load() != 1) return;
    d_->cancel.store(true);
    d_->in.close();
}

void Runner::wait() {
    d_->join();
    if (state_.load() == 2) state_.store(0);
}

} // namespace studio
