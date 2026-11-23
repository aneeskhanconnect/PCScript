#include "structure.hpp"

namespace studio {

const char* closerKeyword(BlockKind k) {
    switch (k) {
        case BlockKind::If: return "ENDIF";
        case BlockKind::Case: return "ENDCASE";
        case BlockKind::For: return "NEXT";
        case BlockKind::While: return "ENDWHILE";
        case BlockKind::Repeat: return "UNTIL";
        case BlockKind::Procedure: return "ENDPROCEDURE";
        case BlockKind::Function: return "ENDFUNCTION";
        case BlockKind::Type: return "ENDTYPE";
        case BlockKind::Class: return "ENDCLASS";
    }
    return "";
}

int BlockTracker::depth() const {
    int d = 0;
    for (const OpenBlock& b : stack_) d += (b.kind == BlockKind::Case) ? (b.inBranch ? 2 : 1) : 1;
    return d;
}

int BlockTracker::depthBelowTop() const {
    int d = 0;
    for (size_t i = 0; i + 1 < stack_.size(); ++i) d += (stack_[i].kind == BlockKind::Case) ? (stack_[i].inBranch ? 2 : 1) : 1;
    return d;
}

bool BlockTracker::closeBlock(BlockKind k) {
    for (size_t i = stack_.size(); i-- > 0;) {
        if (stack_[i].kind == k) { stack_.resize(i); return true; }
    }
    return false;
}

static bool isKw(const std::vector<LTok>& t, size_t i, const char* s) {
    return i < t.size() && t[i].kind == Tk::Keyword && t[i].text == s;
}
static bool isOp(const std::vector<LTok>& t, size_t i, const char* s) {
    return i < t.size() && t[i].kind == Tk::Op && t[i].text == s;
}

// Same idea as Parser::branchStart in src/parser/parser.cpp.
static bool looksLikeCaseLabel(const std::vector<LTok>& t) {
    if (t.empty()) return false;
    const LTok& c = t[0];
    if (c.kind == Tk::Number || c.kind == Tk::String || c.kind == Tk::Char || c.kind == Tk::Constant) return true;
    if (isOp(t, 0, "-")) return true;
    if (c.kind == Tk::Ident) return isOp(t, 1, ":") || isOp(t, 1, ",") || isKw(t, 1, "TO");
    return false;
}

static bool openerOf(const std::vector<LTok>& t, size_t k, BlockKind& kind) {
    if (k >= t.size() || t[k].kind != Tk::Keyword) return false;
    const std::string& w = t[k].text;
    if (w == "IF") kind = BlockKind::If;
    else if (w == "CASE") kind = BlockKind::Case;
    else if (w == "FOR") kind = BlockKind::For;
    else if (w == "WHILE") kind = BlockKind::While;
    else if (w == "REPEAT") kind = BlockKind::Repeat;
    else if (w == "PROCEDURE") kind = BlockKind::Procedure;
    else if (w == "FUNCTION") kind = BlockKind::Function;
    else if (w == "CLASS") kind = BlockKind::Class;
    else if (w == "TYPE") {
        // TYPE Name = ...  is a one-line definition (enumeration, pointer, alias); TYPE Name opens a record.
        if (isOp(t, k + 2, "=")) return false;
        kind = BlockKind::Type;
    } else return false;
    return true;
}

LineResult BlockTracker::feed(const std::vector<LTok>& all, int lineNumber) {
    LineResult r;
    std::vector<LTok> t;
    for (const LTok& x : all) if (x.kind != Tk::Comment) t.push_back(x);
    promoteKeywords(t);
    if (t.empty()) {
        r.cls = all.empty() ? LineClass::Blank : LineClass::CommentOnly;
        r.indent = depth();
        return r;
    }
    r.cls = LineClass::Code;

    size_t k = 0;
    if (isKw(t, 0, "PUBLIC") || isKw(t, 0, "PRIVATE")) k = 1;
    const std::string kw = (k < t.size() && t[k].kind == Tk::Keyword) ? t[k].text : std::string();

    // A CASE label (or OTHERWISE) when the innermost open block is a CASE.
    if (!stack_.empty() && stack_.back().kind == BlockKind::Case && kw != "ENDCASE" &&
        (kw == "OTHERWISE" || looksLikeCaseLabel(t))) {
        r.indent = depthBelowTop() + 1;
        stack_.back().inBranch = true;
        r.closes = true;
        // A block opened on the same line after the colon:  1 : IF x THEN
        size_t colon = 0;
        for (size_t i = 0; i < t.size(); ++i) if (isOp(t, i, ":")) { colon = i; break; }
        BlockKind ok;
        if (colon && openerOf(t, colon + 1, ok)) { stack_.push_back({ok, lineNumber, false, ""}); r.opens = true; }
        return r;
    }

    auto closer = [&](BlockKind kind) {
        r.closes = closeBlock(kind);
        r.indent = depth();
    };
    if (kw == "ENDIF") { closer(BlockKind::If); return r; }
    if (kw == "ENDCASE") { closer(BlockKind::Case); return r; }
    if (kw == "NEXT") { closer(BlockKind::For); return r; }
    if (kw == "ENDWHILE") { closer(BlockKind::While); return r; }
    if (kw == "UNTIL") { closer(BlockKind::Repeat); return r; }
    if (kw == "ENDPROCEDURE") { closer(BlockKind::Procedure); return r; }
    if (kw == "ENDFUNCTION") { closer(BlockKind::Function); return r; }
    if (kw == "ENDTYPE") { closer(BlockKind::Type); return r; }
    if (kw == "ENDCLASS") { closer(BlockKind::Class); return r; }
    if (kw == "ELSE") {
        bool found = false;
        for (size_t i = stack_.size(); i-- > 0;) if (stack_[i].kind == BlockKind::If) { stack_.resize(i + 1); found = true; break; }
        r.closes = found;
        r.indent = found ? depth() - 1 : depth();
        return r;
    }
    if (kw == "THEN" || kw == "DO") { r.indent = depth(); return r; }   // header continued on the next line

    BlockKind ok;
    if (openerOf(t, k, ok)) {
        r.indent = depth();
        OpenBlock b{ok, lineNumber, false, ""};
        if (ok == BlockKind::For && k + 1 < t.size() && t[k + 1].kind == Tk::Ident) b.loopVar = t[k + 1].text;
        stack_.push_back(b);
        r.opens = true;
        return r;
    }
    r.indent = depth();
    return r;
}

BlockTracker trackerBefore(const std::vector<std::string>& lines, int upTo) {
    BlockTracker bt;
    for (int i = 0; i < upTo && i < static_cast<int>(lines.size()); ++i) bt.feed(scanLine(lines[static_cast<size_t>(i)]), i);
    return bt;
}

int indentLevelOf(const std::vector<std::string>& lines, int index) {
    BlockTracker bt = trackerBefore(lines, index);
    return bt.feed(scanLine(lines[static_cast<size_t>(index)]), index).indent;
}

bool lineOpensBlock(const std::string& line, BlockKind& kind, std::string& loopVar, bool& headerComplete) {
    std::vector<LTok> all = scanLine(line), t;
    for (const LTok& x : all) if (x.kind != Tk::Comment) t.push_back(x);
    promoteKeywords(t);
    size_t k = 0;
    if (isKw(t, 0, "PUBLIC") || isKw(t, 0, "PRIVATE")) k = 1;
    if (!openerOf(t, k, kind)) return false;
    loopVar.clear();
    headerComplete = true;
    bool hasThen = false, hasDo = false, hasReturns = false, hasOf = false, hasTo = false;
    for (const LTok& x : t) {
        if (x.kind != Tk::Keyword) continue;
        if (x.text == "THEN") hasThen = true;
        else if (x.text == "DO") hasDo = true;
        else if (x.text == "RETURNS") hasReturns = true;
        else if (x.text == "OF") hasOf = true;
        else if (x.text == "TO") hasTo = true;
    }
    switch (kind) {
        case BlockKind::If: headerComplete = hasThen; break;
        case BlockKind::While: headerComplete = hasDo; break;
        case BlockKind::Function: headerComplete = hasReturns; break;
        case BlockKind::Case: headerComplete = hasOf && t.size() >= k + 3; break;
        case BlockKind::For:
            headerComplete = hasTo && k + 1 < t.size() && t[k + 1].kind == Tk::Ident;
            if (k + 1 < t.size() && t[k + 1].kind == Tk::Ident) loopVar = t[k + 1].text;
            break;
        case BlockKind::Procedure: case BlockKind::Class: headerComplete = k + 1 < t.size(); break;
        case BlockKind::Type: headerComplete = k + 1 < t.size() && t[k + 1].kind == Tk::Ident; break;
        case BlockKind::Repeat: break;
    }
    return true;
}

} // namespace studio
