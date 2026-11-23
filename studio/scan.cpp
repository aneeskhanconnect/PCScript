#include "scan.hpp"
#include <cctype>
#include "lexer/lexer.hpp"
#include "stdlib/builtins.hpp"

namespace studio {

std::string toUpper(std::string s) { for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
std::string toLower(std::string s) { for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back(); out.push_back(std::move(cur)); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
    out.push_back(std::move(cur));
    return out;
}

std::string joinLines(const std::vector<std::string>& lines, const std::string& eol) {
    std::string o;
    for (size_t i = 0; i < lines.size(); ++i) { if (i) o += eol; o += lines[i]; }
    return o;
}

bool isTypeWord(const std::string& w) {
    return w == "INTEGER" || w == "REAL" || w == "STRING" || w == "CHAR" || w == "BOOLEAN" || w == "DATE";
}

bool isAutoCapitalizable(const std::string& w) {
    static const char* const k[] = {
        "declare", "constant", "input", "output", "if", "then", "else", "endif", "case", "otherwise", "endcase", "for", "next",
        "while", "do", "endwhile", "repeat", "until", "procedure", "endprocedure", "function", "endfunction", "returns", "return",
        "call", "byval", "byref", "type", "endtype", "class", "endclass", "public", "private", "inherits", "openfile",
        "closefile", "readfile", "writefile", "seek", "getrecord", "putrecord", "array", "and", "or", "not", "mod", "div",
        "true", "false", "to", "of"};
    for (const char* x : k) if (w == x) return true;
    return false;
}

void promoteKeywords(std::vector<LTok>& t) {
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i].kind != Tk::Ident) continue;
        std::string lw = toLower(t[i].text);
        if (!isAutoCapitalizable(lw)) continue;
        if (i > 0 && t[i - 1].kind == Tk::Op && t[i - 1].text == ".") continue;
        // the word right after DECLARE / PROCEDURE / CALL ... is a name the person chose
        if (i > 0 && t[i - 1].kind == Tk::Keyword) {
            const std::string& p = t[i - 1].text;
            if (p == "DECLARE" || p == "CONSTANT" || p == "PROCEDURE" || p == "FUNCTION" || p == "CLASS" || p == "TYPE" ||
                p == "INHERITS" || p == "CALL" || p == "FOR" || p == "BYREF" || p == "BYVAL") continue;
            if ((p == "PUBLIC" || p == "PRIVATE") && lw != "procedure" && lw != "function") continue;   // a field name
        }
        if (i + 1 < t.size() && t[i + 1].kind == Tk::Op) {
            const std::string& o = t[i + 1].text;
            if (o == "<-" || o == "\xE2\x86\x90" || o == "." || o == "[" || o == "=") continue;
            if ((o == ":" || o == ",") && lw != "otherwise") continue;   // a name in DECLARE a, b : T / a parameter / a field
        }
        t[i].text = toUpper(t[i].text);
        t[i].kind = (t[i].text == "TRUE" || t[i].text == "FALSE") ? Tk::Constant : Tk::Keyword;
    }
}

std::vector<LTok> scanLine(const std::string& s) {
    std::vector<LTok> out;
    const size_t n = s.size();
    size_t p = 0;
    auto push = [&](Tk k, size_t a, size_t b, bool term = true) {
        LTok t; t.kind = k; t.start = static_cast<int>(a); t.len = static_cast<int>(b - a);
        t.text = s.substr(a, b - a); t.terminated = term; out.push_back(std::move(t));
    };
    while (p < n) {
        unsigned char c = static_cast<unsigned char>(s[p]);
        if (c == ' ' || c == '\t' || c == '\r') { ++p; continue; }
        if (c == '/' && p + 1 < n && s[p + 1] == '/') { push(Tk::Comment, p, n); break; }
        if (std::isalpha(c) || c == '_') {
            size_t a = p;
            while (p < n && isWordChar(s[p])) ++p;
            std::string w = s.substr(a, p - a);
            Tk k = Tk::Ident;
            if (pcs::isKeyword(w)) k = (w == "TRUE" || w == "FALSE") ? Tk::Constant : (isTypeWord(w) ? Tk::Type : Tk::Keyword);
            else if (isTypeWord(toUpper(w))) k = Tk::Type;   // the language accepts lower-case type names
            push(k, a, p);
            continue;
        }
        if (std::isdigit(c)) {
            size_t a = p;
            while (p < n && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
            if (p + 1 < n && s[p] == '.' && std::isdigit(static_cast<unsigned char>(s[p + 1]))) {
                ++p; while (p < n && std::isdigit(static_cast<unsigned char>(s[p]))) ++p;
            }
            push(Tk::Number, a, p);
            continue;
        }
        if (c == '"' || c == '\'') {
            size_t a = p++;
            while (p < n && s[p] != static_cast<char>(c)) ++p;
            bool term = p < n;
            if (term) ++p;
            push(c == '"' ? Tk::String : Tk::Char, a, p, term);
            continue;
        }
        if (c == 0xE2 && p + 2 < n) {   // ← ≤ ≥ ≠
            unsigned char b1 = static_cast<unsigned char>(s[p + 1]), b2 = static_cast<unsigned char>(s[p + 2]);
            if ((b1 == 0x86 && b2 == 0x90) || (b1 == 0x89 && (b2 == 0xA4 || b2 == 0xA5 || b2 == 0xA0))) { push(Tk::Op, p, p + 3); p += 3; continue; }
        }
        if (p + 1 < n) {
            std::string two = s.substr(p, 2);
            if (two == "<-" || two == "<=" || two == ">=" || two == "<>") { push(Tk::Op, p, p + 2); p += 2; continue; }
        }
        if (std::string("=<>+-*/^()[],:.&").find(static_cast<char>(c)) != std::string::npos) { push(Tk::Op, p, p + 1); ++p; continue; }
        size_t a = p++;
        while (p < n && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80) ++p;   // keep UTF-8 sequences whole
        push(Tk::Unknown, a, p);
    }
    return out;
}

std::vector<Span> highlightLine(const std::string& line) {
    std::vector<Span> out;
    std::vector<LTok> toks = scanLine(line);
    for (size_t i = 0; i < toks.size(); ++i) {
        const LTok& t = toks[i];
        Hl h = Hl::Plain;
        switch (t.kind) {
            case Tk::Keyword: h = Hl::Keyword; break;
            case Tk::Type: h = Hl::Type; break;
            case Tk::Constant: h = Hl::Constant; break;
            case Tk::Number: h = Hl::Number; break;
            case Tk::String: case Tk::Char: h = t.terminated ? Hl::String : Hl::Error; break;
            case Tk::Comment: h = Hl::Comment; break;
            case Tk::Op: h = Hl::Operator; break;
            case Tk::Unknown: h = Hl::Error; break;
            case Tk::Ident:
                if (i + 1 < toks.size() && toks[i + 1].kind == Tk::Op && toks[i + 1].text == "(" && pcs::findBuiltin(toLower(t.text)))
                    h = Hl::Builtin;
                break;
        }
        out.push_back({t.start, t.len, h});
    }
    return out;
}

} // namespace studio
