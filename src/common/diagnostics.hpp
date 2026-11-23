#pragma once
#include <stdexcept>
#include <string>
#include <vector>

namespace pcs {

enum class Severity { Error, Warning };

struct Diagnostic {
    Severity sev = Severity::Error;
    std::string code, category, file, message, hint;
    int line = 0, col = 0;
};

class PcsError : public std::runtime_error {
public:
    Diagnostic d;
    explicit PcsError(Diagnostic x) : std::runtime_error(x.message), d(std::move(x)) {}
};

Diagnostic makeDiag(Severity sev, std::string code, std::string category, std::string file,
                    int line, int col, std::string msg, std::string hint = "");

// Human readable block with source excerpt and caret.
std::string formatDiagnostic(const Diagnostic& d, const std::string& source);
// One line: file:line:col: error E102: message   (used by the IDE Problems panel)
std::string formatMachine(const Diagnostic& d);

} // namespace pcs
