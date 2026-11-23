#include <cctype>
#include "document.hpp"
#include <algorithm>
#include <cstring>
#include "scan.hpp"
#include "structure.hpp"
#include "symbols.hpp"

namespace studio {

namespace {
bool isBlank(const std::string& s) { return s.find_first_not_of(" \t") == std::string::npos; }
std::string ltrim(const std::string& s) { size_t i = s.find_first_not_of(" \t"); return i == std::string::npos ? "" : s.substr(i); }
int charClass(char c) {
    if (c == ' ' || c == '\t') return 0;
    if (isWordChar(c) || (static_cast<unsigned char>(c) >= 0x80)) return 1;
    return 2;
}
} // namespace

Document::Document() = default;

// ---------------------------------------------------------------- text
void Document::setText(const std::string& utf8) {
    eol_ = utf8.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string t = utf8;
    if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF && static_cast<unsigned char>(t[1]) == 0xBB && static_cast<unsigned char>(t[2]) == 0xBF) t.erase(0, 3);
    lines_ = splitLines(t);
    undo_.clear(); redo_.clear();
    caret_ = anchor_ = Pos{};
    desiredCol_ = -1; depth_ = 0; noCoalesce_ = true;
    ++version_;
    savedVersion_ = version_;
}

std::string Document::text() const { return joinLines(lines_, eol_); }
std::string Document::indentUnit() const { return opt_.insertSpaces ? std::string(static_cast<size_t>(std::max(1, opt_.tabSize)), ' ') : std::string("\t"); }
std::string Document::indentString(int level) const {
    std::string o, u = indentUnit();
    for (int i = 0; i < level; ++i) o += u;
    return o;
}
int Document::leadingWhitespaceBytes(int line) const {
    const std::string& s = lines_[static_cast<size_t>(line)];
    size_t i = s.find_first_not_of(" \t");
    return static_cast<int>(i == std::string::npos ? s.size() : i);
}

Pos Document::clamp(Pos p) const {
    p.line = std::max(0, std::min(p.line, lineCount() - 1));
    int len = static_cast<int>(lines_[static_cast<size_t>(p.line)].size());
    p.col = std::max(0, std::min(p.col, len));
    while (p.col > 0 && p.col < len && (static_cast<unsigned char>(lines_[static_cast<size_t>(p.line)][static_cast<size_t>(p.col)]) & 0xC0) == 0x80) --p.col;
    return p;
}

int Document::charLen(const std::string& s, int col) {
    if (col < 0 || col >= static_cast<int>(s.size())) return 0;
    unsigned char c = static_cast<unsigned char>(s[static_cast<size_t>(col)]);
    int n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
    return std::min(n, static_cast<int>(s.size()) - col);
}
int Document::prevCharStart(const std::string& s, int col) {
    if (col <= 0) return 0;
    --col;
    while (col > 0 && (static_cast<unsigned char>(s[static_cast<size_t>(col)]) & 0xC0) == 0x80) --col;
    return col;
}

// ---------------------------------------------------------------- undo machinery
std::string Document::textBetween(Pos a, Pos b) const {
    if (a.line == b.line) return lines_[static_cast<size_t>(a.line)].substr(static_cast<size_t>(a.col), static_cast<size_t>(b.col - a.col));
    std::string o = lines_[static_cast<size_t>(a.line)].substr(static_cast<size_t>(a.col));
    for (int i = a.line + 1; i < b.line; ++i) { o += '\n'; o += lines_[static_cast<size_t>(i)]; }
    o += '\n';
    o += lines_[static_cast<size_t>(b.line)].substr(0, static_cast<size_t>(b.col));
    return o;
}

Pos Document::advance(Pos start, const std::string& s) {
    size_t nl = s.rfind('\n');
    if (nl == std::string::npos) return {start.line, start.col + static_cast<int>(s.size())};
    int count = static_cast<int>(std::count(s.begin(), s.end(), '\n'));
    return {start.line + count, static_cast<int>(s.size() - nl - 1)};
}

static void applyToLines(std::vector<std::string>& L, Pos a, Pos b, const std::string& ins) {
    std::string head = L[static_cast<size_t>(a.line)].substr(0, static_cast<size_t>(a.col));
    std::string tail = L[static_cast<size_t>(b.line)].substr(static_cast<size_t>(b.col));
    std::vector<std::string> parts;
    size_t p = 0;
    for (;;) {
        size_t nl = ins.find('\n', p);
        if (nl == std::string::npos) { parts.push_back(ins.substr(p)); break; }
        parts.push_back(ins.substr(p, nl - p)); p = nl + 1;
    }
    parts.front() = head + parts.front();
    parts.back() += tail;
    L.erase(L.begin() + a.line, L.begin() + b.line + 1);
    L.insert(L.begin() + a.line, parts.begin(), parts.end());
}

void Document::beginStep(bool typing) {
    if (depth_++ > 0) return;
    bool coalesce = typing && !noCoalesce_ && !undo_.empty() && undo_.back().typing && undo_.back().caretAfter == caret_ &&
                    undo_.back().anchorAfter == anchor_ && !hasSelection();
    if (!coalesce) {
        Step s; s.caretBefore = caret_; s.anchorBefore = anchor_; s.typing = typing;
        undo_.push_back(std::move(s));
    }
    redo_.clear();
}

void Document::endStep() {
    if (--depth_ > 0) return;
    caret_ = clamp(caret_); anchor_ = clamp(anchor_);   // whatever a command did, the selection must stay inside the text
    if (undo_.empty()) return;
    Step& s = undo_.back();
    if (s.edits.empty()) { undo_.pop_back(); return; }
    s.caretAfter = caret_; s.anchorAfter = anchor_;
    if (undo_.size() > 2000) undo_.erase(undo_.begin(), undo_.begin() + 500);
}

Pos Document::rawReplace(Pos a, Pos b, const std::string& ins) {
    if (b < a) std::swap(a, b);
    bool own = depth_ == 0;
    if (own) beginStep(false);
    Edit e; e.at = a; e.removed = textBetween(a, b); e.inserted = ins;
    applyToLines(lines_, a, b, ins);
    undo_.back().edits.push_back(std::move(e));
    ++version_;
    Pos end = advance(a, ins);
    if (own) endStep();
    return end;
}

bool Document::undo() {
    if (undo_.empty()) return false;
    Step s = std::move(undo_.back()); undo_.pop_back();
    for (size_t i = s.edits.size(); i-- > 0;) {
        const Edit& e = s.edits[i];
        applyToLines(lines_, e.at, advance(e.at, e.inserted), e.removed);
    }
    caret_ = clamp(s.caretBefore); anchor_ = clamp(s.anchorBefore);
    redo_.push_back(std::move(s));
    ++version_; resetDesired();
    return true;
}

bool Document::redo() {
    if (redo_.empty()) return false;
    Step s = std::move(redo_.back()); redo_.pop_back();
    for (const Edit& e : s.edits) applyToLines(lines_, e.at, advance(e.at, e.removed), e.inserted);
    caret_ = clamp(s.caretAfter); anchor_ = clamp(s.anchorAfter);
    undo_.push_back(std::move(s));
    ++version_; resetDesired();
    return true;
}

// ---------------------------------------------------------------- caret
void Document::setCaret(Pos p, bool extend) {
    caret_ = clamp(p);
    if (!extend) anchor_ = caret_;
    resetDesired();
}

void Document::moveLeft(bool extend, bool byWord) {
    if (hasSelection() && !extend) { setCaret(selStart(), false); return; }
    Pos p = caret_;
    if (p.col == 0) { if (p.line > 0) { p.line--; p.col = static_cast<int>(lines_[static_cast<size_t>(p.line)].size()); } }
    else if (!byWord) p.col = prevCharStart(lines_[static_cast<size_t>(p.line)], p.col);
    else {
        const std::string& s = lines_[static_cast<size_t>(p.line)];
        while (p.col > 0 && charClass(s[static_cast<size_t>(p.col - 1)]) == 0) --p.col;
        if (p.col > 0) { int cl = charClass(s[static_cast<size_t>(p.col - 1)]); while (p.col > 0 && charClass(s[static_cast<size_t>(p.col - 1)]) == cl) --p.col; }
    }
    setCaret(p, extend);
}

void Document::moveRight(bool extend, bool byWord) {
    if (hasSelection() && !extend) { setCaret(selEnd(), false); return; }
    Pos p = caret_;
    const std::string& s = lines_[static_cast<size_t>(p.line)];
    if (p.col >= static_cast<int>(s.size())) { if (p.line + 1 < lineCount()) { p.line++; p.col = 0; } }
    else if (!byWord) p.col += charLen(s, p.col);
    else {
        int n = static_cast<int>(s.size());
        int cl = charClass(s[static_cast<size_t>(p.col)]);
        if (cl != 0) while (p.col < n && charClass(s[static_cast<size_t>(p.col)]) == cl) ++p.col;
        while (p.col < n && charClass(s[static_cast<size_t>(p.col)]) == 0) ++p.col;
    }
    setCaret(p, extend);
}

void Document::moveUp(bool extend, int n) {
    int want = desiredCol_ >= 0 ? desiredCol_ : displayColumn(caret_.line, caret_.col);
    Pos p = caret_;
    if (p.line - n < 0) { p.line = 0; p.col = 0; setCaret(p, extend); return; }
    p.line -= n; p.col = colFromDisplay(p.line, want);
    caret_ = clamp(p); if (!extend) anchor_ = caret_;
    desiredCol_ = want; noCoalesce_ = true;
}

void Document::moveDown(bool extend, int n) {
    int want = desiredCol_ >= 0 ? desiredCol_ : displayColumn(caret_.line, caret_.col);
    Pos p = caret_;
    if (p.line + n >= lineCount()) { p.line = lineCount() - 1; p.col = static_cast<int>(lines_.back().size()); setCaret(p, extend); return; }
    p.line += n; p.col = colFromDisplay(p.line, want);
    caret_ = clamp(p); if (!extend) anchor_ = caret_;
    desiredCol_ = want; noCoalesce_ = true;
}

void Document::home(bool extend) {
    int first = leadingWhitespaceBytes(caret_.line);
    int col = (caret_.col == first || (caret_.col < first && caret_.col != 0)) ? 0 : first;
    if (caret_.col == 0) col = first;
    setCaret({caret_.line, col}, extend);
}
void Document::end(bool extend) { setCaret({caret_.line, static_cast<int>(lines_[static_cast<size_t>(caret_.line)].size())}, extend); }
void Document::docStart(bool extend) { setCaret({0, 0}, extend); }
void Document::docEnd(bool extend) { setCaret({lineCount() - 1, static_cast<int>(lines_.back().size())}, extend); }
void Document::selectAll() { anchor_ = {0, 0}; caret_ = {lineCount() - 1, static_cast<int>(lines_.back().size())}; resetDesired(); }

std::string Document::wordAt(Pos p, Pos* start) const {
    p = clamp(p);
    const std::string& s = lines_[static_cast<size_t>(p.line)];
    int a = p.col, b = p.col;
    while (a > 0 && isWordChar(s[static_cast<size_t>(a - 1)])) --a;
    while (b < static_cast<int>(s.size()) && isWordChar(s[static_cast<size_t>(b)])) ++b;
    if (start) *start = {p.line, a};
    return s.substr(static_cast<size_t>(a), static_cast<size_t>(b - a));
}

void Document::selectWordAt(Pos p) {
    p = clamp(p);
    const std::string& s = lines_[static_cast<size_t>(p.line)];
    int n = static_cast<int>(s.size());
    if (n == 0) { setCaret(p, false); return; }
    int c = std::min(p.col, n - 1);
    if (p.col >= n || charClass(s[static_cast<size_t>(c)]) != 1) { if (p.col > 0 && charClass(s[static_cast<size_t>(p.col - 1)]) == 1) c = p.col - 1; }
    int cl = charClass(s[static_cast<size_t>(c)]);
    int a = c, b = c + 1;
    while (a > 0 && charClass(s[static_cast<size_t>(a - 1)]) == cl) --a;
    while (b < n && charClass(s[static_cast<size_t>(b)]) == cl) ++b;
    anchor_ = {p.line, a}; caret_ = {p.line, b}; resetDesired();
}

void Document::selectLineAt(int line) {
    line = std::max(0, std::min(line, lineCount() - 1));
    anchor_ = {line, 0};
    caret_ = line + 1 < lineCount() ? Pos{line + 1, 0} : Pos{line, static_cast<int>(lines_[static_cast<size_t>(line)].size())};
    resetDesired();
}

std::string Document::selectedText() const { return hasSelection() ? textBetween(selStart(), selEnd()) : std::string(); }

// ---------------------------------------------------------------- basic edits
Pos Document::deleteSelection() {
    if (!hasSelection()) return caret_;
    Pos a = selStart(), b = selEnd();
    rawReplace(a, b, "");
    setSel(a, a);
    return a;
}

void Document::insertText(const std::string& in) {
    std::string s;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\r') { if (i + 1 < in.size() && in[i + 1] == '\n') ++i; s += '\n'; }
        else s += in[i];
    }
    if (s.empty() && !hasSelection()) return;
    Guard g(*this);
    deleteSelection();
    Pos e = rawReplace(caret_, caret_, s);
    setSel(e, e);
    resetDesired();
}

std::string Document::cut() {
    std::string t = selectedText();
    if (t.empty()) return t;
    Guard g(*this);
    deleteSelection();
    resetDesired();
    return t;
}

bool Document::inStringOrComment(Pos p) const {
    p = clamp(p);
    for (const LTok& t : scanLine(lines_[static_cast<size_t>(p.line)])) {
        if (t.kind == Tk::Comment && p.col >= t.start) return true;
        if ((t.kind == Tk::String || t.kind == Tk::Char) && p.col > t.start && (p.col < t.end() || !t.terminated)) return true;
    }
    return false;
}

bool Document::capitalizeWordBeforeCaret(char nextChar) {
    const std::string ln = lines_[static_cast<size_t>(caret_.line)];
    int end = caret_.col, start = end;
    while (start > 0 && isWordChar(ln[static_cast<size_t>(start - 1)])) --start;
    if (start == end) return false;
    std::string w = ln.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)), lw = toLower(w);
    if (w == toUpper(w)) return false;
    if (!(isAutoCapitalizable(lw) || (lw == "eof" && nextChar == '('))) return false;
    if (start > 0 && ln[static_cast<size_t>(start - 1)] == '.') return false;

    std::vector<LTok> pre = scanLine(ln.substr(0, static_cast<size_t>(start)));
    if (!pre.empty()) {
        const LTok& last = pre.back();
        if (last.kind == Tk::Comment) return false;
        if ((last.kind == Tk::String || last.kind == Tk::Char) && !last.terminated) return false;
        promoteKeywords(pre);
        auto kw = [&](size_t i, const char* s) { return i < pre.size() && pre[i].kind == Tk::Keyword && pre[i].text == s; };
        const LTok& p = pre.back();
        if (p.kind == Tk::Keyword && (p.text == "DECLARE" || p.text == "CONSTANT" || p.text == "PROCEDURE" || p.text == "FUNCTION" ||
                                      p.text == "CLASS" || p.text == "TYPE" || p.text == "INHERITS" || p.text == "NEW" || p.text == "CALL" ||
                                      p.text == "FOR" || p.text == "BYREF" || p.text == "BYVAL")) {
            return false;   // the word after these is a name the user is choosing
        }
        size_t f = (kw(0, "PUBLIC") || kw(0, "PRIVATE")) ? 1 : 0;
        bool hasColon = false;
        for (const LTok& t : pre) if (t.kind == Tk::Op && t.text == ":") hasColon = true;
        if (f == 1 && pre.size() == 1 && lw != "procedure" && lw != "function") return false;   // PRIVATE <field name>
        if ((kw(f, "DECLARE") || kw(f, "CONSTANT") || kw(f, "TYPE") || kw(f, "CLASS")) && !hasColon && lw != "array" && lw != "of") return false;
        if ((kw(f, "PROCEDURE") || kw(f, "FUNCTION")) && lw != "returns" && lw != "array" && lw != "of" && lw != "byref" && lw != "byval") return false;
    }
    if (SymbolIndex::build(lines_).userNamesLower().count(lw)) return false;
    rawReplace({caret_.line, start}, {caret_.line, end}, toUpper(w));
    return true;
}

void Document::typeChar(const std::string& ch) {
    if (ch.empty()) return;
    if (ch == "\n" || ch == "\r") { newline(); return; }
    unsigned char c = static_cast<unsigned char>(ch[0]);
    if (c < 0x20 && c != '\t') return;
    Guard g(*this, true);
    const bool word = (ch.size() == 1 && isWordChar(ch[0])) || c >= 0x80;
    {
        const std::string& ln = lines_[static_cast<size_t>(caret_.line)];
        char next = caret_.col < static_cast<int>(ln.size()) ? ln[static_cast<size_t>(caret_.col)] : '\0';

        // type over an auto-inserted closing character
        if (!hasSelection() && opt_.autoCloseBrackets && ch.size() == 1 && next == ch[0] && (c == ')' || c == ']' || c == '"' || c == '\'')) {
            bool ok = true;
            if (c == '"' || c == '\'') {
                ok = false;
                for (const LTok& t : scanLine(ln))
                    if ((t.kind == Tk::String || t.kind == Tk::Char) && t.text[0] == ch[0] && caret_.col > t.start && caret_.col < t.end() && t.terminated) ok = true;
            }
            if (ok) { caret_.col += 1; anchor_ = caret_; noCoalesce_ = true; return; }
        }
    }
    if (!word && opt_.autoCapitalizeKeywords && !hasSelection()) capitalizeWordBeforeCaret(ch.size() == 1 ? ch[0] : '\0');

    const std::string& ln = lines_[static_cast<size_t>(caret_.line)];
    char next = caret_.col < static_cast<int>(ln.size()) ? ln[static_cast<size_t>(caret_.col)] : '\0';

    // wrap the selection:  "text"  (text)  [text]
    if (hasSelection() && opt_.autoCloseBrackets && ch.size() == 1 && std::strchr("([\"'", ch[0])) {
        char close = ch[0] == '(' ? ')' : ch[0] == '[' ? ']' : ch[0];
        Pos a = selStart(), b = selEnd();
        std::string sel = textBetween(a, b);
        rawReplace(a, b, ch + sel + close);
        Pos in{a.line, a.col + 1};
        setSel(in, advance(in, sel));
        noCoalesce_ = true;
        return;
    }
    deleteSelection();
    const std::string& cur = lines_[static_cast<size_t>(caret_.line)];
    next = caret_.col < static_cast<int>(cur.size()) ? cur[static_cast<size_t>(caret_.col)] : '\0';
    char prev = caret_.col > 0 ? cur[static_cast<size_t>(caret_.col - 1)] : '\0';

    std::string ins = ch;
    bool pair = false;
    if (opt_.autoCloseBrackets && ch.size() == 1) {
        bool nextOk = next == '\0' || next == ' ' || next == '\t' || next == ')' || next == ']' || next == ',';
        if (ch[0] == '(' || ch[0] == '[') { pair = nextOk && !inStringOrComment(caret_); if (pair) ins += ch[0] == '(' ? ')' : ']'; }
        else if (ch[0] == '"' || ch[0] == '\'') {
            pair = nextOk && !isWordChar(prev) && !inStringOrComment(caret_);
            if (pair) ins += ch[0];
        }
    }
    Pos e = rawReplace(caret_, caret_, ins);
    if (pair) e.col -= 1;
    setSel(e, e);
    desiredCol_ = -1;
    if (!word) {
        if (opt_.autoIndent) reindentLineIfCloser(caret_.line);
        noCoalesce_ = true;
    } else noCoalesce_ = false;
}

void Document::typeText(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        int n = charLen(s, static_cast<int>(i));
        if (n <= 0) break;
        typeChar(s.substr(i, static_cast<size_t>(n)));
        i += static_cast<size_t>(n);
    }
}

// ---------------------------------------------------------------- indentation helpers
void Document::setLineIndent(int line, int level) {
    std::string want = indentString(level);
    int cur = leadingWhitespaceBytes(line);
    if (lines_[static_cast<size_t>(line)].compare(0, static_cast<size_t>(cur), want) == 0 && cur == static_cast<int>(want.size())) return;
    Guard g(*this);
    rawReplace({line, 0}, {line, cur}, want);
    auto fix = [&](Pos& p) {
        if (p.line != line) return;
        p.col = p.col >= cur ? p.col + static_cast<int>(want.size()) - cur : static_cast<int>(want.size());
    };
    fix(caret_); fix(anchor_);
}

void Document::reindentLine(int line) { setLineIndent(line, indentLevelOf(lines_, line)); }

void Document::reindentLineIfCloser(int line) {
    if (line < 0 || line >= lineCount()) return;
    std::vector<LTok> t;
    for (const LTok& x : scanLine(lines_[static_cast<size_t>(line)])) if (x.kind != Tk::Comment) t.push_back(x);
    promoteKeywords(t);
    if (t.empty() || t[0].kind != Tk::Keyword) return;
    static const char* const kw[] = {"ENDIF", "ELSE", "NEXT", "ENDWHILE", "UNTIL", "ENDPROCEDURE", "ENDFUNCTION", "ENDTYPE", "ENDCLASS", "ENDCASE", "OTHERWISE"};
    for (const char* k : kw) if (t[0].text == k) { reindentLine(line); return; }
}

bool Document::tryAutoCloseBlock(int line) {
    BlockKind kind; std::string loopVar; bool complete = false;
    if (!lineOpensBlock(lines_[static_cast<size_t>(line)], kind, loopVar, complete) || !complete) return false;
    // Is there already a matching closer of the same kind further down? (Other kinds are ignored on purpose: an outer
    // ENDWHILE must not count as closing an inner IF.)
    {
        int depth = 1;
        const std::string closerWord = closerKeyword(kind);
        for (int i = line + 1; i < lineCount() && depth > 0; ++i) {
            BlockKind k2; std::string lv; bool c2;
            std::vector<LTok> t;
            for (const LTok& x : scanLine(lines_[static_cast<size_t>(i)])) if (x.kind != Tk::Comment) t.push_back(x);
            promoteKeywords(t);
            if (t.empty()) continue;
            if (lineOpensBlock(lines_[static_cast<size_t>(i)], k2, lv, c2) && k2 == kind) ++depth;
            else if (t[0].kind == Tk::Keyword && t[0].text == closerWord && --depth == 0) return false;
        }
    }
    std::string closer = closerKeyword(kind);
    if (kind == BlockKind::For && !loopVar.empty()) closer += " " + loopVar;
    std::string lead = lines_[static_cast<size_t>(line)].substr(0, static_cast<size_t>(leadingWhitespaceBytes(line)));
    int bodyLine = caret_.line;
    rawReplace({bodyLine, static_cast<int>(lines_[static_cast<size_t>(bodyLine)].size())}, {bodyLine, static_cast<int>(lines_[static_cast<size_t>(bodyLine)].size())}, "\n" + lead + closer);
    return true;
}

void Document::newline() {
    Guard g(*this);
    deleteSelection();
    if (opt_.autoCapitalizeKeywords) capitalizeWordBeforeCaret('\n');
    if (opt_.autoIndent) reindentLineIfCloser(caret_.line);
    const int L = caret_.line;
    std::string ln = lines_[static_cast<size_t>(L)];
    int col = caret_.col;
    std::string left = ln.substr(0, static_cast<size_t>(col)), right = ln.substr(static_cast<size_t>(col));

    std::string newIndent;
    if (opt_.autoIndent) {
        BlockTracker bt = trackerBefore(lines_, L);
        bt.feed(scanLine(left), L);
        newIndent = indentString(bt.depth());
    } else newIndent = ln.substr(0, static_cast<size_t>(leadingWhitespaceBytes(L)));

    int removeFrom = col;
    if (isBlank(left)) removeFrom = 0;                       // do not leave a line of stray spaces behind
    std::string insertion = "\n" + newIndent + ltrim(right);
    rawReplace({L, removeFrom}, {L, static_cast<int>(ln.size())}, insertion);
    Pos np{L + 1, static_cast<int>(newIndent.size())};
    setSel(np, np);
    if (!isBlank(right) && opt_.autoIndent) reindentLineIfCloser(L + 1);
    if (opt_.autoCloseBlocks && opt_.autoIndent && isBlank(right) && !isBlank(left)) tryAutoCloseBlock(L);
    resetDesired();
}

void Document::backspace() {
    Guard g(*this);
    if (hasSelection()) { deleteSelection(); resetDesired(); return; }
    Pos c = caret_;
    if (c.line == 0 && c.col == 0) return;
    if (c.col == 0) {
        Pos prevEnd{c.line - 1, static_cast<int>(lines_[static_cast<size_t>(c.line - 1)].size())};
        rawReplace(prevEnd, c, "");
        setSel(prevEnd, prevEnd);
        resetDesired();
        return;
    }
    const std::string& s = lines_[static_cast<size_t>(c.line)];
    if (opt_.autoCloseBrackets && c.col < static_cast<int>(s.size())) {
        char a = s[static_cast<size_t>(c.col - 1)], b = s[static_cast<size_t>(c.col)];
        if ((a == '(' && b == ')') || (a == '[' && b == ']') || (a == '"' && b == '"') || (a == '\'' && b == '\'')) {
            rawReplace({c.line, c.col - 1}, {c.line, c.col + 1}, "");
            Pos p{c.line, c.col - 1}; setSel(p, p); resetDesired();
            return;
        }
    }
    bool onlySpaces = true;
    for (int i = 0; i < c.col; ++i) if (s[static_cast<size_t>(i)] != ' ') onlySpaces = false;
    int from;
    if (onlySpaces && opt_.insertSpaces && opt_.tabSize > 1) from = ((c.col - 1) / opt_.tabSize) * opt_.tabSize;
    else from = prevCharStart(s, c.col);
    rawReplace({c.line, from}, c, "");
    Pos p{c.line, from}; setSel(p, p);
    desiredCol_ = -1; noCoalesce_ = true;
}

void Document::deleteForward() {
    Guard g(*this);
    if (hasSelection()) { deleteSelection(); resetDesired(); return; }
    const std::string& s = lines_[static_cast<size_t>(caret_.line)];
    if (caret_.col >= static_cast<int>(s.size())) {
        if (caret_.line + 1 < lineCount()) rawReplace(caret_, {caret_.line + 1, 0}, "");
    } else rawReplace(caret_, {caret_.line, caret_.col + charLen(s, caret_.col)}, "");
    resetDesired();
}

void Document::deleteWordLeft() {
    if (hasSelection()) { Guard g(*this); deleteSelection(); resetDesired(); return; }
    Pos start = caret_;
    moveLeft(false, true);
    Pos target = caret_;
    if (target == start) return;
    Guard g(*this);
    rawReplace(target, start, "");
    setSel(target, target);
    resetDesired();
}

void Document::deleteWordRight() {
    if (hasSelection()) { Guard g(*this); deleteSelection(); resetDesired(); return; }
    Pos start = caret_;
    moveRight(false, true);
    Pos target = caret_;
    if (target == start) return;
    Guard g(*this);
    rawReplace(start, target, "");
    setSel(start, start);
    resetDesired();
}

void Document::indentLines(int first, int last, bool outdent) {
    Guard g(*this);
    std::vector<int> delta(static_cast<size_t>(lineCount()), 0);
    std::string unit = indentUnit();
    for (int i = first; i <= last; ++i) {
        const std::string& s = lines_[static_cast<size_t>(i)];
        if (outdent) {
            int remove = 0;
            if (!s.empty() && s[0] == '\t') remove = 1;
            else while (remove < opt_.tabSize && remove < static_cast<int>(s.size()) && s[static_cast<size_t>(remove)] == ' ') ++remove;
            if (remove) { rawReplace({i, 0}, {i, remove}, ""); delta[static_cast<size_t>(i)] = -remove; }
        } else if (!s.empty()) {
            rawReplace({i, 0}, {i, 0}, unit);
            delta[static_cast<size_t>(i)] = static_cast<int>(unit.size());
        }
    }
    auto fix = [&](Pos& p) {
        int d = delta[static_cast<size_t>(p.line)];
        if (d) p.col = std::max(0, p.col + d);
    };
    fix(caret_); if (anchor_ != caret_) fix(anchor_); else anchor_ = caret_;
}

void Document::tab(bool shift) {
    if (hasSelection() && selStart().line != selEnd().line) {
        int a = selStart().line, b = selEnd().line;
        if (selEnd().col == 0) --b;
        indentLines(a, b, shift);
        resetDesired();
        return;
    }
    if (shift) { indentLines(caret_.line, caret_.line, true); resetDesired(); return; }
    Guard g(*this);
    deleteSelection();
    std::string ins = "\t";
    if (opt_.insertSpaces) {
        int w = std::max(1, opt_.tabSize);
        ins = std::string(static_cast<size_t>(w - displayColumn(caret_.line, caret_.col) % w), ' ');
    }
    Pos e = rawReplace(caret_, caret_, ins);
    setSel(e, e);
    resetDesired();
}

void Document::deleteLines() {
    int a = selStart().line, b = selEnd().line;
    if (a != b && selEnd().col == 0) --b;
    Guard g(*this);
    if (b + 1 < lineCount()) rawReplace({a, 0}, {b + 1, 0}, "");
    else if (a > 0) rawReplace({a - 1, static_cast<int>(lines_[static_cast<size_t>(a - 1)].size())}, {b, static_cast<int>(lines_[static_cast<size_t>(b)].size())}, "");
    else rawReplace({0, 0}, {b, static_cast<int>(lines_[static_cast<size_t>(b)].size())}, "");
    Pos p = clamp({a, 0});
    setSel(p, p);
    resetDesired();
}

void Document::duplicateLines() {
    int a = selStart().line, b = selEnd().line;
    if (a != b && selEnd().col == 0) --b;
    Guard g(*this);
    std::string block;
    for (int i = a; i <= b; ++i) { if (i > a) block += '\n'; block += lines_[static_cast<size_t>(i)]; }
    int n = b - a + 1;
    Pos at{b, static_cast<int>(lines_[static_cast<size_t>(b)].size())};
    rawReplace(at, at, "\n" + block);
    caret_.line += n; anchor_.line += n;
    resetDesired();
}

void Document::moveLines(int dir) {
    int a = selStart().line, b = selEnd().line;
    if (a != b && selEnd().col == 0) --b;
    if ((dir < 0 && a == 0) || (dir > 0 && b == lineCount() - 1)) return;
    Guard g(*this);
    std::string block;
    for (int i = a; i <= b; ++i) { if (i > a) block += '\n'; block += lines_[static_cast<size_t>(i)]; }
    if (dir < 0) {
        std::string other = lines_[static_cast<size_t>(a - 1)];
        rawReplace({a - 1, 0}, {b, static_cast<int>(lines_[static_cast<size_t>(b)].size())}, block + "\n" + other);
        caret_.line -= 1; anchor_.line -= 1;
    } else {
        std::string other = lines_[static_cast<size_t>(b + 1)];
        rawReplace({a, 0}, {b + 1, static_cast<int>(lines_[static_cast<size_t>(b + 1)].size())}, other + "\n" + block);
        caret_.line += 1; anchor_.line += 1;
    }
    resetDesired();
}

void Document::toggleComment() {
    int a = selStart().line, b = selEnd().line;
    if (a != b && selEnd().col == 0) --b;
    bool allCommented = true, anyCode = false;
    int minIndent = 1 << 20;
    for (int i = a; i <= b; ++i) {
        const std::string& s = lines_[static_cast<size_t>(i)];
        if (isBlank(s)) continue;
        anyCode = true;
        int ind = leadingWhitespaceBytes(i);
        minIndent = std::min(minIndent, ind);
        if (s.compare(static_cast<size_t>(ind), 2, "//") != 0) allCommented = false;
    }
    if (!anyCode) { allCommented = false; minIndent = 0; }
    Guard g(*this);
    std::vector<int> delta(static_cast<size_t>(lineCount()), 0);
    for (int i = a; i <= b; ++i) {
        const std::string& s = lines_[static_cast<size_t>(i)];
        if (allCommented) {
            if (isBlank(s)) continue;
            int ind = leadingWhitespaceBytes(i);
            int n = (static_cast<int>(s.size()) > ind + 2 && s[static_cast<size_t>(ind + 2)] == ' ') ? 3 : 2;
            rawReplace({i, ind}, {i, ind + n}, "");
            delta[static_cast<size_t>(i)] = -n;
        } else {
            if (isBlank(s) && anyCode) continue;
            int at = std::min(minIndent, static_cast<int>(s.size()));
            rawReplace({i, at}, {i, at}, "// ");
            delta[static_cast<size_t>(i)] = 3;
        }
    }
    auto fix = [&](Pos& p) { int d = delta[static_cast<size_t>(p.line)]; if (d) p.col = std::max(0, p.col + d); };
    bool coll = !hasSelection();
    fix(caret_); if (coll) anchor_ = caret_; else fix(anchor_);
    resetDesired();
}

void Document::trimTrailingWhitespace() {
    Guard g(*this);
    for (int i = 0; i < lineCount(); ++i) {
        const std::string& s = lines_[static_cast<size_t>(i)];
        size_t e = s.find_last_not_of(" \t");
        size_t keep = e == std::string::npos ? 0 : e + 1;
        if (keep < s.size()) rawReplace({i, static_cast<int>(keep)}, {i, static_cast<int>(s.size())}, "");
    }
    caret_ = clamp(caret_); anchor_ = clamp(anchor_);
}

void Document::applyCompletion(int line, int startCol, int endCol, const std::string& text) {
    line = std::max(0, std::min(line, lineCount() - 1));
    int len = static_cast<int>(lines_[static_cast<size_t>(line)].size());
    startCol = std::max(0, std::min(startCol, len)); endCol = std::max(startCol, std::min(endCol, len));
    Guard g(*this);
    std::string lead = lines_[static_cast<size_t>(line)].substr(0, static_cast<size_t>(leadingWhitespaceBytes(line)));
    std::string body;
    {
        size_t p = 0; bool firstLine = true;
        for (;;) {
            size_t nl = text.find('\n', p);
            std::string part = text.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
            if (!firstLine) {
                size_t sp = 0;
                while (sp < part.size() && part[sp] == ' ') ++sp;
                std::string ind;
                for (size_t u = 0; u < sp / 4; ++u) ind += indentUnit();
                ind += std::string(sp % 4, ' ');
                part = lead + ind + part.substr(sp);
                body += '\n';
            }
            body += part;
            firstLine = false;
            if (nl == std::string::npos) break;
            p = nl + 1;
        }
    }
    size_t marker = body.find("$0");
    size_t caretOff = marker == std::string::npos ? body.size() : marker;
    if (marker != std::string::npos) body.erase(marker, 2);
    Pos start{line, startCol};
    rawReplace(start, {line, endCol}, body);
    Pos c = advance(start, body.substr(0, caretOff));
    setSel(c, c);
    if (opt_.autoIndent) reindentLineIfCloser(line);
    resetDesired();
}

bool Document::formatDocument(const FormatOptions& fo) {
    FormatResult r = formatSource(joinLines(lines_, "\n"), fo);
    if (!r.ok || !r.changed) return false;
    auto nonBlankBefore = [&](int line) { int n = 0; for (int i = 0; i < line; ++i) if (!isBlank(lines_[static_cast<size_t>(i)])) ++n; return n; };
    int oldIdx = nonBlankBefore(caret_.line);
    bool wasBlank = isBlank(lines_[static_cast<size_t>(caret_.line)]);
    int rel = std::max(0, caret_.col - leadingWhitespaceBytes(caret_.line));
    int oldLine = caret_.line;

    Guard g(*this);
    rawReplace({0, 0}, {lineCount() - 1, static_cast<int>(lines_.back().size())}, r.text);
    Pos np;
    if (wasBlank) np = clamp({std::min(oldLine, lineCount() - 1), 0});
    else {
        int seen = 0, target = lineCount() - 1;
        for (int i = 0; i < lineCount(); ++i) {
            if (isBlank(lines_[static_cast<size_t>(i)])) continue;
            if (seen == oldIdx) { target = i; break; }
            ++seen;
        }
        np = clamp({target, leadingWhitespaceBytes(target) + rel});
    }
    setSel(np, np);
    resetDesired();
    return true;
}

// ---------------------------------------------------------------- search
static bool wordBoundaryOk(const std::string& s, size_t at, size_t len) {
    bool before = at == 0 || !isWordChar(s[at - 1]);
    bool after = at + len >= s.size() || !isWordChar(s[at + len]);
    return before && after;
}

bool Document::find(const std::string& needle, const FindOptions& fo, Pos from, bool forward, bool wrap, Pos& start, Pos& end) const {
    if (needle.empty()) return false;
    from = clamp(from);
    const std::string key = fo.matchCase ? needle : toLower(needle);
    const int n = lineCount();
    for (int k = 0; k <= n; ++k) {
        int li = forward ? from.line + k : from.line - k;
        if (li < 0 || li >= n) {
            if (!wrap) break;
            li = ((li % n) + n) % n;
        }
        const std::string& raw = lines_[static_cast<size_t>(li)];
        const std::string hay = fo.matchCase ? raw : toLower(raw);
        if (forward) {
            size_t pos = (k == 0) ? static_cast<size_t>(from.col) : 0;
            while ((pos = hay.find(key, pos)) != std::string::npos) {
                if (!fo.wholeWord || wordBoundaryOk(hay, pos, key.size())) { start = {li, static_cast<int>(pos)}; end = {li, static_cast<int>(pos + key.size())}; return true; }
                ++pos;
            }
        } else {
            long long limit = (k == 0) ? static_cast<long long>(from.col) - static_cast<long long>(key.size()) : static_cast<long long>(hay.size());
            if (limit < 0) continue;
            size_t pos = static_cast<size_t>(limit);
            while ((pos = hay.rfind(key, pos)) != std::string::npos) {
                if (!fo.wholeWord || wordBoundaryOk(hay, pos, key.size())) { start = {li, static_cast<int>(pos)}; end = {li, static_cast<int>(pos + key.size())}; return true; }
                if (pos == 0) break;
                --pos;
            }
        }
    }
    return false;
}

bool Document::findNext(const std::string& needle, const FindOptions& fo, bool forward, bool wrap) {
    Pos s, e;
    Pos from = forward ? selEnd() : selStart();
    if (!find(needle, fo, from, forward, wrap, s, e)) return false;
    setSel(s, e); resetDesired();
    return true;
}

bool Document::replaceCurrent(const std::string& needle, const std::string& with, const FindOptions& fo) {
    bool replaced = false;
    if (hasSelection()) {
        std::string sel = selectedText();
        if (fo.matchCase ? sel == needle : toLower(sel) == toLower(needle)) {
            Guard g(*this);
            Pos a = selStart();
            Pos e = rawReplace(a, selEnd(), with);
            setSel(e, e);
            replaced = true;
        }
    }
    findNext(needle, fo, true, true);
    return replaced;
}

int Document::replaceAll(const std::string& needle, const std::string& with, const FindOptions& fo) {
    if (needle.empty()) return 0;
    std::vector<Pos> hits;
    const std::string key = fo.matchCase ? needle : toLower(needle);
    for (int i = 0; i < lineCount(); ++i) {
        const std::string hay = fo.matchCase ? lines_[static_cast<size_t>(i)] : toLower(lines_[static_cast<size_t>(i)]);
        size_t pos = 0;
        while ((pos = hay.find(key, pos)) != std::string::npos) {
            if (!fo.wholeWord || wordBoundaryOk(hay, pos, key.size())) { hits.push_back({i, static_cast<int>(pos)}); pos += key.size(); }
            else ++pos;
        }
    }
    if (hits.empty()) return 0;
    Guard g(*this);
    for (size_t i = hits.size(); i-- > 0;) rawReplace(hits[i], {hits[i].line, hits[i].col + static_cast<int>(key.size())}, with);
    caret_ = clamp(caret_); anchor_ = caret_;
    resetDesired();
    return static_cast<int>(hits.size());
}

// ---------------------------------------------------------------- view helpers
int Document::displayColumn(int line, int col) const {
    const std::string& s = lines_[static_cast<size_t>(std::max(0, std::min(line, lineCount() - 1)))];
    int tab = std::max(1, opt_.tabSize), d = 0;
    col = std::min(col, static_cast<int>(s.size()));
    for (int i = 0; i < col; ++i) {
        unsigned char c = static_cast<unsigned char>(s[static_cast<size_t>(i)]);
        if (c == '\t') d += tab - d % tab;
        else if ((c & 0xC0) != 0x80) ++d;
    }
    return d;
}

int Document::colFromDisplay(int line, int target) const {
    const std::string& s = lines_[static_cast<size_t>(std::max(0, std::min(line, lineCount() - 1)))];
    int tab = std::max(1, opt_.tabSize), d = 0, i = 0, n = static_cast<int>(s.size());
    while (i < n) {
        int w = s[static_cast<size_t>(i)] == '\t' ? tab - d % tab : 1;
        if (d + w > target) {
            // closer to the left or right edge of this character?
            return (target - d) * 2 >= w ? i + charLen(s, i) : i;
        }
        d += w; i += charLen(s, i);
    }
    return n;
}

Pos Document::matchingBracket(Pos p) const {
    p = clamp(p);
    auto bracketsOf = [&](int line) {
        std::vector<std::pair<int, char>> v;
        for (const LTok& t : scanLine(lines_[static_cast<size_t>(line)]))
            if (t.kind == Tk::Op && t.len == 1 && std::strchr("()[]", t.text[0])) v.emplace_back(t.start, t.text[0]);
        return v;
    };
    auto at = [&](int line, int col) -> char {
        for (auto& b : bracketsOf(line)) if (b.first == col) return b.second;
        return '\0';
    };
    Pos hit{-1, -1};
    char c = at(p.line, p.col);
    if (c) hit = p;
    else if (p.col > 0 && (c = at(p.line, p.col - 1))) hit = {p.line, p.col - 1};
    if (!c) return {-1, -1};
    bool open = c == '(' || c == '[';
    char partner = c == '(' ? ')' : c == ')' ? '(' : c == '[' ? ']' : '[';
    int depth = 0;
    if (open) {
        for (int li = hit.line; li < lineCount() && li < hit.line + 2000; ++li)
            for (auto& b : bracketsOf(li)) {
                if (li == hit.line && b.first < hit.col) continue;
                if (b.second == c) ++depth;
                else if (b.second == partner && --depth == 0) return {li, b.first};
            }
    } else {
        for (int li = hit.line; li >= 0 && li > hit.line - 2000; --li) {
            auto v = bracketsOf(li);
            for (size_t i = v.size(); i-- > 0;) {
                if (li == hit.line && v[i].first > hit.col) continue;
                if (v[i].second == c) ++depth;
                else if (v[i].second == partner && --depth == 0) return {li, v[i].first};
            }
        }
    }
    return {-1, -1};
}

} // namespace studio
