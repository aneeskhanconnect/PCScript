#pragma once
#include <string>
#include "ast/ast.hpp"

namespace pcs {
// Lexes and parses PCScript source. Throws PcsError (E101 lexical, E102/E103 syntax).
Program parseSource(const std::string& source, const std::string& fileName);
}
