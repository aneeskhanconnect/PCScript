#include "runtime/interpreter.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <unordered_map>
#include "runtime/value.hpp"
#include "stdlib/builtins.hpp"

namespace pcs {

struct ClassInfo {
    const ClassDef* def = nullptr;
    ClassInfo* parent = nullptr;
    std::unordered_map<std::string, const Routine*> methods;
    std::unordered_map<std::string, const FieldDecl*> fields;
};

namespace {
enum class Flow { Normal, Return };

struct Frame {
    std::unordered_map<std::string, CellP> vars;
    std::shared_ptr<ObjectData> self;
    const ClassInfo* cls = nullptr;
    Value ret;
    TypePtr retType;
    bool isFunc = false, returned = false;
    std::string name;
};

struct LRef {
    Value* v = nullptr;
    TypePtr t;
    bool isConst = false;
    std::shared_ptr<void> keep;
};

struct FileState {
    int mode = 0;
    std::ifstream in;
    std::ofstream out;
    std::vector<std::string> recs;   // RANDOM files: one record per line
    size_t pos = 1;                  // RANDOM: next record number (1-based)
    std::string path;
};

TypePtr basic(Type::K k, const char* name) { auto t = std::make_shared<Type>(); t->k = k; t->name = name; return t; }
}

struct Interpreter::Impl {
    const Program& prog;
    std::istream& in;
    std::ostream& out;
    Options opt;
    std::unordered_map<std::string, const Routine*> routines;
    std::unordered_map<std::string, const TypeDef*> typeDefs;
    std::unordered_map<std::string, TypePtr> typeCache;
    std::unordered_map<std::string, std::unique_ptr<ClassInfo>> classes;
    std::unordered_map<std::string, std::unique_ptr<FileState>> files;
    Frame globals;
    std::mt19937_64 rng;
    int depth = 0;
    unsigned long long ticks = 0;
    std::chrono::steady_clock::time_point started;
    TypePtr tInt = basic(Type::Int, "INTEGER"), tReal = basic(Type::Real, "REAL"), tStr = basic(Type::Str, "STRING"),
            tChar = basic(Type::Char, "CHAR"), tBool = basic(Type::Bool, "BOOLEAN"), tDate = basic(Type::Date, "DATE");

    Impl(const Program& p, std::istream& i, std::ostream& o, Options op) : prog(p), in(i), out(o), opt(std::move(op)) {
        std::random_device rd;
        rng.seed(opt.hasSeed ? opt.seed : (static_cast<unsigned long long>(rd()) << 32) ^ rd());
    }

    // ---------- errors ----------
    [[noreturn]] void rt(const std::string& code, const std::string& msg, int line, int col, const std::string& hint = "") {
        throw PcsError(makeDiag(Severity::Error, code, "Runtime error", prog.file, line, col, msg, hint));
    }
    [[noreturn]] void rt(const std::string& code, const std::string& msg, const Expr& e, const std::string& hint = "") { rt(code, msg, e.line, e.col, hint); }
    [[noreturn]] void rt(const std::string& code, const std::string& msg, const Stmt& s, const std::string& hint = "") { rt(code, msg, s.line, s.col, hint); }

    // ---------- setup ----------
    void hoist() {
        for (const auto& s : prog.stmts) {
            if (s->k == Stmt::ProcDef) {
                if (routines.count(s->routine->key)) rt("R313", "Procedure or function '" + s->routine->name + "' is defined twice", *s);
                routines[s->routine->key] = s->routine.get();
            } else if (s->k == Stmt::TypeDefn) {
                if (typeDefs.count(s->tdef->key)) rt("R313", "Type '" + s->tdef->name + "' is defined twice", *s);
                typeDefs[s->tdef->key] = s->tdef.get();
            } else if (s->k == Stmt::ClassDefn) {
                auto ci = std::make_unique<ClassInfo>(); ci->def = s->cls.get();
                for (const auto& m : s->cls->methods) ci->methods[m->key] = m.get();
                for (const auto& f : s->cls->fields) ci->fields[f.key] = &f;
                if (classes.count(s->cls->key)) rt("R313", "Class '" + s->cls->name + "' is defined twice", *s);
                classes[s->cls->key] = std::move(ci);
            }
        }
        for (auto& kv : classes) {
            const ClassDef* cd = kv.second->def;
            if (cd->parentKey.empty()) continue;
            auto it = classes.find(cd->parentKey);
            if (it == classes.end()) rt("R311", "Class '" + cd->name + "' inherits from unknown class '" + cd->parentName + "'", cd->line, cd->col);
            kv.second->parent = it->second.get();
        }
        for (auto& kv : classes) {
            int n = 0;
            for (ClassInfo* c = kv.second.get(); c; c = c->parent)
                if (++n > 1000) rt("R311", "Class '" + kv.second->def->name + "' inherits from itself", kv.second->def->line, kv.second->def->col);
        }
        for (const auto& kv : typeDefs) {
            if (kv.second->kind != TypeDef::Enum) continue;
            long long n = 0;
            for (const auto& nm : kv.second->enumNames) {
                auto c = std::make_shared<Cell>();
                c->v.t = VT::Enum; c->v.i = n++; c->v.s = kv.first; c->isConst = true;
                c->t = resolveNamed(kv.first, kv.second->line, kv.second->col);
                std::string key; for (char ch : nm) key += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                globals.vars[key] = c;
            }
        }
    }

    // ---------- types ----------
    long long evalInt(const Expr& e, Frame& f, const char* what) {
        Value v = eval(e, f);
        if (v.t != VT::Int) rt("R303", std::string(what) + " must be an INTEGER but is " + typeName(v), e);
        return v.i;
    }
    TypePtr resolveNamed(const std::string& key, int line, int col) {
        auto c = typeCache.find(key);
        if (c != typeCache.end()) return c->second;
        auto ci = classes.find(key);
        if (ci != classes.end()) {
            auto t = std::make_shared<Type>(); t->k = Type::Class; t->name = ci->second->def->name; t->cls = ci->second.get();
            typeCache[key] = t; return t;
        }
        auto td = typeDefs.find(key);
        if (td == typeDefs.end()) rt("R311", "Unknown type '" + key + "'", line, col);
        const TypeDef& d = *td->second;
        auto t = std::make_shared<Type>(); t->name = d.name;
        switch (d.kind) {
            case TypeDef::Enum: t->k = Type::Enum; t->enumNames = d.enumNames; typeCache[key] = t; return t;
            case TypeDef::Pointer: t->k = Type::Pointer; typeCache[key] = t; return t;
            case TypeDef::Alias: { auto r = resolveType(*d.target, globals); typeCache[key] = r; return r; }
            case TypeDef::Record:
                t->k = Type::Record; typeCache[key] = t;
                for (const auto& fd : d.fields) t->fields.emplace_back(fd.key, resolveType(fd.type, globals));
                return t;
        }
        return t;
    }
    TypePtr resolveType(const TypeSpec& ts, Frame& f) {
        if (ts.isArray) {
            auto t = std::make_shared<Type>(); t->k = Type::Array; t->name = "ARRAY";
            for (const auto& d : ts.dims) {
                long long lo = evalInt(*d.first, f, "Array lower bound"), hi = evalInt(*d.second, f, "Array upper bound");
                if (hi < lo) rt("R302", "Array upper bound " + std::to_string(hi) + " is smaller than lower bound " + std::to_string(lo), ts.line, ts.col);
                t->bounds.emplace_back(lo, hi);
            }
            t->elem = resolveType(*ts.elem, f); return t;
        }
        if (ts.isPtr) { auto t = std::make_shared<Type>(); t->k = Type::Pointer; t->name = "pointer"; return t; }
        if (ts.key == "integer") return tInt;
        if (ts.key == "real") return tReal;
        if (ts.key == "string") return tStr;
        if (ts.key == "char") return tChar;
        if (ts.key == "boolean") return tBool;
        if (ts.key == "date") return tDate;
        return resolveNamed(ts.key, ts.line, ts.col);
    }
    Value defaultValue(const TypePtr& t) {
        switch (t->k) {
            case Type::Int: return Value::Int(0);
            case Type::Real: return Value::Real(0);
            case Type::Str: return Value::Str("");
            case Type::Char: return Value::Char(' ');
            case Type::Bool: return Value::Bool(false);
            case Type::Date: return Value::Date(0);
            case Type::Enum: { Value v; v.t = VT::Enum; v.i = 0; v.s = t->name; for (char& c : v.s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return v; }
            case Type::Array: {
                auto a = std::make_shared<ArrayData>(); a->bounds = t->bounds; a->elem = t->elem;
                size_t n = 1; for (auto& b : t->bounds) n *= static_cast<size_t>(b.second - b.first + 1);
                a->data.reserve(n); for (size_t i = 0; i < n; ++i) a->data.push_back(defaultValue(t->elem));
                Value v; v.t = VT::Array; v.h = a; return v;
            }
            case Type::Record: {
                auto r = std::make_shared<RecordData>(); r->type = t;
                for (auto& f : t->fields) r->f[f.first] = defaultValue(f.second);
                Value v; v.t = VT::Record; v.h = r; return v;
            }
            case Type::Class: case Type::Pointer: return Value();
        }
        return Value();
    }
    static bool derives(const ClassInfo* c, const ClassInfo* base) { for (; c; c = c->parent) if (c == base) return true; return false; }
    static std::string tname(const TypePtr& t) { return t->k == Type::Array ? "ARRAY" : t->name; }

    // Check that v may be stored in a variable of type t; returns the (deep-copied, converted) value.
    Value coerce(const Value& v, const TypePtr& t, int line, int col, const char* ctx = "assign") {
        auto mismatch = [&]() -> Value {
            std::string hint;
            if (t->k == Type::Char && v.t == VT::Str) hint = "Use single quotes for a CHAR, e.g. 'A'.";
            if (t->k == Type::Int && v.t == VT::Real) hint = "Use INT(value) to convert a REAL to an INTEGER.";
            rt("R303", std::string("Type mismatch: cannot ") + ctx + " a value of type " + typeName(v) + " to " + tname(t), line, col, hint);
        };
        switch (t->k) {
            case Type::Int: if (v.t == VT::Int) return v; return mismatch();
            case Type::Real: if (v.t == VT::Real) return v; if (v.t == VT::Int) return Value::Real(static_cast<double>(v.i)); return mismatch();
            case Type::Str: if (v.t == VT::Str) return v; if (v.t == VT::Char) return Value::Str(valueToString(v)); return mismatch();
            case Type::Char: if (v.t == VT::Char) return v; return mismatch();
            case Type::Bool: if (v.t == VT::Bool) return v; return mismatch();
            case Type::Date: if (v.t == VT::Date) return v; return mismatch();
            case Type::Enum: {
                std::string k = t->name; for (char& c : k) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (v.t == VT::Enum && v.s == k) return v;
                return mismatch();
            }
            case Type::Array: {
                if (v.t != VT::Array) return mismatch();
                auto a = v.as<ArrayData>();
                if (!t->bounds.empty() && a->bounds != t->bounds) rt("R303", "Array shape mismatch: the bounds of the two arrays differ", line, col);
                if (a->elem->k != t->elem->k) rt("R303", "Array element type mismatch", line, col);
                return copyValue(v);
            }
            case Type::Record: if (v.t == VT::Record && v.as<RecordData>()->type->name == t->name) return copyValue(v); return mismatch();
            case Type::Class:
                if (v.t == VT::Null) return v;
                if (v.t == VT::Object && derives(v.as<ObjectData>()->cls, t->cls)) return v;
                return mismatch();
            case Type::Pointer: if (v.t == VT::Null || v.t == VT::Ptr) return v; return mismatch();
        }
        return v;
    }

    // ---------- lookup ----------
    CellP lookup(const std::string& key, Frame& f) {
        auto it = f.vars.find(key); if (it != f.vars.end()) return it->second;
        if (f.self) { auto o = f.self->f.find(key); if (o != f.self->f.end()) return o->second; }
        auto g = globals.vars.find(key); if (g != globals.vars.end()) return g->second;
        return nullptr;
    }
    const Routine* findMethod(const ClassInfo* c, const std::string& key, const ClassInfo*& owner) {
        for (; c; c = c->parent) { auto it = c->methods.find(key); if (it != c->methods.end()) { owner = c; return it->second; } }
        return nullptr;
    }
    const FieldDecl* findField(const ClassInfo* c, const std::string& key, const ClassInfo*& owner) {
        for (; c; c = c->parent) { auto it = c->fields.find(key); if (it != c->fields.end()) { owner = c; return it->second; } }
        return nullptr;
    }

    std::string str(const Value& v) {
        if (v.t == VT::Enum) {
            auto it = typeCache.find(v.s);
            if (it != typeCache.end() && v.i >= 0 && v.i < static_cast<long long>(it->second->enumNames.size())) return it->second->enumNames[static_cast<size_t>(v.i)];
        }
        return valueToString(v);
    }

    // ---------- l-values ----------
    LRef locateBase(const Expr& e, Frame& f) {
        if (e.k == Expr::Var || e.k == Expr::Index || e.k == Expr::Field || e.k == Expr::Deref) return locate(e, f);
        auto h = std::make_shared<Value>(eval(e, f));
        LRef r; r.v = h.get(); r.keep = h; return r;
    }
    LRef locate(const Expr& e, Frame& f) {
        switch (e.k) {
            case Expr::Var: {
                CellP c = lookup(e.key, f);
                if (!c) rt("R311", "Variable '" + e.s + "' has not been declared", e, "Add DECLARE " + e.s + " : TYPE before using it.");
                LRef r; r.v = &c->v; r.t = c->t; r.isConst = c->isConst; r.keep = c; return r;
            }
            case Expr::Index: {
                LRef b = locateBase(*e.a, f);
                if (b.v->t != VT::Array) rt("R303", "Only arrays can be indexed, but this is " + typeName(*b.v), e);
                ArrayData* a = b.v->as<ArrayData>();
                if (e.args.size() != a->bounds.size())
                    rt("R302", "This array has " + std::to_string(a->bounds.size()) + " dimension(s) but " + std::to_string(e.args.size()) + " index value(s) were given", e);
                size_t off = 0;
                for (size_t d = 0; d < e.args.size(); ++d) {
                    long long ix = evalInt(*e.args[d], f, "Array index");
                    long long lo = a->bounds[d].first, hi = a->bounds[d].second;
                    if (ix < lo || ix > hi)
                        rt("R302", "Array index " + std::to_string(ix) + " is out of bounds (valid range " + std::to_string(lo) + " to " + std::to_string(hi) + ")" +
                           (a->bounds.size() > 1 ? " in dimension " + std::to_string(d + 1) : ""), *e.args[d]);
                    off = off * static_cast<size_t>(hi - lo + 1) + static_cast<size_t>(ix - lo);
                }
                LRef r; r.v = &a->data[off]; r.t = a->elem; r.isConst = b.isConst;
                r.keep = std::make_shared<std::pair<std::shared_ptr<void>, std::shared_ptr<void>>>(b.keep, b.v->h);   // keep array alive
                return r;
            }
            case Expr::Field: {
                LRef b = locateBase(*e.a, f);
                if (b.v->t == VT::Record) {
                    RecordData* rd = b.v->as<RecordData>();
                    auto it = rd->f.find(e.key);
                    if (it == rd->f.end()) rt("R311", "The record type " + rd->type->name + " has no field '" + e.s + "'", e);
                    TypePtr ft; for (auto& p : rd->type->fields) if (p.first == e.key) ft = p.second;
                    LRef r; r.v = &it->second; r.t = ft; r.isConst = b.isConst;
                    r.keep = std::make_shared<std::pair<std::shared_ptr<void>, std::shared_ptr<void>>>(b.keep, b.v->h);
                    return r;
                }
                if (b.v->t == VT::Object) {
                    ObjectData* od = b.v->as<ObjectData>();
                    const ClassInfo* owner = nullptr;
                    const FieldDecl* fd = findField(od->cls, e.key, owner);
                    if (!fd) rt("R311", "The class " + od->cls->def->name + " has no field '" + e.s + "'", e);
                    if (fd->isPrivate && f.cls != owner)
                        rt("R308", "'" + e.s + "' is PRIVATE in class " + owner->def->name + " and cannot be accessed from outside it", e,
                           "Declare it PUBLIC, or add a public method that returns it.");
                    CellP c = od->f.at(e.key);
                    LRef r; r.v = &c->v; r.t = c->t; r.keep = c; return r;
                }
                if (b.v->t == VT::Null) rt("R305", "Cannot access field '" + e.s + "' because the object is not initialised", e, "Create it first with NEW.");
                rt("R303", "'" + e.s + "' cannot be accessed: " + typeName(*b.v) + " values have no fields", e);
            }
            case Expr::Deref: {
                Value p = eval(*e.a, f);
                if (p.t != VT::Ptr) rt("R305", "Cannot dereference a pointer that has not been assigned an address", e);
                CellP c = std::static_pointer_cast<Cell>(p.h);
                LRef r; r.v = &c->v; r.t = c->t; r.isConst = c->isConst; r.keep = c; return r;
            }
            default: rt("R303", "This expression cannot be assigned to or read as a variable", e);
        }
    }

    // ---------- expressions ----------
    bool evalBool(const Expr& e, Frame& f, const char* what) {
        Value v = eval(e, f);
        if (v.t != VT::Bool) rt("R303", std::string(what) + " must be a BOOLEAN but is " + typeName(v), e);
        return v.i != 0;
    }
    static bool numeric(const Value& v) { return v.t == VT::Int || v.t == VT::Real; }
    static double dbl(const Value& v) { return v.t == VT::Int ? static_cast<double>(v.i) : v.r; }
    static bool texty(const Value& v) { return v.t == VT::Str || v.t == VT::Char; }

    int compare(const Value& a, const Value& b, const Expr& e, bool& comparable) {
        comparable = true;
        if (a.t == VT::Int && b.t == VT::Int) return a.i < b.i ? -1 : a.i > b.i;
        if (numeric(a) && numeric(b)) { double x = dbl(a), y = dbl(b); return x < y ? -1 : x > y; }
        if (texty(a) && texty(b)) { int c = valueToString(a).compare(valueToString(b)); return c < 0 ? -1 : c > 0; }
        if (a.t == b.t && (a.t == VT::Bool || a.t == VT::Date || a.t == VT::Char)) return a.i < b.i ? -1 : a.i > b.i;
        if (a.t == VT::Enum && b.t == VT::Enum) {
            if (a.s != b.s) rt("R303", "Cannot compare values of different enumeration types", e);
            return a.i < b.i ? -1 : a.i > b.i;
        }
        if ((a.t == VT::Object || a.t == VT::Null || a.t == VT::Ptr) && (b.t == VT::Object || b.t == VT::Null || b.t == VT::Ptr)) {
            comparable = false; return a.h.get() == b.h.get() ? 0 : 1;   // identity only
        }
        rt("R303", "Cannot compare " + typeName(a) + " with " + typeName(b), e);
    }

    Value checkedInt(long long a, long long b, Op op, const Expr& e) {
        const long long mx = std::numeric_limits<long long>::max(), mn = std::numeric_limits<long long>::min();
        bool of = false; long long r = 0;
        if (op == Op::Add) { if ((b > 0 && a > mx - b) || (b < 0 && a < mn - b)) of = true; else r = a + b; }
        else if (op == Op::Sub) { if ((b < 0 && a > mx + b) || (b > 0 && a < mn + b)) of = true; else r = a - b; }
        else { long double p = static_cast<long double>(a) * static_cast<long double>(b); if (p > 9.2e18L || p < -9.2e18L) of = true; else r = a * b; }
        if (of) rt("R317", "INTEGER overflow: the result is too large to store", e, "Use REAL variables for very large numbers.");
        return Value::Int(r);
    }

    Value binary(const Expr& e, Frame& f) {
        if (e.op == Op::And) { if (!evalBool(*e.a, f, "AND operand")) return Value::Bool(false); return Value::Bool(evalBool(*e.b, f, "AND operand")); }
        if (e.op == Op::Or) { if (evalBool(*e.a, f, "OR operand")) return Value::Bool(true); return Value::Bool(evalBool(*e.b, f, "OR operand")); }
        Value a = eval(*e.a, f), b = eval(*e.b, f);
        auto opName = [&]() { switch (e.op) { case Op::Add: return "+"; case Op::Sub: return "-"; case Op::Mul: return "*"; case Op::Div: return "/";
            case Op::IDiv: return "DIV"; case Op::Mod: return "MOD"; case Op::Pow: return "^"; default: return "?"; } };
        switch (e.op) {
            case Op::Add: case Op::Sub: case Op::Mul:
                if (!numeric(a) || !numeric(b))
                    rt("R303", std::string("Operator ") + opName() + " cannot be applied to " + typeName(a) + " and " + typeName(b), e,
                       texty(a) && texty(b) && e.op == Op::Add ? "Use & to join strings." : "Arithmetic needs INTEGER or REAL values.");
                if (a.t == VT::Int && b.t == VT::Int) return checkedInt(a.i, b.i, e.op, e);
                return Value::Real(e.op == Op::Add ? dbl(a) + dbl(b) : e.op == Op::Sub ? dbl(a) - dbl(b) : dbl(a) * dbl(b));
            case Op::Div:
                if (!numeric(a) || !numeric(b)) rt("R303", "Operator / cannot be applied to " + typeName(a) + " and " + typeName(b), e);
                if (dbl(b) == 0) rt("R301", "Division by zero", e, "Check the divisor before dividing.");
                return Value::Real(dbl(a) / dbl(b));
            case Op::IDiv: case Op::Mod:
                if (a.t != VT::Int || b.t != VT::Int) rt("R303", std::string(opName()) + " needs two INTEGER values but received " + typeName(a) + " and " + typeName(b), e);
                if (b.i == 0) rt("R301", std::string(e.op == Op::Mod ? "MOD" : "DIV") + " by zero", e);
                if (a.i == std::numeric_limits<long long>::min() && b.i == -1) rt("R317", "INTEGER overflow", e);
                return Value::Int(e.op == Op::Mod ? a.i % b.i : a.i / b.i);
            case Op::Pow: {
                if (!numeric(a) || !numeric(b)) rt("R303", "Operator ^ cannot be applied to " + typeName(a) + " and " + typeName(b), e);
                double r = std::pow(dbl(a), dbl(b));
                if (std::isnan(r)) rt("R301", "Invalid power operation", e);
                return Value::Real(r);
            }
            case Op::Concat:
                if (!texty(a) || !texty(b)) rt("R303", "Operator & joins STRING or CHAR values but received " + typeName(a) + " and " + typeName(b), e, "Convert numbers first with NUM_TO_STR().");
                return Value::Str(valueToString(a) + valueToString(b));
            default: {
                bool cmp; int c = compare(a, b, e, cmp);
                if (!cmp && e.op != Op::Eq && e.op != Op::Ne) rt("R303", "Objects and pointers can only be compared with = and <>", e);
                switch (e.op) {
                    case Op::Eq: return Value::Bool(c == 0); case Op::Ne: return Value::Bool(c != 0);
                    case Op::Lt: return Value::Bool(c < 0); case Op::Gt: return Value::Bool(c > 0);
                    case Op::Le: return Value::Bool(c <= 0); default: return Value::Bool(c >= 0);
                }
            }
        }
    }

    Value eval(const Expr& e, Frame& f) {
        switch (e.k) {
            case Expr::IntLit: return Value::Int(e.i);
            case Expr::RealLit: return Value::Real(e.r);
            case Expr::StrLit: return Value::Str(e.s);
            case Expr::CharLit: return Value::Char(e.s[0]);
            case Expr::BoolLit: return Value::Bool(e.i != 0);
            case Expr::Var: case Expr::Index: case Expr::Deref: { LRef l = locate(e, f); return *l.v; }
            case Expr::Field: { LRef l = locate(e, f); return *l.v; }
            case Expr::Unary: {
                if (e.op == Op::Not) return Value::Bool(!evalBool(*e.a, f, "NOT operand"));
                Value v = eval(*e.a, f);
                if (v.t == VT::Int) { if (v.i == std::numeric_limits<long long>::min()) rt("R317", "INTEGER overflow", e); return Value::Int(-v.i); }
                if (v.t == VT::Real) return Value::Real(-v.r);
                rt("R303", "Unary minus needs a number but received " + typeName(v), e);
            }
            case Expr::Binary: return binary(e, f);
            case Expr::AddrOf: {
                if (e.a->k != Expr::Var) rt("R303", "The address operator ^ can only be applied to a simple variable", e);
                CellP c = lookup(e.a->key, f);
                if (!c) rt("R311", "Variable '" + e.a->s + "' has not been declared", *e.a);
                Value v; v.t = VT::Ptr; v.h = c; return v;
            }
            case Expr::New: {
                auto it = classes.find(e.key);
                if (it == classes.end()) rt("R311", "Unknown class '" + e.s + "'", e);
                return instantiate(it->second.get(), e, f);
            }
            case Expr::Call: return call(e, f);
            case Expr::MethodCall: return methodCall(e, f);
            case Expr::SuperCall: return superCall(e, f);
        }
        return Value();
    }

    // ---------- calls ----------
    Value callRoutine(const Routine& r, const std::vector<ExprP>& args, Frame& caller, std::shared_ptr<ObjectData> self,
                      const ClassInfo* cls, int line, int col) {
        if (args.size() != r.params.size())
            rt("R312", (r.isFunc ? "Function " : "Procedure ") + r.name + " expects " + std::to_string(r.params.size()) +
               " argument(s) but " + std::to_string(args.size()) + " were given", line, col);
        if (depth >= opt.maxDepth)
            rt("R307", "Stack overflow: calls are nested more than " + std::to_string(opt.maxDepth) + " levels deep", line, col, "Check that your recursion has a base case that stops it.");
        Frame fr; fr.self = std::move(self); fr.cls = cls; fr.isFunc = r.isFunc; fr.name = r.name;
        struct Back { LRef target; CellP cell; };
        std::vector<Back> back;
        for (size_t i = 0; i < r.params.size(); ++i) {
            const Param& p = r.params[i];
            TypePtr pt = resolveType(p.type, fr);
            auto cell = std::make_shared<Cell>();
            if (p.byRef) {
                LRef l = locate(*args[i], caller);
                if (l.isConst) rt("R303", "A constant cannot be passed BYREF", *args[i]);
                cell->v = coerce(*l.v, pt, args[i]->line, args[i]->col, "pass");
                back.push_back({l, cell});
            } else {
                cell->v = coerce(eval(*args[i], caller), pt, args[i]->line, args[i]->col, "pass");
            }
            if (pt->k == Type::Array && pt->bounds.empty()) {   // open array parameter: adopt the argument's shape
                auto at = std::make_shared<Type>(*pt); at->bounds = cell->v.as<ArrayData>()->bounds; pt = at;
            }
            cell->t = pt; fr.vars[p.key] = cell;
        }
        if (r.isFunc) fr.retType = resolveType(*r.ret, fr);
        ++depth;
        try { execBlock(r.body, fr); } catch (...) { --depth; throw; }
        --depth;
        for (auto& b : back) *b.target.v = coerce(b.cell->v, b.target.t ? b.target.t : b.cell->t, line, col, "return");
        if (r.isFunc) {
            if (!fr.returned) rt("R305", "Function " + r.name + " finished without returning a value", line, col, "Add a RETURN statement.");
            return fr.ret;
        }
        return Value();
    }

    Value call(const Expr& e, Frame& f) {
        if (f.self) {
            const ClassInfo* owner = nullptr;
            if (const Routine* m = findMethod(f.self->cls, e.key, owner)) return callRoutine(*m, e.args, f, f.self, owner, e.line, e.col);
        }
        auto it = routines.find(e.key);
        if (it != routines.end()) return callRoutine(*it->second, e.args, f, nullptr, nullptr, e.line, e.col);
        const BuiltinInfo* bi = findBuiltin(e.key);
        if (!bi) rt("R314", "Procedure or function '" + e.s + "' is not defined", e);
        if (static_cast<int>(e.args.size()) < bi->minArgs || static_cast<int>(e.args.size()) > bi->maxArgs)
            rt("R312", e.s + " expects " + std::to_string(bi->minArgs) + (bi->minArgs == bi->maxArgs ? "" : ".." + std::to_string(bi->maxArgs)) +
               " argument(s) but " + std::to_string(e.args.size()) + " were given", e);
        if (e.key == "eof") return Value::Bool(eofOf(e, f));
        std::vector<Value> a; for (const auto& x : e.args) a.push_back(eval(*x, f));
        BuiltinCtx ctx; ctx.rng = &rng;
        try { return callBuiltin(e.key, a, ctx); }
        catch (const BuiltinError& be) { rt(be.code, be.msg, e); }
    }

    Value methodCall(const Expr& e, Frame& f) {
        Value o = eval(*e.a, f);
        if (o.t == VT::Null) rt("R305", "Cannot call method '" + e.s + "' because the object is not initialised", e, "Create it first with NEW.");
        if (o.t != VT::Object) rt("R303", "Methods can only be called on objects, not on " + typeName(o), e);
        auto od = std::static_pointer_cast<ObjectData>(o.h);
        const ClassInfo* owner = nullptr;
        const Routine* m = findMethod(od->cls, e.key, owner);
        if (!m) rt("R315", "The class " + od->cls->def->name + " has no method '" + e.s + "'", e);
        if (m->isPrivate && f.cls != owner) rt("R308", "Method '" + e.s + "' is PRIVATE in class " + owner->def->name + " and cannot be called from outside it", e);
        return callRoutine(*m, e.args, f, od, owner, e.line, e.col);
    }

    Value superCall(const Expr& e, Frame& f) {
        if (!f.self || !f.cls) rt("R303", "SUPER can only be used inside a method of a class that inherits from another class", e);
        const ClassInfo* owner = nullptr;
        const Routine* m = findMethod(f.cls->parent, e.key, owner);
        if (!m) {
            if (e.key == "new" && e.args.empty()) return Value();
            rt("R315", "The parent class has no method '" + e.s + "'", e);
        }
        return callRoutine(*m, e.args, f, f.self, owner, e.line, e.col);
    }

    Value instantiate(ClassInfo* ci, const Expr& e, Frame& f) {
        auto obj = std::make_shared<ObjectData>(); obj->cls = ci;
        std::vector<const ClassInfo*> chain; for (const ClassInfo* c = ci; c; c = c->parent) chain.push_back(c);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            for (const auto& fd : (*it)->def->fields) {
                auto cell = std::make_shared<Cell>(); cell->t = resolveType(fd.type, globals); cell->v = defaultValue(cell->t);
                obj->f[fd.key] = cell;
            }
        const ClassInfo* owner = nullptr;
        if (const Routine* ctor = findMethod(ci, "new", owner)) callRoutine(*ctor, e.args, f, obj, owner, e.line, e.col);
        else if (!e.args.empty()) rt("R312", "The class " + ci->def->name + " has no constructor (PROCEDURE NEW) but arguments were given", e);
        Value v; v.t = VT::Object; v.h = obj; return v;
    }

    // ---------- files ----------
    std::string resolvePath(const std::string& n) {
        if (opt.baseDir.empty() || n.empty()) return n;
        bool abs = n[0] == '/' || n[0] == '\\' || (n.size() > 1 && n[1] == ':');
        return abs ? n : opt.baseDir + "/" + n;
    }
    FileState& fileOf(const Expr& e, Frame& f, int wantMode = -1) {
        Value v = eval(e, f);
        if (!texty(v)) rt("R303", "A file name must be a STRING but is " + typeName(v), e);
        auto it = files.find(valueToString(v));
        if (it == files.end()) rt("R306", "File \"" + valueToString(v) + "\" is not open", e, "Use OPENFILE first.");
        if (wantMode >= 0) {
            int m = it->second->mode;
            bool ok = (wantMode == 0 && m == 0) || (wantMode == 1 && (m == 1 || m == 2));
            if (!ok) rt("R306", "File \"" + valueToString(v) + "\" was not opened for " + (wantMode == 0 ? "READ" : "WRITE or APPEND"), e);
        }
        return *it->second;
    }
    bool eofOf(const Expr& e, Frame& f) {
        FileState& fs = fileOf(*e.args[0], f);
        if (fs.mode == 3) return fs.pos > fs.recs.size();
        if (fs.mode != 0) rt("R306", "EOF can only be used on a file opened for READ", e);
        return fs.in.peek() == std::char_traits<char>::eof();
    }
    static std::string esc(const std::string& s) {
        std::string o; for (char c : s) { if (c == '\\') o += "\\\\"; else if (c == '\t') o += "\\t"; else if (c == '\n') o += "\\n"; else o += c; } return o;
    }
    static std::string unesc(const std::string& s) {
        std::string o; for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\\' && i + 1 < s.size()) { ++i; o += s[i] == 't' ? '\t' : s[i] == 'n' ? '\n' : s[i]; } else o += s[i]; } return o;
    }
    std::string serialise(const Value& v, const Stmt& s) {
        if (v.t != VT::Record) rt("R306", "PUTRECORD needs a record value but received " + typeName(v), s);
        RecordData* rd = v.as<RecordData>(); std::string line;
        for (size_t i = 0; i < rd->type->fields.size(); ++i) {
            const auto& fv = rd->f.at(rd->type->fields[i].first);
            if (fv.t == VT::Array || fv.t == VT::Record || fv.t == VT::Object || fv.t == VT::Ptr || fv.t == VT::Enum || fv.t == VT::Null)
                rt("R306", "Records written to random-access files may only contain simple fields (INTEGER, REAL, STRING, CHAR, BOOLEAN, DATE)", s);
            if (i) line += '\t';
            line += esc(valueToString(fv));
        }
        return line;
    }
    void deserialise(const std::string& line, Value& target, const Stmt& s) {
        if (target.t != VT::Record) rt("R306", "GETRECORD needs a record variable but received " + typeName(target), s);
        RecordData* rd = target.as<RecordData>();
        std::vector<std::string> parts; std::string cur; for (char c : line) { if (c == '\t') { parts.push_back(cur); cur.clear(); } else cur += c; } parts.push_back(cur);
        if (parts.size() != rd->type->fields.size()) rt("R306", "The stored record does not match the record type " + rd->type->name, s);
        for (size_t i = 0; i < parts.size(); ++i) {
            const auto& fld = rd->type->fields[i]; std::string p = unesc(parts[i]); Value v;
            try {
                switch (fld.second->k) {
                    case Type::Int: v = Value::Int(std::stoll(p)); break;
                    case Type::Real: v = Value::Real(std::stod(p)); break;
                    case Type::Str: v = Value::Str(p); break;
                    case Type::Char: if (p.size() != 1) throw 1; v = Value::Char(p[0]); break;
                    case Type::Bool: v = Value::Bool(p == "TRUE"); break;
                    case Type::Date: { if (p.size() != 10) throw 1; v = Value::Date(daysFromCivil(std::stoi(p.substr(6)), static_cast<unsigned>(std::stoi(p.substr(3, 2))), static_cast<unsigned>(std::stoi(p.substr(0, 2))))); break; }
                    default: throw 1;
                }
            } catch (...) { rt("R306", "The stored record is corrupt or does not match the record type " + rd->type->name, s); }
            rd->f[fld.first] = v;
        }
    }
    void flushRandom(FileState& fs) {
        std::ofstream o(fs.path, std::ios::trunc);
        for (const auto& r : fs.recs) o << r << '\n';
    }
    void closeAll() {
        for (auto& kv : files) if (kv.second->mode == 3) flushRandom(*kv.second);
        files.clear();
    }

    // ---------- statements ----------
    void tick(const Stmt& s) {
        if (opt.trace && opt.traceOut) *opt.traceOut << "[trace] line " << s.line << "\n";
        ++ticks;
        if (opt.cancel && (ticks & 0x3F) == 0 && opt.cancel->load(std::memory_order_relaxed))
            rt("R318", "Execution stopped by the user", s);
        if (opt.timeoutMs > 0 && (ticks & 0x3FF) == 0 &&
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count() > opt.timeoutMs)
            rt("R310", "Execution timed out after " + std::to_string(opt.timeoutMs) + " ms", s, "The program may contain an infinite loop.");
    }
    Flow execBlock(const std::vector<StmtP>& b, Frame& f) {
        for (const auto& s : b) if (exec(*s, f) == Flow::Return) return Flow::Return;
        return Flow::Normal;
    }
    bool caseMatch(const Value& subj, const Expr& lo, const Expr* hi, Frame& f, const Expr& at) {
        Value a = eval(lo, f); bool cmp;
        if (!hi) return compare(subj, a, at, cmp) == 0;
        Value b = eval(*hi, f);
        return compare(subj, a, at, cmp) >= 0 && compare(subj, b, at, cmp) <= 0;
    }
    Value readInput(const TypePtr& t, const Stmt& s) {
        out.flush();
        std::string line;
        if (!std::getline(in, line)) rt("R309", "INPUT: there is no more input available", s);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto trim = [](std::string x) { size_t b = x.find_first_not_of(" \t"), e = x.find_last_not_of(" \t"); return b == std::string::npos ? std::string() : x.substr(b, e - b + 1); };
        auto bad = [&](const char* what) { rt("R304", "INPUT: \"" + line + "\" is not a valid " + what, s); };
        switch (t->k) {
            case Type::Str: return Value::Str(line);
            case Type::Char: if (line.empty()) bad("CHAR"); return Value::Char(line[0]);
            case Type::Int: { std::string x = trim(line); size_t pos = 0; long long v = 0;
                try { v = std::stoll(x, &pos); } catch (...) { bad("INTEGER"); } if (pos != x.size() || x.empty()) bad("INTEGER"); return Value::Int(v); }
            case Type::Real: { std::string x = trim(line); size_t pos = 0; double v = 0;
                try { v = std::stod(x, &pos); } catch (...) { bad("REAL"); } if (pos != x.size() || x.empty()) bad("REAL"); return Value::Real(v); }
            case Type::Bool: { std::string x = trim(line); for (char& c : x) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (x == "TRUE") return Value::Bool(true);
                if (x == "FALSE") return Value::Bool(false);
                bad("BOOLEAN"); return Value(); }
            default: rt("R303", "INPUT cannot read into a variable of type " + tname(t), s);
        }
        return Value();
    }

    Flow exec(const Stmt& s, Frame& f) {
        tick(s);
        switch (s.k) {
            case Stmt::Declare:
                for (size_t i = 0; i < s.keys.size(); ++i) {
                    auto ex = f.vars.find(s.keys[i]);
                    if (ex != f.vars.end() && ex->second->site != &s) rt("R313", "Variable '" + s.names[i] + "' is already declared", s);
                    // The same DECLARE executed again (e.g. inside a loop body) simply re-initialises the variable.
                    auto c = std::make_shared<Cell>(); c->t = resolveType(*s.tspec, f); c->v = defaultValue(c->t); c->site = &s; f.vars[s.keys[i]] = c;
                }
                return Flow::Normal;
            case Stmt::Constant: {
                if (f.vars.count(s.keys[0])) rt("R313", "'" + s.names[0] + "' is already declared", s);
                auto c = std::make_shared<Cell>(); c->v = eval(*s.e1, f); c->isConst = true;
                switch (c->v.t) { case VT::Int: c->t = tInt; break; case VT::Real: c->t = tReal; break; case VT::Str: c->t = tStr; break;
                    case VT::Char: c->t = tChar; break; case VT::Bool: c->t = tBool; break; case VT::Date: c->t = tDate; break;
                    default: rt("R303", "A constant must be a simple value", s); }
                f.vars[s.keys[0]] = c; return Flow::Normal;
            }
            case Stmt::Assign: {
                Value v = eval(*s.e2, f);
                LRef l = locate(*s.e1, f);
                if (l.isConst) rt("R303", "Cannot assign to a constant", *s.e1);
                if (!l.t) rt("R303", "Cannot assign to this expression", *s.e1);
                *l.v = coerce(v, l.t, s.e2->line, s.e2->col);
                return Flow::Normal;
            }
            case Stmt::Input: {
                LRef l = locate(*s.e1, f);
                if (l.isConst) rt("R303", "Cannot INPUT into a constant", *s.e1);
                *l.v = readInput(l.t, s); return Flow::Normal;
            }
            case Stmt::Output: {
                for (const auto& a : s.args) out << str(eval(*a, f));
                out << '\n'; if (opt.autoFlush) out.flush(); return Flow::Normal;
            }
            case Stmt::If:
                if (evalBool(*s.e1, f, "IF condition")) return execBlock(s.body, f);
                return execBlock(s.elseBody, f);
            case Stmt::Case: {
                Value subj = eval(*s.e1, f);
                for (const auto& b : s.branches)
                    for (const auto& v : b.vals)
                        if (caseMatch(subj, *v.first, v.second.get(), f, *s.e1)) return execBlock(b.body, f);
                return execBlock(s.otherwise, f);
            }
            case Stmt::For: {
                long long from = evalInt(*s.e2, f, "FOR start value"), to = evalInt(*s.e3, f, "FOR end value");
                long long step = s.e4 ? evalInt(*s.e4, f, "FOR STEP") : 1;
                if (step == 0) rt("R303", "FOR STEP cannot be 0", *s.e4);
                LRef l = locate(*s.e1, f);
                if (!l.t || l.t->k != Type::Int) rt("R303", "The FOR loop variable '" + s.e1->s + "' must be an INTEGER", *s.e1);
                if (l.isConst) rt("R303", "A constant cannot be used as a loop variable", *s.e1);
                for (long long i = from; step > 0 ? i <= to : i >= to; i += step) {
                    LRef lv = locate(*s.e1, f); *lv.v = Value::Int(i);
                    if (execBlock(s.body, f) == Flow::Return) return Flow::Return;
                    tick(s);
                    if ((step > 0 && i > to - step) || (step < 0 && i < to - step)) { LRef e = locate(*s.e1, f); *e.v = Value::Int(i + step); break; }
                }
                return Flow::Normal;
            }
            case Stmt::While:
                while (evalBool(*s.e1, f, "WHILE condition")) { if (execBlock(s.body, f) == Flow::Return) return Flow::Return; tick(s); }
                return Flow::Normal;
            case Stmt::Repeat:
                do { if (execBlock(s.body, f) == Flow::Return) return Flow::Return; tick(s); } while (!evalBool(*s.e1, f, "UNTIL condition"));
                return Flow::Normal;
            case Stmt::Call: eval(*s.e1, f); return Flow::Normal;
            case Stmt::Return:
                if (!f.isFunc) rt("R303", "RETURN can only be used inside a FUNCTION", s);
                f.ret = coerce(eval(*s.e1, f), f.retType, s.e1->line, s.e1->col, "return"); f.returned = true; return Flow::Return;
            case Stmt::TypeDefn: case Stmt::ClassDefn: case Stmt::ProcDef: return Flow::Normal;
            case Stmt::OpenFile: {
                Value n = eval(*s.e1, f); if (!texty(n)) rt("R303", "A file name must be a STRING", *s.e1);
                std::string name = valueToString(n);
                if (files.count(name)) rt("R306", "File \"" + name + "\" is already open", s);
                auto fs = std::make_unique<FileState>(); fs->mode = s.mode; fs->path = resolvePath(name);
                if (s.mode == 0) { fs->in.open(fs->path); if (!fs->in) rt("R306", "Cannot open \"" + name + "\" for reading (file not found or not readable)", s, "Check the file name and that the file exists in the working directory."); }
                else if (s.mode == 1 || s.mode == 2) { fs->out.open(fs->path, s.mode == 1 ? std::ios::trunc : std::ios::app); if (!fs->out) rt("R306", "Cannot open \"" + name + "\" for writing", s); }
                else { std::ifstream r(fs->path); std::string l; while (std::getline(r, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); fs->recs.push_back(l); }
                       std::ofstream touch(fs->path, std::ios::app); if (!touch) rt("R306", "Cannot open \"" + name + "\" for random access", s); }
                files[name] = std::move(fs); return Flow::Normal;
            }
            case Stmt::CloseFile: {
                Value n = eval(*s.e1, f); std::string name = valueToString(n);
                auto it = files.find(name); if (it == files.end()) rt("R306", "File \"" + name + "\" is not open", s);
                if (it->second->mode == 3) flushRandom(*it->second);
                files.erase(it); return Flow::Normal;
            }
            case Stmt::ReadFile: {
                FileState& fs = fileOf(*s.e1, f, 0);
                std::string line; if (!std::getline(fs.in, line)) rt("R306", "READFILE: attempted to read past the end of the file", s, "Test EOF(filename) before reading.");
                if (!line.empty() && line.back() == '\r') line.pop_back();
                LRef l = locate(*s.e2, f);
                if (l.t && l.t->k != Type::Str && l.t->k != Type::Char) {   // convert text to the target type
                    std::istringstream is(line); (void)is;
                    Value v; try { switch (l.t->k) { case Type::Int: v = Value::Int(std::stoll(line)); break; case Type::Real: v = Value::Real(std::stod(line)); break;
                        case Type::Bool: v = Value::Bool(line == "TRUE"); break; default: throw 1; } }
                    catch (...) { rt("R304", "READFILE: \"" + line + "\" cannot be converted to " + tname(l.t), s); }
                    *l.v = v;
                } else *l.v = coerce(Value::Str(line), l.t ? l.t : tStr, s.line, s.col);
                return Flow::Normal;
            }
            case Stmt::WriteFile: {
                FileState& fs = fileOf(*s.e1, f, 1);
                fs.out << str(eval(*s.e2, f)) << '\n'; return Flow::Normal;
            }
            case Stmt::Seek: {
                FileState& fs = fileOf(*s.e1, f); if (fs.mode != 3) rt("R306", "SEEK needs a file opened for RANDOM", s);
                long long n = evalInt(*s.e2, f, "SEEK address");
                if (n < 1) rt("R306", "SEEK address must be 1 or more (records are numbered from 1)", s);
                fs.pos = static_cast<size_t>(n); return Flow::Normal;
            }
            case Stmt::GetRecord: {
                FileState& fs = fileOf(*s.e1, f); if (fs.mode != 3) rt("R306", "GETRECORD needs a file opened for RANDOM", s);
                if (fs.pos > fs.recs.size()) rt("R306", "GETRECORD: record " + std::to_string(fs.pos) + " does not exist (the file has " + std::to_string(fs.recs.size()) + " records)", s);
                LRef l = locate(*s.e2, f); deserialise(fs.recs[fs.pos - 1], *l.v, s); ++fs.pos; return Flow::Normal;
            }
            case Stmt::PutRecord: {
                FileState& fs = fileOf(*s.e1, f); if (fs.mode != 3) rt("R306", "PUTRECORD needs a file opened for RANDOM", s);
                std::string rec = serialise(eval(*s.e2, f), s);
                if (fs.pos > fs.recs.size() + 1) fs.recs.resize(fs.pos - 1);
                if (fs.pos == fs.recs.size() + 1) fs.recs.push_back(rec); else fs.recs[fs.pos - 1] = rec;
                ++fs.pos; return Flow::Normal;
            }
        }
        return Flow::Normal;
    }

    void run() {
        started = std::chrono::steady_clock::now();
        hoist();
        try {
            for (const auto& s : prog.stmts) { Flow fl = exec(*s, globals); if (fl == Flow::Return) break; }
        } catch (...) { out.flush(); files.clear(); throw; }
        out.flush(); closeAll();
    }
};

Interpreter::Interpreter(const Program& p, std::istream& in, std::ostream& out, Options o)
    : d_(std::make_unique<Impl>(p, in, out, std::move(o))) {}
Interpreter::~Interpreter() = default;
void Interpreter::run() { d_->run(); }

} // namespace pcs
