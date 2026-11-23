#pragma once
#include <string>
#include <vector>
#include "common/diagnostics.hpp"

namespace pcs {

enum class Tok { Ident, Keyword, Int, Real, Str, Char, Op, Newline, End };

struct Token {
    Tok t = Tok::End;
    std::string text;   // original spelling (string/char: contents). Operators normalised.
    std::string key;    // lower-case identifier key (identifiers are case-insensitive)
    long long i = 0;
    double r = 0;
    int line = 1, col = 1;
};

class Lexer {
public:
    Lexer(std::string src, std::string file) : src_(std::move(src)), file_(std::move(file)) {}
    std::vector<Token> tokenize();   // throws PcsError (E101)
private:
    std::string src_, file_;
};

bool isKeyword(const std::string& s);

} // namespace pcs
