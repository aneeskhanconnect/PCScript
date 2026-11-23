#include "formatter.hpp"
#include <algorithm>
#include "scan.hpp"
#include "structure.hpp"
#include "symbols.hpp"

namespace studio {

namespace {

using Toks = std::vector<LTok>;

bool isOperand(const LTok& t) {   // a token after which a binary operator may follow
    switch (t.kind) {
        case Tk::Ident: case Tk::Number: case Tk::String: case Tk::Char: case Tk::Constant: case Tk::Type: return true;
        case Tk::Keyword: return t.text == "EOF";
        case Tk::Op: return t.text == ")" || t.text == "]";
        default: return false;
    }
}

bool isSpacedBinary(const std::string& o) {
    return o == "<-" || o == "=" || o == "<>" || o == "<" || o == ">" || o == "<=" || o == ">=" || o == "+" || o == "*" || o == "/" ||
           o == "&" || o == "\xE2\x86\x90" || o == "\xE2\x89\xA4" || o == "\xE2\x89\xA5" || o == "\xE2\x89\xA0";
}

// Rebuilds one line from tokens with canonical spacing. `ind` is the indentation text already chosen.
std::string renderLine(const std::string& original, const Toks& toks, const std::string& ind, const FormatOptions& opt,
                       const std::set<std::string>& userNames) {
    std::string out = ind;
    int bracket = 0;                // depth of [ ] (':' inside is an array-bounds colon: no spaces)
    const LTok* prev = nullptr;
    bool prevUnary = false;         // previous token was a unary '-' or a prefix '^'
    bool prevCaretPostfix = false;
    bool first = true;

    for (size_t i = 0; i < toks.size(); ++i) {
        const LTok& t = toks[i];
        std::string text = t.text;
        if (opt.uppercaseKeywords && t.kind != Tk::Keyword && t.kind != Tk::Constant && t.kind != Tk::Type) {
            // identifiers spelled like keywords in the wrong case
            if (t.kind == Tk::Ident) {
                std::string lw = toLower(text);
                if (isAutoCapitalizable(lw) && !userNames.count(lw)) text = toUpper(text);
            }
        }
        if (opt.uppercaseKeywords && (t.kind == Tk::Type || t.kind == Tk::Constant)) {
            if (!userNames.count(toLower(text))) text = toUpper(text);
        }

        bool space = false;
        bool thisUnary = false, thisCaretPostfix = false;
        if (t.kind == Tk::Comment) {
            space = !first;
        } else if (!opt.normalizeSpacing) {
            space = prev && t.start > prev->end();
        } else if (prev) {
            if (t.kind == Tk::Op) {
                const std::string& o = t.text;
                if (o == "," || o == ")" || o == "]" || o == ".") space = false;
                else if (o == "(") {
                    bool call = prev->kind == Tk::Ident || (prev->kind == Tk::Op && (prev->text == ")" || prev->text == "]")) ||
                                (prev->kind == Tk::Keyword && (prev->text == "EOF" || prev->text == "NEW" || prev->text == "SUPER"));
                    space = !call && !prevUnary && !(prev->kind == Tk::Op && (prev->text == "(" || prev->text == "[" || prev->text == "."));
                } else if (o == "[") {
                    bool idx = prev->kind == Tk::Ident || prev->kind == Tk::Type || (prev->kind == Tk::Keyword && prev->text == "ARRAY") ||
                               (prev->kind == Tk::Op && (prev->text == ")" || prev->text == "]"));
                    space = !idx && !(prev->kind == Tk::Op && (prev->text == "(" || prev->text == "["));
                } else if (o == ":") {
                    space = bracket == 0;
                } else if (o == "^") {
                    if (isOperand(*prev) || prevCaretPostfix) { space = t.start > prev->end(); thisCaretPostfix = true; }
                    else { space = !(prev->kind == Tk::Op && (prev->text == "(" || prev->text == "[")); thisUnary = true; }
                } else if (o == "-") {
                    bool unary = !isOperand(*prev) && !prevCaretPostfix;
                    if (unary) { thisUnary = true; space = !(prev->kind == Tk::Op && (prev->text == "(" || prev->text == "[") ) && !prevUnary; }
                    else space = true;
                } else if (isSpacedBinary(o)) space = true;
                else space = t.start > prev->end();
            } else {
                // a word/number/string after something
                if (prev->kind == Tk::Op) {
                    const std::string& po = prev->text;
                    if (po == "(" || po == "[" || po == ".") space = false;
                    else if (po == ":") space = bracket == 0;
                    else if (prevUnary) space = false;
                    else if (po == "^" && prevCaretPostfix) space = t.start > prev->end();
                    else space = true;
                } else space = true;
            }
        }
        if (t.kind == Tk::Op) {
            if (t.text == "[") ++bracket;
            else if (t.text == "]" && bracket > 0) --bracket;
        }
        if (t.kind == Tk::Comment) {
            if (!first) out.append(static_cast<size_t>(std::max(1, opt.commentGap)), ' ');
            std::string c = text;
            while (!c.empty() && (c.back() == ' ' || c.back() == '\t' || c.back() == '\r')) c.pop_back();
            out += c;
        } else {
            if (space) out += ' ';
            out += text;
        }
        first = false;
        prev = &t; prevUnary = thisUnary; prevCaretPostfix = thisCaretPostfix;
    }
    (void)original;
    return out;
}

std::string indentText(int level, const FormatOptions& opt) {
    if (level < 0) level = 0;
    if (opt.insertSpaces) return std::string(static_cast<size_t>(level * std::max(1, opt.tabSize)), ' ');
    return std::string(static_cast<size_t>(level), '\t');
}

} // namespace

std::vector<std::string> significantTokens(const std::string& source) {
    // One entry per non-empty line: tokens joined by '\x1f' (keyword case ignored). Comments are ignored.
    std::vector<std::string> out;
    for (const std::string& line : splitLines(source)) {
        std::string joined;
        bool any = false;
        for (const LTok& t : scanLine(line)) {
            if (t.kind == Tk::Comment) continue;
            if (any) joined += '\x1f';
            any = true;
            joined += (t.kind == Tk::String || t.kind == Tk::Char || t.kind == Tk::Number || t.kind == Tk::Op) ? t.text : toLower(t.text);
        }
        if (any) out.push_back(joined);
    }
    return out;
}

FormatResult formatSource(const std::string& source, const FormatOptions& opt) {
    FormatResult r;
    std::vector<std::string> lines = splitLines(source);
    SymbolIndex idx = SymbolIndex::build(lines);
    std::set<std::string> names = idx.userNamesLower();

    BlockTracker bt;
    std::vector<std::string> out;
    int blanks = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        Toks toks = scanLine(lines[i]);
        LineResult lr = bt.feed(toks, static_cast<int>(i));
        if (lr.cls == LineClass::Blank) {
            if (out.empty()) continue;                       // no leading blank lines
            if (opt.maxBlankLines >= 0 && blanks >= opt.maxBlankLines) continue;
            ++blanks; out.push_back("");
            continue;
        }
        blanks = 0;
        std::string text = renderLine(lines[i], toks, indentText(lr.indent, opt), opt, names);
        if (!opt.trimTrailingWhitespace) {
            // keep any trailing spaces the user had after the last token (rarely wanted)
            size_t end = lines[i].find_last_not_of(" \t");
            if (end != std::string::npos) text += lines[i].substr(end + 1);
        }
        out.push_back(std::move(text));
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    std::string joined = joinLines(out, "\n");
    if (opt.finalNewline && !out.empty()) joined += "\n";

    // Normalise the comparison baseline: the input may use CRLF and have no final newline.
    std::string normalizedSource = joinLines(lines, "\n");
    r.text = joined;
    if (significantTokens(joined) != significantTokens(normalizedSource)) {
        r.text = source; r.ok = false; r.changed = false;
        r.note = "The formatter's safety check failed, so the text was left unchanged.";
        return r;
    }
    // "changed" compares against the source with line endings normalised
    std::string cmp = normalizedSource;
    r.changed = r.text != cmp;
    return r;
}

} // namespace studio
