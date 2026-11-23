// Pretty printer for PCScript. Works on unfinished/invalid code too (it never needs a successful parse) and never
// changes what a program means: the result is checked token-by-token against the input and the original text is
// returned unchanged if they would differ.
#pragma once
#include <set>
#include <string>
#include <vector>

namespace studio {

struct FormatOptions {
    int tabSize = 4;
    bool insertSpaces = true;
    bool uppercaseKeywords = true;    // "endif" -> "ENDIF" (only words that cannot be user identifiers)
    bool normalizeSpacing = true;     // spaces around operators, after commas, none inside brackets
    bool trimTrailingWhitespace = true;
    int maxBlankLines = 1;            // runs of blank lines are shortened to this many (-1 = keep all)
    int commentGap = 2;               // spaces before a trailing // comment
    bool finalNewline = true;
};

struct FormatResult {
    std::string text;
    bool changed = false;
    bool ok = true;              // false if the safety check rejected the output (text is then the unmodified input)
    std::string note;
};

FormatResult formatSource(const std::string& source, const FormatOptions& opt = {});
// Formats only; used by tests to compare to the input's token stream.
std::vector<std::string> significantTokens(const std::string& source);

} // namespace studio
