#include "symbols.hpp"
#include <algorithm>
#include "scan.hpp"

namespace studio {

const char* symKindName(SymKind k) {
    switch (k) {
        case SymKind::Variable: return "variable"; case SymKind::Constant: return "constant";
        case SymKind::Parameter: return "parameter"; case SymKind::Procedure: return "procedure";
        case SymKind::Function: return "function"; case SymKind::Class: return "class";
        case SymKind::Record: return "record"; case SymKind::Enum: return "enumeration";
        case SymKind::EnumValue: return "enumeration value"; case SymKind::Pointer: return "pointer type";
        case SymKind::Alias: return "type alias"; case SymKind::Field: return "field"; case SymKind::Method: return "method";
    }
    return "";
}

namespace {

using Toks = std::vector<LTok>;

bool isKw(const Toks& t, size_t i, const char* s) { return i < t.size() && t[i].kind == Tk::Keyword && t[i].text == s; }
bool isOp(const Toks& t, size_t i, const char* s) { return i < t.size() && t[i].kind == Tk::Op && t[i].text == s; }

// Text of tokens [from, to) taken from the original line (keeps the user's spelling).
std::string slice(const std::string& line, const Toks& t, size_t from, size_t to) {
    if (from >= to || from >= t.size()) return "";
    to = std::min(to, t.size());
    return line.substr(static_cast<size_t>(t[from].start), static_cast<size_t>(t[to - 1].end() - t[from].start));
}

struct Param { std::string name, type; bool byRef = false; };

// Parses "( [BYREF|BYVAL] a, b : T, c : U )" starting at the '(' at index i; returns the index after ')'.
size_t parseParams(const std::string& line, const Toks& t, size_t i, std::vector<Param>& out) {
    if (!isOp(t, i, "(")) return i;
    size_t j = i + 1, depth = 0;
    std::vector<std::pair<size_t, size_t>> pieces;   // token ranges separated by top-level commas
    size_t start = j;
    for (; j < t.size(); ++j) {
        if (isOp(t, j, "[") || isOp(t, j, "(")) ++depth;
        else if (isOp(t, j, "]")) { if (depth) --depth; }
        else if (isOp(t, j, ")")) { if (depth == 0) break; --depth; }
        else if (isOp(t, j, ",") && depth == 0) { pieces.emplace_back(start, j); start = j + 1; }
    }
    if (start < j) pieces.emplace_back(start, j);
    // BYREF / BYVAL apply to every parameter after them until the other one appears (same rule as the parser).
    std::vector<bool> refOf(pieces.size(), false);
    bool sticky = false;
    for (size_t pi = 0; pi < pieces.size(); ++pi) {
        if (isKw(t, pieces[pi].first, "BYREF")) sticky = true; else if (isKw(t, pieces[pi].first, "BYVAL")) sticky = false;
        refOf[pi] = sticky;
    }
    // Walk backwards so a bare name inherits the type of the piece that follows it:  a, b : INTEGER
    std::string curType;
    std::vector<Param> rev;
    for (size_t pi = pieces.size(); pi-- > 0;) {
        size_t a = pieces[pi].first, b = pieces[pi].second, colon = b;
        for (size_t k = a; k < b; ++k) if (isOp(t, k, ":")) { colon = k; break; }
        size_t na = a;
        if (isKw(t, na, "BYREF") || isKw(t, na, "BYVAL")) ++na;
        if (colon < b) curType = slice(line, t, colon + 1, b);
        for (size_t k = na; k < colon; ++k)
            if (t[k].kind == Tk::Ident) rev.push_back({t[k].text, curType, refOf[pi]});
    }
    std::reverse(rev.begin(), rev.end());
    for (auto& p : rev) out.push_back(std::move(p));
    return j < t.size() ? j + 1 : j;
}

} // namespace

SymbolIndex SymbolIndex::build(const std::vector<std::string>& lines) {
    SymbolIndex idx;
    idx.lastLine_ = lines.empty() ? 0 : static_cast<int>(lines.size()) - 1;

    struct Ctx { enum K { Class, Record, Routine } k; size_t symIndex; std::string name; };
    std::vector<Ctx> ctx;

    auto closeCtx = [&](Ctx::K k, int line) {
        for (size_t i = ctx.size(); i-- > 0;) {
            if (ctx[i].k != k) continue;
            idx.syms_[ctx[i].symIndex].scopeEnd = line;
            // locals/params recorded after the routine started share its extent
            if (k == Ctx::Routine) {
                for (size_t s = ctx[i].symIndex + 1; s < idx.syms_.size(); ++s)
                    if ((idx.syms_[s].kind == SymKind::Parameter || idx.syms_[s].kind == SymKind::Variable || idx.syms_[s].kind == SymKind::Constant) &&
                        idx.syms_[s].scopeStart == idx.syms_[ctx[i].symIndex].line && idx.syms_[s].scopeEnd < 0)
                        idx.syms_[s].scopeEnd = line;
            }
            ctx.resize(i);
            return;
        }
    };
    auto currentRoutine = [&]() -> const Ctx* {
        for (size_t i = ctx.size(); i-- > 0;) if (ctx[i].k == Ctx::Routine) return &ctx[i];
        return nullptr;
    };
    auto innermost = [&]() -> const Ctx* { return ctx.empty() ? nullptr : &ctx.back(); };

    for (int ln = 0; ln < static_cast<int>(lines.size()); ++ln) {
        const std::string& line = lines[static_cast<size_t>(ln)];
        Toks all = scanLine(line), t;
        for (const LTok& x : all) if (x.kind != Tk::Comment) t.push_back(x);
        promoteKeywords(t);
        if (t.empty()) continue;

        size_t k = 0;
        bool priv = false;
        if (isKw(t, 0, "PUBLIC")) k = 1; else if (isKw(t, 0, "PRIVATE")) { k = 1; priv = true; }
        const std::string kw = (k < t.size() && t[k].kind == Tk::Keyword) ? t[k].text : std::string();

        if (kw == "ENDCLASS") { closeCtx(Ctx::Class, ln); continue; }
        if (kw == "ENDTYPE") { closeCtx(Ctx::Record, ln); continue; }
        if (kw == "ENDPROCEDURE" || kw == "ENDFUNCTION") { closeCtx(Ctx::Routine, ln); continue; }

        if (kw == "CLASS" && k + 1 < t.size() && t[k + 1].kind == Tk::Ident) {
            Symbol s; s.name = t[k + 1].text; s.kind = SymKind::Class; s.line = ln; s.scopeStart = ln; s.scopeEnd = -1;
            if (isKw(t, k + 2, "INHERITS") && k + 3 < t.size() && t[k + 3].kind == Tk::Ident) s.parent = t[k + 3].text;
            s.signature = "CLASS " + s.name + (s.parent.empty() ? "" : " INHERITS " + s.parent);
            idx.syms_.push_back(s);
            ctx.push_back({Ctx::Class, idx.syms_.size() - 1, s.name});
            continue;
        }
        if (kw == "TYPE" && k + 1 < t.size() && t[k + 1].kind == Tk::Ident) {
            Symbol s; s.name = t[k + 1].text; s.line = ln; s.scopeStart = ln; s.scopeEnd = ln;
            if (isOp(t, k + 2, "=")) {
                if (isOp(t, k + 3, "(")) {
                    s.kind = SymKind::Enum;
                    std::vector<std::string> vals;
                    for (size_t i = k + 4; i < t.size() && !isOp(t, i, ")"); ++i)
                        if (t[i].kind == Tk::Ident) vals.push_back(t[i].text);
                    std::string sig = "TYPE " + s.name + " = (";
                    for (size_t i = 0; i < vals.size(); ++i) sig += (i ? ", " : "") + vals[i];
                    s.signature = sig + ")";
                    idx.syms_.push_back(s);
                    for (const std::string& v : vals) {
                        Symbol e; e.name = v; e.kind = SymKind::EnumValue; e.type = s.name; e.owner = s.name; e.line = ln;
                        idx.syms_.push_back(e);
                    }
                } else if (isOp(t, k + 3, "^")) {
                    s.kind = SymKind::Pointer; s.type = slice(line, t, k + 4, t.size());
                    s.signature = "TYPE " + s.name + " = ^" + s.type; idx.syms_.push_back(s);
                } else {
                    s.kind = SymKind::Alias; s.type = slice(line, t, k + 3, t.size());
                    s.signature = "TYPE " + s.name + " = " + s.type; idx.syms_.push_back(s);
                }
            } else {
                s.kind = SymKind::Record; s.scopeEnd = -1; s.signature = "TYPE " + s.name;
                idx.syms_.push_back(s);
                ctx.push_back({Ctx::Record, idx.syms_.size() - 1, s.name});
            }
            continue;
        }
        if (kw == "PROCEDURE" || kw == "FUNCTION") {
            size_t ni = k + 1;
            if (ni >= t.size() || !(t[ni].kind == Tk::Ident || isKw(t, ni, "NEW"))) continue;
            Symbol s; s.name = t[ni].text; s.line = ln; s.isPrivate = priv; s.scopeStart = ln; s.scopeEnd = -1;
            bool func = kw == "FUNCTION";
            const Ctx* in = innermost();
            bool inClass = in && in->k == Ctx::Class;
            s.kind = inClass ? SymKind::Method : (func ? SymKind::Function : SymKind::Procedure);
            if (inClass) s.owner = in->name;
            std::vector<Param> ps;
            size_t after = parseParams(line, t, ni + 1, ps);
            if (func && isKw(t, after, "RETURNS")) s.type = slice(line, t, after + 1, t.size());
            std::string sig = s.name + "(";
            for (size_t i = 0; i < ps.size(); ++i) sig += (i ? ", " : "") + std::string(ps[i].byRef ? "BYREF " : "") + ps[i].name + " : " + ps[i].type;
            sig += ")";
            if (func && !s.type.empty()) sig += " RETURNS " + s.type;
            s.signature = sig;
            if (inClass && func) s.kind = SymKind::Method;
            idx.syms_.push_back(s);
            ctx.push_back({Ctx::Routine, idx.syms_.size() - 1, s.name});
            for (const Param& p : ps) {
                Symbol q; q.name = p.name; q.kind = SymKind::Parameter; q.type = p.type; q.byRef = p.byRef; q.owner = s.name;
                q.line = ln; q.scopeStart = ln; q.scopeEnd = -1;
                idx.syms_.push_back(q);
            }
            continue;
        }
        if (kw == "CONSTANT" && k + 1 < t.size() && t[k + 1].kind == Tk::Ident) {
            Symbol s; s.name = t[k + 1].text; s.kind = SymKind::Constant; s.line = ln;
            size_t v = k + 2;
            if (isOp(t, v, "<-") || isOp(t, v, "=")) s.signature = "CONSTANT " + s.name + " <- " + slice(line, t, v + 1, t.size());
            // infer a type from a literal value
            if (v + 1 < t.size()) {
                const LTok& lit = t[v + 1];
                if (lit.kind == Tk::Number) s.type = lit.text.find('.') != std::string::npos ? "REAL" : "INTEGER";
                else if (lit.kind == Tk::String) s.type = "STRING"; else if (lit.kind == Tk::Char) s.type = "CHAR";
                else if (lit.kind == Tk::Constant) s.type = "BOOLEAN";
            }
            if (const Ctx* r = currentRoutine()) { s.owner = r->name; s.scopeStart = idx.syms_[r->symIndex].line; }
            idx.syms_.push_back(s);
            continue;
        }

        // DECLARE a, b : TYPE   — or, inside CLASS / TYPE, a bare   [PUBLIC|PRIVATE] a, b : TYPE
        size_t names = std::string::npos;
        if (kw == "DECLARE") names = k + 1;
        else if (kw.empty() && !ctx.empty() && ctx.back().k != Ctx::Routine && k < t.size() && t[k].kind == Tk::Ident) names = k;
        if (names != std::string::npos) {
            std::vector<const LTok*> ids;
            size_t i = names;
            for (; i < t.size() && !isOp(t, i, ":"); ++i) if (t[i].kind == Tk::Ident) ids.push_back(&t[i]);
            if (i >= t.size()) continue;   // no colon yet: still being typed
            std::string type = slice(line, t, i + 1, t.size());
            const Ctx* in = innermost();
            for (const LTok* id : ids) {
                Symbol s; s.name = id->text; s.type = type; s.line = ln; s.isPrivate = priv;
                s.signature = s.name + " : " + type;
                if (in && in->k != Ctx::Routine) { s.kind = SymKind::Field; s.owner = in->name; }
                else if (in) { s.kind = SymKind::Variable; s.owner = in->name; s.scopeStart = idx.syms_[in->symIndex].line; }
                else s.kind = SymKind::Variable;
                idx.syms_.push_back(s);
            }
            continue;
        }
    }
    // Anything still open runs to the end of the file.
    for (const Ctx& c : ctx) idx.syms_[c.symIndex].scopeEnd = idx.lastLine_;
    for (Symbol& s : idx.syms_)
        if (s.scopeStart >= 0 && s.scopeEnd < 0 && (s.kind == SymKind::Parameter || s.kind == SymKind::Variable || s.kind == SymKind::Constant) && !s.owner.empty())
            s.scopeEnd = idx.lastLine_;
    return idx;
}

const Symbol* SymbolIndex::enclosingClass(int line) const {
    const Symbol* best = nullptr;
    for (const Symbol& s : syms_) if (s.kind == SymKind::Class && s.scopeStart <= line && line <= s.scopeEnd) best = &s;
    return best;
}

const Symbol* SymbolIndex::enclosingRoutine(int line) const {
    const Symbol* best = nullptr;
    for (const Symbol& s : syms_)
        if ((s.kind == SymKind::Procedure || s.kind == SymKind::Function || s.kind == SymKind::Method) && s.scopeStart <= line && line <= s.scopeEnd) best = &s;
    return best;
}

const Symbol* SymbolIndex::findType(const std::string& name) const {
    std::string key = toLower(name);
    for (const Symbol& s : syms_)
        if ((s.kind == SymKind::Class || s.kind == SymKind::Record || s.kind == SymKind::Enum || s.kind == SymKind::Pointer || s.kind == SymKind::Alias) &&
            toLower(s.name) == key) return &s;
    return nullptr;
}

const Symbol* SymbolIndex::findRoutine(const std::string& name) const {
    std::string key = toLower(name);
    for (const Symbol& s : syms_)
        if ((s.kind == SymKind::Procedure || s.kind == SymKind::Function) && toLower(s.name) == key) return &s;
    return nullptr;
}

std::vector<const Symbol*> SymbolIndex::membersOf(const std::string& typeName) const {
    std::vector<const Symbol*> out;
    std::set<std::string> seen;
    std::string cur = typeName;
    for (int hop = 0; hop < 16 && !cur.empty(); ++hop) {
        const Symbol* ty = findType(cur);
        if (!ty) break;
        if (ty->kind == SymKind::Alias) { cur = resolveAlias(cur); if (cur.empty()) break; ty = findType(cur); if (!ty) break; }
        if (ty->kind != SymKind::Class && ty->kind != SymKind::Record) break;
        for (const Symbol& s : syms_)
            if ((s.kind == SymKind::Field || s.kind == SymKind::Method) && toLower(s.owner) == toLower(ty->name) && seen.insert(toLower(s.name)).second)
                out.push_back(&s);
        cur = ty->parent;
    }
    return out;
}

const Symbol* SymbolIndex::findMember(const std::string& typeName, const std::string& member) const {
    std::string key = toLower(member);
    for (const Symbol* m : membersOf(typeName)) if (toLower(m->name) == key) return m;
    return nullptr;
}

std::vector<const Symbol*> SymbolIndex::visibleAt(int line) const {
    std::vector<const Symbol*> out;
    std::set<std::string> seen;
    auto add = [&](const Symbol* s) { if (seen.insert(toLower(s->name)).second) out.push_back(s); };
    // locals and parameters of the enclosing routine
    for (size_t i = syms_.size(); i-- > 0;) {
        const Symbol& s = syms_[i];
        if ((s.kind == SymKind::Parameter || ((s.kind == SymKind::Variable || s.kind == SymKind::Constant) && !s.owner.empty())) &&
            s.scopeStart <= line && line <= s.scopeEnd) add(&s);
    }
    // members of the enclosing class
    if (const Symbol* cls = enclosingClass(line)) for (const Symbol* m : membersOf(cls->name)) add(m);
    // globals
    for (const Symbol& s : syms_) {
        bool global = (s.kind == SymKind::Variable || s.kind == SymKind::Constant) && s.owner.empty();
        bool decl = s.kind == SymKind::Procedure || s.kind == SymKind::Function || s.kind == SymKind::Class || s.kind == SymKind::Record ||
                    s.kind == SymKind::Enum || s.kind == SymKind::Pointer || s.kind == SymKind::Alias || s.kind == SymKind::EnumValue;
        if (global || decl) add(&s);
    }
    return out;
}

std::string SymbolIndex::resolveAlias(const std::string& typeText, int depth) const {
    std::string name = typeText;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (depth > 8) return name;
    const Symbol* ty = findType(name);
    if (ty && ty->kind == SymKind::Alias) return resolveAlias(ty->type, depth + 1);
    return name;
}

std::string SymbolIndex::elementType(const std::string& typeText) const {
    // "ARRAY[1:10] OF Player" -> "Player"  (nested arrays: take the innermost element type)
    std::string s = typeText;
    for (;;) {
        std::string up = toUpper(s);
        size_t a = up.find("ARRAY");
        if (a != 0) return s;
        size_t of = up.find(" OF ");
        if (of == std::string::npos) return "";
        s = s.substr(of + 4);
        while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    }
}

std::string SymbolIndex::typeOfPath(const std::string& path, int line) const {
    Toks t = scanLine(path);
    if (t.empty()) return "";
    size_t i = 0;
    std::string type;
    // first element
    if (t[0].kind == Tk::Keyword && t[0].text == "SUPER") {
        const Symbol* c = enclosingClass(line);
        if (!c || c->parent.empty()) return "";
        type = c->parent; i = 1;
    } else if (t[0].kind == Tk::Ident) {
        const Symbol* found = nullptr;
        for (const Symbol* s : visibleAt(line))
            if (toLower(s->name) == toLower(t[0].text)) { found = s; break; }
        if (!found) return "";
        if (found->kind == SymKind::Procedure || found->kind == SymKind::Method) return "";
        type = found->type; i = 1;
    } else return "";

    bool indexed = false;
    while (i < t.size()) {
        if (isOp(t, i, "[")) {
            size_t depth = 1, j = i + 1;
            while (j < t.size() && depth) { if (isOp(t, j, "[")) ++depth; else if (isOp(t, j, "]")) --depth; ++j; }
            type = elementType(type); i = j; indexed = true; continue;
        }
        if (isOp(t, i, "(")) {   // function result: symbol->type already holds the return type
            size_t depth = 1, j = i + 1;
            while (j < t.size() && depth) { if (isOp(t, j, "(")) ++depth; else if (isOp(t, j, ")")) --depth; ++j; }
            i = j; continue;
        }
        if (isOp(t, i, "^")) {
            const Symbol* p = findType(resolveAlias(type));
            if (p && p->kind == SymKind::Pointer) type = p->type; else return "";
            ++i; continue;
        }
        if (isOp(t, i, ".") && i + 1 < t.size() && (t[i + 1].kind == Tk::Ident || t[i + 1].kind == Tk::Keyword)) {
            std::string base = resolveAlias(type);
            const Symbol* m = findMember(base, t[i + 1].text);
            if (!m) return "";
            type = m->type; i += 2; continue;
        }
        return "";
    }
    (void)indexed;
    type = resolveAlias(type);
    if (toUpper(type).rfind("ARRAY", 0) == 0) return "";
    return type;
}

std::set<std::string> SymbolIndex::userNamesLower() const {
    std::set<std::string> n;
    for (const Symbol& s : syms_) n.insert(toLower(s.name));
    return n;
}

std::vector<const Symbol*> SymbolIndex::outline() const {
    std::vector<const Symbol*> o;
    for (const Symbol& s : syms_) {
        switch (s.kind) {
            case SymKind::Class: case SymKind::Record: case SymKind::Enum: case SymKind::Pointer: case SymKind::Alias:
            case SymKind::Procedure: case SymKind::Function: case SymKind::Method: o.push_back(&s); break;
            case SymKind::Variable: case SymKind::Constant: if (s.owner.empty()) o.push_back(&s); break;
            default: break;
        }
    }
    return o;
}

} // namespace studio
