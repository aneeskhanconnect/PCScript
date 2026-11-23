#pragma once
#include <vector>
#include "ast/ast.hpp"
#include "common/diagnostics.hpp"

namespace pcs {
// Static analysis: declarations, scopes, routine calls, constants and obvious type errors.
// Returns every error (E2xx) and warning (W5xx) found; the program may run only if there are no errors.
std::vector<Diagnostic> analyze(const Program& prog);
bool hasErrors(const std::vector<Diagnostic>& d);
}
