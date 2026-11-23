#include "semantic/semantic.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include "stdlib/builtins.hpp"

namespace pcs {
namespace {

enum class ST { Unknown, Int, Real, Str, Char, Bool, Date, Other };

const char* stName(ST t) {
    switch (t) { case ST::Int: return "INTEGER"; case ST::Real: return "REAL"; case ST::Str: return "STRING"; case ST::Char: return "CHAR";
        case ST::Bool: return "BOOLEAN"; case ST::Date: return "DATE"; default: return "value"; }
}
bool isNum(ST t) { return t == ST::Int || t == ST::Real; }

struct Var { ST type = ST::Unknown; bool isConst = false, used = false, isArray = false; int line = 0, col = 0; std::string name; const TypeSpec* spec = nullptr; };
using Scope = std::unordered_map<std::string, Var>;

struct Analyzer {
    const Program& prog;
    std::vector<Diagnostic> out;
    std::unordered_map<std::string, const Routine*> routines;
    std::unordered_map<std::string, const TypeDef*> types;
    std::unordered_map<std::string, const ClassDef*> classes;
    Scope globals;
    Scope* locals = nullptr;
    const ClassDef* curClass = nullptr;
    const Routine* curRoutine = nullptr;

    explicit Analyzer(const Program& p) : prog(p) {}

    void err(const char* code, const char* cat, int line, int col, const std::string& msg, const std::string& hint = "") {
        out.push_back(makeDiag(Severity::Error, code, cat, prog.file, line, col, msg, hint));
    }
    void warn(const char* code, int line, int col, const std::string& msg, const std::string& hint = "") {
        out.push_back(makeDiag(Severity::Warning, code, "Code quality", prog.file, line, col, msg, hint));
    }

    ST fromSpec(const TypeSpec& ts) {
        if (ts.isArray || ts.isPtr) return ST::Other;
        if (ts.key == "integer") return ST::Int;
        if (ts.key == "real") return ST::Real;
        if (ts.key == "string") return ST::Str;
        if (ts.key == "char") return ST::Char;
        if (ts.key == "boolean") return ST::Bool;
        if (ts.key == "date") return ST::Date;
        return ST::Other;
    }
    void checkSpec(const TypeSpec& ts) {
        if (ts.isArray) { for (auto& d : ts.dims) { expr(*d.first); expr(*d.second); } checkSpec(*ts.elem); return; }
        if (ts.isPtr) { checkSpec(*ts.elem); return; }
        static const std::set<std::string> base = {"integer", "real", "string", "char", "boolean", "date"};
        if (base.count(ts.key) || types.count(ts.key) || classes.count(ts.key)) return;
        err("E208", "Semantic error", ts.line, ts.col, "Unknown type '" + ts.name + "'",
            "Define it with TYPE ... ENDTYPE or CLASS ... ENDCLASS first, or use INTEGER, REAL, STRING, CHAR, BOOLEAN or DATE.");
    }

    Var* find(const std::string& key) {
        if (locals) { auto it = locals->find(key); if (it != locals->end()) return &it->second; }
        // class fields (incl. inherited)
        for (const ClassDef* c = curClass; c;) {
            for (const auto& f : c->fields) if (f.key == key) { static Var fv; fv = Var(); fv.type = fromSpec(f.type); fv.used = true; fv.spec = &f.type; fv.isArray = f.type.isArray; return &fv; }
            if (c->parentKey.empty()) break;
            auto it = classes.find(c->parentKey); c = it == classes.end() ? nullptr : it->second;
        }
        auto g = globals.find(key); if (g != globals.end()) return &g->second;
        return nullptr;
    }

    bool hasMethod(const ClassDef* c, const std::string& key) {
        int guard = 0;
        while (c && guard++ < 1000) {
            for (const auto& m : c->methods) if (m->key == key) return true;
            if (c->parentKey.empty()) return false;
            auto it = classes.find(c->parentKey); c = it == classes.end() ? nullptr : it->second;
        }
        return false;
    }
    const Routine* findMethod(const ClassDef* c, const std::string& key) {
        int guard = 0;
        while (c && guard++ < 1000) {
            for (const auto& m : c->methods) if (m->key == key) return m.get();
            if (c->parentKey.empty()) return nullptr;
            auto it = classes.find(c->parentKey); c = it == classes.end() ? nullptr : it->second;
        }
        return nullptr;
    }

    ST expr(const Expr& e) {
        switch (e.k) {
            case Expr::IntLit: return ST::Int; case Expr::RealLit: return ST::Real; case Expr::StrLit: return ST::Str;
            case Expr::CharLit: return ST::Char; case Expr::BoolLit: return ST::Bool;
            case Expr::Var: {
                Var* v = find(e.key);
                if (!v) { err("E201", "Semantic error", e.line, e.col, "Variable '" + e.s + "' has not been declared", "Add DECLARE " + e.s + " : TYPE before using it (check the spelling too)."); return ST::Unknown; }
                v->used = true; return v->isArray ? ST::Other : v->type;
            }
            case Expr::Index: {
                ST r = ST::Unknown;
                if (e.a->k == Expr::Var) {
                    Var* v = find(e.a->key);
                    if (!v) { err("E201", "Semantic error", e.a->line, e.a->col, "Variable '" + e.a->s + "' has not been declared", "Add DECLARE " + e.a->s + " : ARRAY[lower:upper] OF TYPE."); }
                    else { v->used = true;
                        if (v->spec && v->spec->isArray) r = fromSpec(*v->spec->elem);
                        else if (v->spec && !v->spec->isArray && !v->spec->isPtr && v->type != ST::Other && v->type != ST::Unknown)
                            err("E203", "Semantic error", e.line, e.col, "'" + e.a->s + "' is a " + std::string(stName(v->type)) + ", not an array, so it cannot be indexed"); }
                } else expr(*e.a);
                for (const auto& a : e.args) { ST t = expr(*a); if (t != ST::Unknown && t != ST::Int) err("E203", "Semantic error", a->line, a->col, std::string("An array index must be an INTEGER but this is a ") + stName(t)); }
                return r;
            }
            case Expr::Field: expr(*e.a); return ST::Unknown;
            case Expr::Deref: expr(*e.a); return ST::Unknown;
            case Expr::AddrOf: expr(*e.a); return ST::Other;
            case Expr::New: {
                if (!classes.count(e.key)) err("E208", "Semantic error", e.line, e.col, "Unknown class '" + e.s + "'", "Define it with CLASS ... ENDCLASS first.");
                else { const Routine* ctor = findMethod(classes[e.key], "new");
                    if (ctor && ctor->params.size() != e.args.size()) err("E205", "Semantic error", e.line, e.col, "The constructor of " + e.s + " expects " + std::to_string(ctor->params.size()) + " argument(s) but " + std::to_string(e.args.size()) + " were given");
                    if (!ctor && !e.args.empty()) err("E205", "Semantic error", e.line, e.col, "Class " + e.s + " has no constructor, so NEW takes no arguments"); }
                for (const auto& a : e.args) expr(*a);
                return ST::Other;
            }
            case Expr::SuperCall: {
                if (!curClass || curClass->parentKey.empty()) err("E207", "Semantic error", e.line, e.col, "SUPER can only be used in a method of a class that INHERITS from another class");
                for (const auto& a : e.args) expr(*a);
                return ST::Unknown;
            }
            case Expr::MethodCall: { expr(*e.a); for (const auto& a : e.args) expr(*a); return ST::Unknown; }
            case Expr::Call: return call(e, true);
            case Expr::Unary: {
                ST t = expr(*e.a);
                if (e.op == Op::Not) { if (t != ST::Unknown && t != ST::Bool) err("E203", "Semantic error", e.line, e.col, std::string("NOT needs a BOOLEAN but this is a ") + stName(t)); return ST::Bool; }
                if (t != ST::Unknown && !isNum(t)) err("E203", "Semantic error", e.line, e.col, std::string("Unary minus needs a number but this is a ") + stName(t));
                return t;
            }
            case Expr::Binary: {
                ST a = expr(*e.a), b = expr(*e.b);
                auto bad = [&](const std::string& m, const std::string& h = "") { err("E203", "Semantic error", e.line, e.col, m, h); };
                switch (e.op) {
                    case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Pow: case Op::IDiv: case Op::Mod:
                        if ((a != ST::Unknown && !isNum(a)) || (b != ST::Unknown && !isNum(b)))
                            bad(std::string("This arithmetic operator needs numbers but received ") + stName(a) + " and " + stName(b),
                                e.op == Op::Add && (a == ST::Str || a == ST::Char) ? "Use & to join strings." : "");
                        if (e.op == Op::Div || e.op == Op::Pow) return ST::Real;
                        if (e.op == Op::IDiv || e.op == Op::Mod) { if (a == ST::Real || b == ST::Real) bad("DIV and MOD need INTEGER values"); return ST::Int; }
                        return (a == ST::Real || b == ST::Real) ? ST::Real : (a == ST::Int && b == ST::Int ? ST::Int : ST::Unknown);
                    case Op::Concat:
                        if ((a != ST::Unknown && a != ST::Str && a != ST::Char) || (b != ST::Unknown && b != ST::Str && b != ST::Char))
                            bad(std::string("& joins STRING or CHAR values but received ") + stName(a) + " and " + stName(b), "Convert numbers with NUM_TO_STR().");
                        return ST::Str;
                    case Op::And: case Op::Or:
                        if ((a != ST::Unknown && a != ST::Bool) || (b != ST::Unknown && b != ST::Bool)) bad("AND/OR need BOOLEAN operands");
                        return ST::Bool;
                    default: {
                        bool ta = a == ST::Str || a == ST::Char, tb = b == ST::Str || b == ST::Char;
                        if (a != ST::Unknown && b != ST::Unknown && a != ST::Other && b != ST::Other &&
                            !(isNum(a) && isNum(b)) && !(ta && tb) && a != b)
                            bad(std::string("Cannot compare ") + stName(a) + " with " + stName(b));
                        return ST::Bool;
                    }
                }
            }
        }
        return ST::Unknown;
    }

    ST call(const Expr& e, bool wantValue) {
        if (curClass) {
            if (const Routine* m = findMethod(curClass, e.key)) { checkArgs(e, m->params.size(), m->name, m->isFunc, wantValue); return m->isFunc && m->ret ? fromSpec(*m->ret) : ST::Unknown; }
        }
        auto it = routines.find(e.key);
        if (it != routines.end()) { checkArgs(e, it->second->params.size(), it->second->name, it->second->isFunc, wantValue); return it->second->isFunc ? fromSpec(*it->second->ret) : ST::Unknown; }
        if (const BuiltinInfo* b = findBuiltin(e.key)) {
            int n = static_cast<int>(e.args.size());
            if (n < b->minArgs || n > b->maxArgs) err("E205", "Semantic error", e.line, e.col, e.s + " expects " + std::to_string(b->minArgs) + (b->minArgs == b->maxArgs ? "" : ".." + std::to_string(b->maxArgs)) + " argument(s) but " + std::to_string(n) + " were given");
            for (const auto& a : e.args) expr(*a);
            if (!wantValue) err("E204", "Semantic error", e.line, e.col, e.s + " is a function that returns a value; it cannot be used as a statement", "Assign its result to a variable or use it in an expression.");
            switch (b->ret) { case 'i': return ST::Int; case 'r': return ST::Real; case 's': return ST::Str; case 'c': return ST::Char; case 'b': return ST::Bool; case 'd': return ST::Date; default: return ST::Unknown; }
        }
        for (const auto& a : e.args) expr(*a);
        err("E204", "Semantic error", e.line, e.col, "Procedure or function '" + e.s + "' is not defined", "Check the spelling, or define it with PROCEDURE/FUNCTION.");
        return ST::Unknown;
    }
    void checkArgs(const Expr& e, size_t want, const std::string& name, bool isFunc, bool wantValue) {
        if (e.args.size() != want)
            err("E205", "Semantic error", e.line, e.col, std::string(isFunc ? "Function " : "Procedure ") + name + " expects " + std::to_string(want) + " argument(s) but " + std::to_string(e.args.size()) + " were given");
        for (const auto& a : e.args) expr(*a);
        if (wantValue && !isFunc) err("E204", "Semantic error", e.line, e.col, "Procedure " + name + " does not return a value, so it cannot be used in an expression", "Use CALL " + name + "(...) as a statement, or make it a FUNCTION.");
        if (!wantValue && isFunc) err("E204", "Semantic error", e.line, e.col, "Function " + name + " returns a value; use it in an expression or assign it", "Write result <- " + name + "(...).");
    }

    void block(const std::vector<StmtP>& b) { for (const auto& s : b) stmt(*s); }

    void declare(Scope& sc, const std::string& key, const std::string& name, ST t, bool isConst, int line, int col, const TypeSpec* spec = nullptr) {
        if (sc.count(key)) { err("E202", "Semantic error", line, col, "'" + name + "' is already declared in this scope", "Use a different name."); return; }
        Var v; v.type = t; v.isConst = isConst; v.line = line; v.col = col; v.name = name; v.spec = spec; v.isArray = spec && spec->isArray;
        sc[key] = v;
    }
    Scope& scope() { return locals ? *locals : globals; }

    void target(const Expr& e, bool assigning) {
        if (e.k == Expr::Var) {
            Var* v = find(e.key);
            if (!v) { err("E201", "Semantic error", e.line, e.col, "Variable '" + e.s + "' has not been declared", "Add DECLARE " + e.s + " : TYPE before using it (check the spelling too)."); return; }
            v->used = true;
            if (assigning && v->isConst) err("E206", "Semantic error", e.line, e.col, "'" + e.s + "' is a constant and cannot be changed");
        } else expr(e);
    }

    void stmt(const Stmt& s) {
        switch (s.k) {
            case Stmt::Declare:
                checkSpec(*s.tspec);
                for (size_t i = 0; i < s.names.size(); ++i) declare(scope(), s.keys[i], s.names[i], fromSpec(*s.tspec), false, s.line, s.col, s.tspec.get());
                break;
            case Stmt::Constant: { ST t = expr(*s.e1); declare(scope(), s.keys[0], s.names[0], t, true, s.line, s.col); scope()[s.keys[0]].used = true; break; }
            case Stmt::Assign: {
                ST tt = ST::Unknown;
                if (s.e1->k == Expr::Var) { Var* v = find(s.e1->key); if (v && !v->isArray) tt = v->type; if (v && v->spec && v->spec->isArray) tt = ST::Other; }
                else if (s.e1->k == Expr::Index) tt = expr(*s.e1);
                target(*s.e1, true);
                ST vt = expr(*s.e2);
                bool ok = tt == ST::Unknown || vt == ST::Unknown || tt == ST::Other || vt == ST::Other || tt == vt || (tt == ST::Real && vt == ST::Int) || (tt == ST::Str && vt == ST::Char);
                if (!ok) err("E203", "Semantic error", s.e2->line, s.e2->col, std::string("Type mismatch: cannot assign a ") + stName(vt) + " value to a variable of type " + stName(tt),
                             tt == ST::Int && vt == ST::Real ? "Use INT(value) to convert a REAL to an INTEGER." : tt == ST::Char && vt == ST::Str ? "Use single quotes for a CHAR, e.g. 'A'." : "");
                break;
            }
            case Stmt::Input: target(*s.e1, true); break;
            case Stmt::Output: for (const auto& a : s.args) expr(*a); break;
            case Stmt::If: { ST t = expr(*s.e1); if (t != ST::Unknown && t != ST::Bool && t != ST::Other) err("E209", "Semantic error", s.e1->line, s.e1->col, std::string("An IF condition must be BOOLEAN but this is a ") + stName(t), "Use a comparison such as x > 0."); block(s.body); block(s.elseBody); break; }
            case Stmt::Case: {
                expr(*s.e1);
                for (const auto& b : s.branches) { for (const auto& v : b.vals) { expr(*v.first); if (v.second) expr(*v.second); } block(b.body); }
                block(s.otherwise); break;
            }
            case Stmt::For: {
                Var* v = find(s.e1->key);
                if (!v) err("E201", "Semantic error", s.e1->line, s.e1->col, "Loop variable '" + s.e1->s + "' has not been declared", "Add DECLARE " + s.e1->s + " : INTEGER before the loop.");
                else { v->used = true; if (v->type != ST::Unknown && v->type != ST::Int) err("E203", "Semantic error", s.e1->line, s.e1->col, "The FOR loop variable must be an INTEGER"); if (v->isConst) err("E206", "Semantic error", s.e1->line, s.e1->col, "A constant cannot be a loop variable"); }
                for (const Expr* x : {s.e2.get(), s.e3.get(), s.e4.get()}) if (x) { ST t = expr(*x); if (t != ST::Unknown && t != ST::Int) err("E203", "Semantic error", x->line, x->col, std::string("FOR bounds and STEP must be INTEGER but this is a ") + stName(t)); }
                block(s.body); break;
            }
            case Stmt::While: { ST t = expr(*s.e1); if (t != ST::Unknown && t != ST::Bool && t != ST::Other) err("E209", "Semantic error", s.e1->line, s.e1->col, std::string("A WHILE condition must be BOOLEAN but this is a ") + stName(t)); block(s.body); break; }
            case Stmt::Repeat: { block(s.body); ST t = expr(*s.e1); if (t != ST::Unknown && t != ST::Bool && t != ST::Other) err("E209", "Semantic error", s.e1->line, s.e1->col, std::string("An UNTIL condition must be BOOLEAN but this is a ") + stName(t)); break; }
            case Stmt::Call:
                if (s.e1->k == Expr::Call) call(*s.e1, false); else expr(*s.e1);
                break;
            case Stmt::Return:
                if (!curRoutine || !curRoutine->isFunc) err("E207", "Semantic error", s.line, s.col, "RETURN can only be used inside a FUNCTION");
                else { ST t = expr(*s.e1), want = fromSpec(*curRoutine->ret);
                    if (t != ST::Unknown && want != ST::Other && want != ST::Unknown && t != ST::Other && t != want && !(want == ST::Real && t == ST::Int))
                        err("E203", "Semantic error", s.e1->line, s.e1->col, std::string("Function ") + curRoutine->name + " returns " + stName(want) + " but this value is " + stName(t)); }
                break;
            case Stmt::ProcDef: routine(*s.routine, nullptr); break;
            case Stmt::TypeDefn: {
                const TypeDef& td = *s.tdef;
                for (const auto& f : td.fields) checkSpec(f.type);
                if (td.target) checkSpec(*td.target);
                break;
            }
            case Stmt::ClassDefn: {
                const ClassDef& c = *s.cls;
                if (!c.parentKey.empty() && !classes.count(c.parentKey)) err("E208", "Semantic error", c.line, c.col, "Class " + c.name + " inherits from unknown class '" + c.parentName + "'");
                for (const auto& f : c.fields) checkSpec(f.type);
                for (const auto& m : c.methods) routine(*m, &c);
                break;
            }
            case Stmt::OpenFile: case Stmt::CloseFile: expr(*s.e1); break;
            case Stmt::ReadFile: case Stmt::GetRecord: expr(*s.e1); target(*s.e2, true); break;
            case Stmt::WriteFile: case Stmt::Seek: case Stmt::PutRecord: expr(*s.e1); expr(*s.e2); break;
        }
    }

    void routine(const Routine& r, const ClassDef* cls) {
        Scope sc; Scope* savedL = locals; const ClassDef* savedC = curClass; const Routine* savedR = curRoutine;
        locals = &sc; curClass = cls; curRoutine = &r;
        for (const auto& p : r.params) { checkSpec(p.type); declare(sc, p.key, p.name, fromSpec(p.type), false, r.line, r.col, &p.type); sc[p.key].used = true; }
        if (r.ret) checkSpec(*r.ret);
        block(r.body);
        for (const auto& kv : sc) if (!kv.second.used && !kv.second.isConst) warn("W501", kv.second.line, kv.second.col, "Variable '" + kv.second.name + "' is declared but never used");
        locals = savedL; curClass = savedC; curRoutine = savedR;
    }

    void run() {
        for (const auto& s : prog.stmts) {
            if (s->k == Stmt::ProcDef) {
                if (routines.count(s->routine->key)) err("E202", "Semantic error", s->line, s->col, "Procedure or function '" + s->routine->name + "' is defined twice");
                routines[s->routine->key] = s->routine.get();
            } else if (s->k == Stmt::TypeDefn) {
                if (types.count(s->tdef->key) || classes.count(s->tdef->key)) err("E202", "Semantic error", s->line, s->col, "Type '" + s->tdef->name + "' is defined twice");
                types[s->tdef->key] = s->tdef.get();
                if (s->tdef->kind == TypeDef::Enum)
                    for (const auto& n : s->tdef->enumNames) { std::string k; for (char c : n) k += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); declare(globals, k, n, ST::Other, true, s->line, s->col); globals[k].used = true; }
            } else if (s->k == Stmt::ClassDefn) {
                if (types.count(s->cls->key) || classes.count(s->cls->key)) err("E202", "Semantic error", s->line, s->col, "Class '" + s->cls->name + "' is defined twice");
                classes[s->cls->key] = s->cls.get();
            }
        }
        for (const auto& s : prog.stmts) stmt(*s);
        for (const auto& kv : globals) if (!kv.second.used && !kv.second.isConst) warn("W501", kv.second.line, kv.second.col, "Variable '" + kv.second.name + "' is declared but never used");
    }
};

} // namespace

std::vector<Diagnostic> analyze(const Program& prog) {
    Analyzer a(prog); a.run();
    std::stable_sort(a.out.begin(), a.out.end(), [](const Diagnostic& x, const Diagnostic& y) { return x.line != y.line ? x.line < y.line : x.col < y.col; });
    return a.out;
}
bool hasErrors(const std::vector<Diagnostic>& d) { for (const auto& x : d) if (x.sev == Severity::Error) return true; return false; }

} // namespace pcs
