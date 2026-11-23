#pragma once
#include <atomic>
#include <iosfwd>
#include <memory>
#include "ast/ast.hpp"
#include "common/diagnostics.hpp"

namespace pcs {

struct Options {
    bool trace = false;          // write "[trace] line N" to traceOut before each statement
    std::ostream* traceOut = nullptr;
    long long timeoutMs = 0;     // 0 = unlimited
    bool hasSeed = false;
    unsigned long long seed = 0;
    int maxDepth = 5000;         // PCScript call depth limit
    bool autoFlush = true;       // flush output after every OUTPUT (interactive use)
    std::string baseDir;         // directory used to resolve relative file names
    const std::atomic<bool>* cancel = nullptr;   // when set to true the run stops with R318 (used by the IDE Stop button)
};

// Tree-walking interpreter. run() throws PcsError (R3xx) on runtime errors.
class Interpreter {
public:
    Interpreter(const Program& prog, std::istream& in, std::ostream& out, Options opt = {});
    ~Interpreter();
    void run();
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace pcs
