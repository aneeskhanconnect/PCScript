#include "lexer/lexer.hpp"
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <unordered_set>

namespace pcs {

static const std::unordered_set<std::string> kKeywords = {
    "DECLARE","CONSTANT","INPUT","OUTPUT","IF","THEN","ELSE","ENDIF","CASE","OF","OTHERWISE","ENDCASE",
    "FOR","TO","STEP","NEXT","WHILE","DO","ENDWHILE","REPEAT","UNTIL","PROCEDURE","ENDPROCEDURE",
    "FUNCTION","ENDFUNCTION","RETURNS","RETURN","CALL","BYVAL","BYREF","TYPE","ENDTYPE","CLASS",
    "ENDCLASS","PUBLIC","PRIVATE","INHERITS","NEW","SUPER","OPENFILE","CLOSEFILE","READFILE",
    "WRITEFILE","SEEK","GETRECORD","PUTRECORD","EOF","READ","WRITE","APPEND","RANDOM","ARRAY",
    "INTEGER","REAL","STRING","CHAR","BOOLEAN","DATE","AND","OR","NOT","MOD","DIV","TRUE","FALSE"};

bool isKeyword(const std::string& s) { return kKeywords.count(s) != 0; }

static std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    const size_t n = src_.size();
    size_t p = 0, lineStart = 0;
    int line = 1;
    auto col = [&](size_t q) { return static_cast<int>(q - lineStart) + 1; };
    auto fail = [&](size_t q, const std::string& m, const std::string& hint = "") {
        throw PcsError(makeDiag(Severity::Error, "E101", "Lexical error", file_, line, col(q), m, hint));
    };
    auto push = [&](Tok t, std::string text, size_t q) -> Token& {
        Token tk; tk.t = t; tk.text = std::move(text); tk.line = line; tk.col = col(q);
        out.push_back(std::move(tk));
        return out.back();
    };
    if (n >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF && static_cast<unsigned char>(src_[1]) == 0xBB &&
        static_cast<unsigned char>(src_[2]) == 0xBF) { p = 3; lineStart = 3; }   // UTF-8 BOM

    while (p < n) {
        unsigned char c = static_cast<unsigned char>(src_[p]);
        if (c == '\n') { push(Tok::Newline, "\n", p); ++p; ++line; lineStart = p; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { ++p; continue; }
        if (c == '/' && p + 1 < n && src_[p + 1] == '/') { while (p < n && src_[p] != '\n') ++p; continue; }

        if (std::isalpha(c) || c == '_') {
            size_t s = p;
            while (p < n && (std::isalnum(static_cast<unsigned char>(src_[p])) || src_[p] == '_')) ++p;
            std::string w = src_.substr(s, p - s);
            if (isKeyword(w)) { push(Tok::Keyword, w, s); }
            else { Token& t = push(Tok::Ident, w, s); t.key = lower(w); }
            continue;
        }
        if (std::isdigit(c)) {
            size_t s = p; bool real = false;
            while (p < n && std::isdigit(static_cast<unsigned char>(src_[p]))) ++p;
            if (p + 1 < n && src_[p] == '.' && std::isdigit(static_cast<unsigned char>(src_[p + 1]))) {
                real = true; ++p;
                while (p < n && std::isdigit(static_cast<unsigned char>(src_[p]))) ++p;
            }
            std::string w = src_.substr(s, p - s);
            if (real) { Token& t = push(Tok::Real, w, s); t.r = std::strtod(w.c_str(), nullptr); }
            else {
                errno = 0;
                long long v = std::strtoll(w.c_str(), nullptr, 10);
                if (errno == ERANGE) fail(s, "Integer literal is too large: " + w);
                Token& t = push(Tok::Int, w, s); t.i = v;
            }
            continue;
        }
        if (c == '"') {
            size_t s = p++; std::string v;
            while (p < n && src_[p] != '"' && src_[p] != '\n') v += src_[p++];
            if (p >= n || src_[p] != '"')
                fail(s, "Unterminated string literal", "Add a closing double quote (\") at the end of the string.");
            ++p; push(Tok::Str, v, s);
            continue;
        }
        if (c == '\'') {
            size_t s = p++; std::string v;
            while (p < n && src_[p] != '\'' && src_[p] != '\n') v += src_[p++];
            if (p >= n || src_[p] != '\'') fail(s, "Unterminated character literal", "Add a closing single quote (').");
            ++p;
            if (v.size() != 1)
                fail(s, "A CHAR literal must contain exactly one character", "Use double quotes for text, e.g. \"" + v + "\".");
            push(Tok::Char, v, s);
            continue;
        }
        if (c == 0xE2 && p + 2 < n) {   // UTF-8 operator glyphs
            unsigned char b1 = static_cast<unsigned char>(src_[p + 1]), b2 = static_cast<unsigned char>(src_[p + 2]);
            const char* o = nullptr;
            if (b1 == 0x86 && b2 == 0x90) o = "<-";
            else if (b1 == 0x89 && b2 == 0xA4) o = "<=";
            else if (b1 == 0x89 && b2 == 0xA5) o = ">=";
            else if (b1 == 0x89 && b2 == 0xA0) o = "<>";
            if (o) { push(Tok::Op, o, p); p += 3; continue; }
        }
        if (p + 1 < n) {
            std::string two = src_.substr(p, 2);
            if (two == "<-" || two == "<=" || two == ">=" || two == "<>") { push(Tok::Op, two, p); p += 2; continue; }
        }
        if (std::string("=<>+-*/^()[],:.&").find(static_cast<char>(c)) != std::string::npos) {
            push(Tok::Op, std::string(1, static_cast<char>(c)), p); ++p; continue;
        }
        fail(p, std::string("Unexpected character '") + static_cast<char>(c) + "'");
    }
    push(Tok::Newline, "\n", p);
    push(Tok::End, "", p);
    return out;
}

} // namespace pcs
