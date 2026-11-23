#include "common/diagnostics.hpp"
#include <sstream>

namespace pcs {

Diagnostic makeDiag(Severity sev, std::string code, std::string category, std::string file,
                    int line, int col, std::string msg, std::string hint) {
    Diagnostic d;
    d.sev = sev; d.code = std::move(code); d.category = std::move(category);
    d.file = std::move(file); d.line = line; d.col = col;
    d.message = std::move(msg); d.hint = std::move(hint);
    return d;
}

static std::string sourceLine(const std::string& src, int line) {
    int cur = 1; size_t start = 0;
    while (cur < line) {
        size_t nl = src.find('\n', start);
        if (nl == std::string::npos) return "";
        start = nl + 1; ++cur;
    }
    size_t end = src.find('\n', start);
    std::string l = src.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!l.empty() && l.back() == '\r') l.pop_back();
    return l;
}

std::string formatDiagnostic(const Diagnostic& d, const std::string& source) {
    std::ostringstream o;
    o << "PCScript " << (d.sev == Severity::Error ? "Error " : "Warning ") << d.code
      << " (" << d.category << ")\n";
    o << "File: " << d.file << "\nLine: " << d.line << "\nColumn: " << d.col << "\n";
    if (d.line > 0) {
        std::string l = sourceLine(source, d.line);
        if (!l.empty()) {
            std::string num = std::to_string(d.line);
            o << "\n  " << num << " | " << l << "\n  " << std::string(num.size(), ' ') << " | "
              << std::string(d.col > 1 ? static_cast<size_t>(d.col - 1) : 0, ' ') << "^\n";
        }
    }
    o << "\n" << d.message << "\n";
    if (!d.hint.empty()) o << "Suggestion: " << d.hint << "\n";
    return o.str();
}

std::string formatMachine(const Diagnostic& d) {
    std::string m = d.message;
    for (char& c : m) if (c == '\n') c = ' ';
    return d.file + ":" + std::to_string(d.line) + ":" + std::to_string(d.col) + ": " +
           (d.sev == Severity::Error ? "error " : "warning ") + d.code + ": " + m;
}

} // namespace pcs
