// Error-tolerant, per-line scanner used for syntax highlighting, formatting, auto-indent and autocomplete.
// The engine's own lexer throws on the first error and drops comments, which is no good while a person is typing, so
// the IDE has this separate scanner. It follows the same lexical rules (see src/lexer/lexer.cpp).
#pragma once
#include <string>
#include <vector>

namespace studio {

enum class Tk { Ident, Keyword, Type, Constant, Number, String, Char, Comment, Op, Unknown };

struct LTok {
    Tk kind = Tk::Unknown;
    int start = 0, len = 0;      // byte offsets inside the line
    std::string text;            // raw text (for Str/Char it includes the quotes)
    bool terminated = true;      // false for a string/char literal missing its closing quote
    int end() const { return start + len; }
};

std::vector<LTok> scanLine(const std::string& line);

// Highlight classes for the editor (a Tk::Ident followed by '(' that names a built-in function becomes Builtin).
enum class Hl { Plain, Keyword, Type, Builtin, String, Number, Comment, Operator, Constant, Error };
struct Span { int start, len; Hl kind; };
std::vector<Span> highlightLine(const std::string& line);

// Helpers shared by the modules.
std::string toUpper(std::string s);
std::string toLower(std::string s);
bool isWordChar(char c);   // letters, digits, underscore
std::vector<std::string> splitLines(const std::string& text);   // splits on \n, strips a trailing \r from each line
std::string joinLines(const std::vector<std::string>& lines, const std::string& eol = "\n");
bool isTypeWord(const std::string& upperWord);   // INTEGER REAL STRING CHAR BOOLEAN DATE

// Keywords that are safe to upper-case when typed in lower case ("if", "endif", "then" ...). Words such as step, read,
// write, random, date, new can legitimately be variable names in PCScript, so they are not included.
bool isAutoCapitalizable(const std::string& lowerWord);
// For structure analysis while someone is still typing in lower case: turns identifiers that are spelled like keywords
// ("if", "endif", "then") into keyword tokens with upper-case text, unless they are clearly used as a name
// (after a '.', or followed by <-, '.', '[' or '=').
void promoteKeywords(std::vector<LTok>& toks);

} // namespace studio
