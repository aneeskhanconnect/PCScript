#include "parser/parser.hpp"
#include <algorithm>
#include <set>
#include "lexer/lexer.hpp"
#include "common/diagnostics.hpp"

namespace pcs {
namespace {

const std::set<std::string> kClosers = {"ENDIF", "ELSE", "ENDCASE", "OTHERWISE", "NEXT", "ENDWHILE", "UNTIL",
                                        "ENDPROCEDURE", "ENDFUNCTION", "ENDTYPE", "ENDCLASS"};

class P {
public:
    P(std::vector<Token> t, std::string f) : t_(std::move(t)), file_(std::move(f)) {}

    Program run() {
        Program p; p.file = file_;
        for (;;) {
            skipNL();
            if (cur().t == Tok::End) break;
            if (cur().t == Tok::Keyword && kClosers.count(cur().text))
                fail("E102", "Syntax error", "Unexpected token: " + desc(cur()) +
                     "\nThere is no open block that this keyword could close.",
                     "Remove it, or check that the matching block keyword (IF, WHILE, FOR ...) is present.");
            p.stmts.push_back(stmt());
        }
        return p;
    }

private:
    std::vector<Token> t_;
    size_t i_ = 0;
    std::string file_;

    // ---- token helpers ----
    const Token& cur() const { return t_[i_]; }
    const Token& at(size_t k) const { return t_[std::min(i_ + k, t_.size() - 1)]; }
    bool kw(const char* s, size_t k = 0) const { return at(k).t == Tok::Keyword && at(k).text == s; }
    bool op(const char* s, size_t k = 0) const { return at(k).t == Tok::Op && at(k).text == s; }
    void adv() { if (i_ + 1 < t_.size()) ++i_; }
    void skipNL() { while (cur().t == Tok::Newline) adv(); }

    static std::string desc(const Token& t) {
        switch (t.t) {
            case Tok::Newline: return "end of line";
            case Tok::End: return "end of file";
            case Tok::Str: return "\"" + t.text + "\"";
            case Tok::Char: return "'" + t.text + "'";
            default: return t.text;
        }
    }
    [[noreturn]] void failAt(const Token& tk, const std::string& code, const std::string& cat,
                             const std::string& msg, const std::string& hint = "") {
        throw PcsError(makeDiag(Severity::Error, code, cat, file_, tk.line, tk.col, msg, hint));
    }
    [[noreturn]] void fail(const std::string& code, const std::string& cat, const std::string& msg,
                           const std::string& hint = "") { failAt(cur(), code, cat, msg, hint); }
    [[noreturn]] void unexpected(const std::string& expected, const std::string& hint = "") {
        fail("E102", "Syntax error", "Unexpected token: " + desc(cur()) + "\nExpected: " + expected, hint);
    }
    void expectKw(const char* s, const std::string& hint = "") { if (!kw(s)) unexpected(s, hint); adv(); }
    void expectOp(const char* s, const std::string& hint = "") { if (!op(s)) unexpected(std::string("'") + s + "'", hint); adv(); }

    template <class T> std::unique_ptr<T> mk() {
        auto n = std::make_unique<T>(); n->line = cur().line; n->col = cur().col; return n;
    }
    ExprP ex(Expr::K k) { auto e = mk<Expr>(); e->k = k; return e; }

    void endStmt() {
        if (cur().t == Tok::Newline) { adv(); return; }
        if (cur().t == Tok::End) return;
        if (cur().t == Tok::Keyword && kClosers.count(cur().text)) return;
        unexpected("end of line", "Put each statement on its own line.");
    }

    std::vector<StmtP> block(const std::set<std::string>& terms, const std::string& opener, int openLine) {
        std::vector<StmtP> out;
        std::string list;   // primary closers first; ELSE is shown as an optional alternative
        for (const auto& s : terms) if (s != "ELSE") list += (list.empty() ? "" : " or ") + s;
        if (terms.count("ELSE")) list += " (or ELSE)";
        for (;;) {
            skipNL();
            if (cur().t == Tok::End)
                fail("E103", "Syntax error", "Reached the end of the file while looking for " + list +
                     ".\nThe " + opener + " block opened on line " + std::to_string(openLine) + " is never closed.",
                     "Add " + list + " to close the block.");
            if (cur().t == Tok::Keyword && terms.count(cur().text)) return out;
            if (cur().t == Tok::Keyword && kClosers.count(cur().text)) unexpected(list);
            out.push_back(stmt());
        }
    }

    // ---- types ----
    TypeSpec parseType() {
        TypeSpec ts; ts.line = cur().line; ts.col = cur().col;
        if (kw("ARRAY")) {
            adv(); ts.isArray = true; ts.name = "ARRAY"; ts.key = "array";
            if (op("[")) {
                adv();
                for (;;) {
                    ExprP lo = expr();
                    expectOp(":", "Array bounds are written ARRAY[lower:upper] OF type.");
                    ExprP hi = expr();
                    ts.dims.emplace_back(std::move(lo), std::move(hi));
                    if (op(",")) { adv(); continue; }
                    break;
                }
                expectOp("]");
            }
            expectKw("OF");
            ts.elem = std::make_unique<TypeSpec>(parseType());
            return ts;
        }
        if (op("^")) {
            adv(); ts.isPtr = true; ts.name = "^"; ts.key = "^";
            ts.elem = std::make_unique<TypeSpec>(parseType());
            return ts;
        }
        const Token& c = cur();
        if (c.t == Tok::Keyword && (c.text == "INTEGER" || c.text == "REAL" || c.text == "STRING" ||
                                    c.text == "CHAR" || c.text == "BOOLEAN" || c.text == "DATE")) {
            ts.name = c.text; ts.key = c.text; for (char& ch : ts.key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            adv(); return ts;
        }
        if (c.t == Tok::Ident) { ts.name = c.text; ts.key = c.key; adv(); return ts; }
        unexpected("a type name (INTEGER, REAL, STRING, CHAR, BOOLEAN, DATE, ARRAY or a user-defined type)");
    }

    // ---- expressions ----
    ExprP expr() { return orE(); }
    ExprP bin(Op o, ExprP a, ExprP b, const Token& at) {
        auto e = std::make_unique<Expr>(); e->k = Expr::Binary; e->op = o; e->line = at.line; e->col = at.col;
        e->a = std::move(a); e->b = std::move(b); return e;
    }
    ExprP orE() {
        ExprP l = andE();
        while (kw("OR")) { Token t = cur(); adv(); l = bin(Op::Or, std::move(l), andE(), t); }
        return l;
    }
    ExprP andE() {
        ExprP l = notE();
        while (kw("AND")) { Token t = cur(); adv(); l = bin(Op::And, std::move(l), notE(), t); }
        return l;
    }
    ExprP notE() {
        if (kw("NOT")) {
            auto e = ex(Expr::Unary); e->op = Op::Not; adv(); e->a = notE(); return e;
        }
        return cmpE();
    }
    ExprP cmpE() {
        ExprP l = concatE();
        static const std::pair<const char*, Op> ops[] = {{"=", Op::Eq}, {"<>", Op::Ne}, {"<=", Op::Le},
                                                         {">=", Op::Ge}, {"<", Op::Lt}, {">", Op::Gt}};
        for (const auto& o : ops)
            if (op(o.first)) { Token t = cur(); adv(); return bin(o.second, std::move(l), concatE(), t); }
        return l;
    }
    ExprP concatE() {
        ExprP l = addE();
        while (op("&")) { Token t = cur(); adv(); l = bin(Op::Concat, std::move(l), addE(), t); }
        return l;
    }
    ExprP addE() {
        ExprP l = mulE();
        for (;;) {
            if (op("+")) { Token t = cur(); adv(); l = bin(Op::Add, std::move(l), mulE(), t); }
            else if (op("-")) { Token t = cur(); adv(); l = bin(Op::Sub, std::move(l), mulE(), t); }
            else return l;
        }
    }
    ExprP mulE() {
        ExprP l = unaryE();
        for (;;) {
            Op o;
            if (op("*")) o = Op::Mul; else if (op("/")) o = Op::Div;
            else if (kw("DIV")) o = Op::IDiv; else if (kw("MOD")) o = Op::Mod; else return l;
            Token t = cur(); adv(); l = bin(o, std::move(l), unaryE(), t);
        }
    }
    ExprP unaryE() {
        if (op("-")) { auto e = ex(Expr::Unary); e->op = Op::Neg; adv(); e->a = unaryE(); return e; }
        if (op("+")) { adv(); return unaryE(); }
        return powE();
    }
    ExprP powE() {
        ExprP b = postfix();
        if (op("^")) { Token t = cur(); adv(); return bin(Op::Pow, std::move(b), unaryE(), t); }
        return b;
    }
    bool startsOperand(size_t k) const {
        const Token& t = at(k);
        if (t.t == Tok::Int || t.t == Tok::Real || t.t == Tok::Ident || t.t == Tok::Str || t.t == Tok::Char) return true;
        if (t.t == Tok::Op) return t.text == "(" || t.text == "-";
        if (t.t == Tok::Keyword) return t.text == "TRUE" || t.text == "FALSE" || t.text == "NOT" || t.text == "NEW" || t.text == "EOF";
        return false;
    }
    std::vector<ExprP> args() {
        std::vector<ExprP> a;
        expectOp("(");
        if (!op(")")) for (;;) { a.push_back(expr()); if (op(",")) { adv(); continue; } break; }
        expectOp(")", "Check that every opening bracket has a matching closing bracket.");
        return a;
    }
    ExprP primary() {
        const Token& c = cur();
        switch (c.t) {
            case Tok::Int: { auto e = ex(Expr::IntLit); e->i = c.i; adv(); return e; }
            case Tok::Real: { auto e = ex(Expr::RealLit); e->r = c.r; adv(); return e; }
            case Tok::Str: { auto e = ex(Expr::StrLit); e->s = c.text; adv(); return e; }
            case Tok::Char: { auto e = ex(Expr::CharLit); e->s = c.text; adv(); return e; }
            case Tok::Ident: {
                auto e = ex(Expr::Var); e->s = c.text; e->key = c.key; adv();
                if (op("(")) { e->k = Expr::Call; e->args = args(); }
                return e;
            }
            case Tok::Keyword:
                if (c.text == "TRUE" || c.text == "FALSE") { auto e = ex(Expr::BoolLit); e->i = c.text == "TRUE"; adv(); return e; }
                if (c.text == "EOF") { auto e = ex(Expr::Call); e->s = "EOF"; e->key = "eof"; adv(); e->args = args(); return e; }
                if (c.text == "NEW") {
                    auto e = ex(Expr::New); adv();
                    if (cur().t != Tok::Ident) unexpected("a class name after NEW");
                    e->s = cur().text; e->key = cur().key; adv();
                    if (op("(")) e->args = args();
                    return e;
                }
                if (c.text == "SUPER") {
                    auto e = ex(Expr::SuperCall); adv(); expectOp(".", "Use SUPER.NEW(...) or SUPER.MethodName(...).");
                    if (cur().t == Tok::Ident) { e->s = cur().text; e->key = cur().key; }
                    else if (kw("NEW")) { e->s = "NEW"; e->key = "new"; }
                    else unexpected("a method name after SUPER.");
                    adv();
                    if (op("(")) e->args = args();
                    return e;
                }
                break;
            case Tok::Op:
                if (c.text == "(") { adv(); ExprP e = expr(); expectOp(")", "Check that every opening bracket has a matching closing bracket."); return e; }
                if (c.text == "^") { auto e = ex(Expr::AddrOf); adv(); e->a = postfix(); return e; }
                break;
            default: break;
        }
        unexpected("an expression (a value, variable or function call)");
    }
    ExprP postfix() {
        ExprP e = primary();
        for (;;) {
            if (op("[")) {
                auto n = ex(Expr::Index); adv(); n->a = std::move(e);
                for (;;) { n->args.push_back(expr()); if (op(",")) { adv(); continue; } break; }
                expectOp("]", "Array indexes are written Name[index] or Name[row, column].");
                e = std::move(n);
            } else if (op(".")) {
                adv();
                if (cur().t != Tok::Ident && !kw("NEW")) unexpected("a field or method name after '.'");
                auto n = ex(Expr::Field); n->a = std::move(e); n->s = cur().text; n->key = cur().t == Tok::Ident ? cur().key : "new";
                adv();
                if (op("(")) { n->k = Expr::MethodCall; n->args = args(); }
                e = std::move(n);
            } else if (op("^") && !startsOperand(1)) {
                auto n = ex(Expr::Deref); adv(); n->a = std::move(e); e = std::move(n);
            } else break;
        }
        return e;
    }

    // ---- statements ----
    StmtP stmt() {
        const Token& c = cur();
        if (c.t == Tok::Keyword) {
            const std::string& k = c.text;
            if (k == "DECLARE") return declare();
            if (k == "CONSTANT") return constant();
            if (k == "INPUT") { auto s = mk<Stmt>(); s->k = Stmt::Input; adv(); s->e1 = postfix(); endStmt(); return s; }
            if (k == "OUTPUT") {
                auto s = mk<Stmt>(); s->k = Stmt::Output; adv();
                for (;;) { s->args.push_back(expr()); if (op(",")) { adv(); continue; } break; }
                endStmt(); return s;
            }
            if (k == "IF") return ifStmt();
            if (k == "CASE") return caseStmt();
            if (k == "FOR") return forStmt();
            if (k == "WHILE") {
                auto s = mk<Stmt>(); s->k = Stmt::While; int ln = c.line; adv(); s->e1 = expr();
                skipNLBefore("DO"); expectKw("DO"); s->body = block({"ENDWHILE"}, "WHILE", ln); adv(); endStmt(); return s;
            }
            if (k == "REPEAT") {
                auto s = mk<Stmt>(); s->k = Stmt::Repeat; int ln = c.line; adv();
                s->body = block({"UNTIL"}, "REPEAT", ln); adv(); s->e1 = expr(); endStmt(); return s;
            }
            if (k == "PROCEDURE" || k == "FUNCTION") {
                auto s = mk<Stmt>(); s->k = Stmt::ProcDef; s->routine = routine(false); return s;
            }
            if (k == "RETURN") { auto s = mk<Stmt>(); s->k = Stmt::Return; adv(); s->e1 = expr(); endStmt(); return s; }
            if (k == "CALL") {
                auto s = mk<Stmt>(); s->k = Stmt::Call; adv();
                ExprP e = postfix();
                if (e->k == Expr::Var) e->k = Expr::Call;
                if (e->k != Expr::Call && e->k != Expr::MethodCall && e->k != Expr::SuperCall)
                    failAt(t_[i_ > 0 ? i_ - 1 : 0], "E102", "Syntax error", "CALL must be followed by a procedure or method call",
                           "Write CALL ProcedureName(arguments).");
                s->e1 = std::move(e); endStmt(); return s;
            }
            if (k == "TYPE") return typeDef();
            if (k == "CLASS") return classDef();
            if (k == "OPENFILE") {
                auto s = mk<Stmt>(); s->k = Stmt::OpenFile; adv(); s->e1 = expr(); expectKw("FOR", "Write OPENFILE \"name.txt\" FOR READ (or WRITE, APPEND, RANDOM).");
                if (kw("READ")) s->mode = 0; else if (kw("WRITE")) s->mode = 1; else if (kw("APPEND")) s->mode = 2;
                else if (kw("RANDOM")) s->mode = 3; else unexpected("READ, WRITE, APPEND or RANDOM");
                adv(); endStmt(); return s;
            }
            if (k == "CLOSEFILE") { auto s = mk<Stmt>(); s->k = Stmt::CloseFile; adv(); s->e1 = expr(); endStmt(); return s; }
            if (k == "READFILE" || k == "GETRECORD") {
                auto s = mk<Stmt>(); s->k = k == "READFILE" ? Stmt::ReadFile : Stmt::GetRecord; adv();
                s->e1 = expr(); expectOp(","); s->e2 = postfix(); endStmt(); return s;
            }
            if (k == "WRITEFILE" || k == "SEEK" || k == "PUTRECORD") {
                auto s = mk<Stmt>(); s->k = k == "WRITEFILE" ? Stmt::WriteFile : k == "SEEK" ? Stmt::Seek : Stmt::PutRecord; adv();
                s->e1 = expr(); expectOp(","); s->e2 = expr(); endStmt(); return s;
            }
            if (k == "SUPER") return assignOrCall();
            unexpected("a statement",
                       k.rfind("END", 0) == 0 || k == "NEXT" || k == "UNTIL" ? "This closing keyword does not match the block that is open." : "");
        }
        if (c.t == Tok::Ident) return assignOrCall();
        unexpected("a statement");
    }
    void skipNLBefore(const char* k) {
        size_t j = i_; while (t_[j].t == Tok::Newline) ++j;
        if (t_[j].t == Tok::Keyword && t_[j].text == k) i_ = j;
    }
    StmtP assignOrCall() {
        auto s = mk<Stmt>();
        ExprP e = postfix();
        if (op("<-")) {
            adv(); s->k = Stmt::Assign; s->e1 = std::move(e); s->e2 = expr(); endStmt(); return s;
        }
        if (op("="))
            fail("E102", "Syntax error", "Unexpected token: =\nExpected: <- (assignment)",
                 "Use <- (or the arrow symbol) to assign a value; = only compares values.");
        if (e->k == Expr::Var) e->k = Expr::Call;
        if (e->k != Expr::Call && e->k != Expr::MethodCall && e->k != Expr::SuperCall)
            unexpected("<- (assignment) or a procedure call", "A statement must assign a value or call a procedure.");
        s->k = Stmt::Call; s->e1 = std::move(e); endStmt(); return s;
    }
    StmtP declare() {
        auto s = mk<Stmt>(); s->k = Stmt::Declare; adv();
        for (;;) {
            if (cur().t != Tok::Ident) unexpected("a variable name");
            s->names.push_back(cur().text); s->keys.push_back(cur().key); adv();
            if (op(",")) { adv(); continue; }
            break;
        }
        expectOp(":", "Write DECLARE Name : TYPE.");
        s->tspec = std::make_unique<TypeSpec>(parseType());
        endStmt(); return s;
    }
    StmtP constant() {
        auto s = mk<Stmt>(); s->k = Stmt::Constant; adv();
        if (cur().t != Tok::Ident) unexpected("a constant name");
        s->names.push_back(cur().text); s->keys.push_back(cur().key); adv();
        if (op("<-") || op("=")) adv(); else unexpected("<- (or =)", "Write CONSTANT Name <- value.");
        s->e1 = expr(); endStmt(); return s;
    }
    StmtP ifStmt() {
        auto s = mk<Stmt>(); s->k = Stmt::If; int ln = cur().line; adv();
        s->e1 = expr(); skipNLBefore("THEN"); expectKw("THEN", "An IF condition must be followed by THEN.");
        s->body = block({"ELSE", "ENDIF"}, "IF", ln);
        if (kw("ELSE")) { adv(); s->elseBody = block({"ENDIF"}, "IF", ln); }
        adv(); endStmt(); return s;
    }
    bool branchStart() const {
        const Token& c = cur();
        if (c.t == Tok::Int || c.t == Tok::Real || c.t == Tok::Str || c.t == Tok::Char) return true;
        if (op("-") || kw("TRUE") || kw("FALSE")) return true;
        if (c.t == Tok::Ident) return op(":", 1) || op(",", 1) || kw("TO", 1);
        return false;
    }
    StmtP caseStmt() {
        auto s = mk<Stmt>(); s->k = Stmt::Case; int ln = cur().line; adv();
        expectKw("OF", "Write CASE OF variable."); s->e1 = expr();
        for (;;) {
            skipNL();
            if (cur().t == Tok::End)
                fail("E103", "Syntax error", "Reached the end of the file while looking for ENDCASE.\nThe CASE block opened on line " +
                     std::to_string(ln) + " is never closed.", "Add ENDCASE to close the block.");
            if (kw("ENDCASE")) break;
            if (kw("OTHERWISE")) {
                adv(); if (op(":")) adv();
                for (;;) {
                    skipNL();
                    if (cur().t == Tok::End) fail("E103", "Syntax error", "Reached the end of the file while looking for ENDCASE.\nThe CASE block opened on line " + std::to_string(ln) + " is never closed.", "Add ENDCASE.");
                    if (kw("ENDCASE")) break;
                    if (cur().t == Tok::Keyword && kClosers.count(cur().text)) unexpected("ENDCASE");
                    s->otherwise.push_back(stmt());
                }
                break;
            }
            if (!branchStart()) unexpected("a CASE value followed by ':', OTHERWISE or ENDCASE");
            Branch b;
            for (;;) {
                ExprP lo = expr(), hi;
                if (kw("TO")) { adv(); hi = expr(); }
                b.vals.emplace_back(std::move(lo), std::move(hi));
                if (op(",")) { adv(); continue; }
                break;
            }
            expectOp(":", "Write each CASE branch as value : statement.");
            for (;;) {
                skipNL();
                if (cur().t == Tok::End || kw("ENDCASE") || kw("OTHERWISE") || branchStart()) break;
                if (cur().t == Tok::Keyword && kClosers.count(cur().text)) unexpected("ENDCASE");
                b.body.push_back(stmt());
            }
            s->branches.push_back(std::move(b));
        }
        adv(); endStmt(); return s;
    }
    StmtP forStmt() {
        auto s = mk<Stmt>(); s->k = Stmt::For; int ln = cur().line; adv();
        if (cur().t != Tok::Ident) unexpected("a loop variable");
        s->e1 = ex(Expr::Var); s->e1->s = cur().text; s->e1->key = cur().key; adv();
        if (!op("<-")) unexpected("<- (assignment)", "Write FOR Counter <- 1 TO 10.");
        adv(); s->e2 = expr(); expectKw("TO", "Write FOR Counter <- 1 TO 10.");
        s->e3 = expr();
        if (kw("STEP")) { adv(); s->e4 = expr(); }
        s->body = block({"NEXT"}, "FOR", ln); adv();
        if (cur().t == Tok::Ident) {
            if (cur().key != s->e1->key)
                fail("E102", "Syntax error", "NEXT names the variable '" + cur().text + "' but the loop variable is '" + s->e1->s + "'",
                     "Write NEXT " + s->e1->s + " (or just NEXT).");
            adv();
        }
        endStmt(); return s;
    }
    std::unique_ptr<Routine> routine(bool inClass) {
        auto r = mk<Routine>();
        r->isFunc = kw("FUNCTION"); int ln = cur().line; adv();
        if (cur().t == Tok::Ident) { r->name = cur().text; r->key = cur().key; }
        else if (kw("NEW") && inClass) { r->name = "NEW"; r->key = "new"; }
        else unexpected(r->isFunc ? "a function name" : "a procedure name");
        adv();
        if (op("(")) {
            adv();
            if (!op(")")) {
                bool byRef = false;
                for (;;) {
                    if (kw("BYREF")) { byRef = true; adv(); } else if (kw("BYVAL")) { byRef = false; adv(); }
                    std::vector<Token> names;
                    for (;;) {
                        if (cur().t != Tok::Ident) unexpected("a parameter name");
                        names.push_back(cur()); adv();
                        if (op(",") && at(1).t == Tok::Ident && (op(":", 2) || op(",", 2))) { adv(); continue; }
                        break;
                    }
                    expectOp(":", "Write parameters as Name : TYPE.");
                    TypeSpec ts = parseType();
                    for (size_t n = 0; n < names.size(); ++n) {
                        Param p; p.name = names[n].text; p.key = names[n].key; p.byRef = byRef;
                        if (n + 1 == names.size()) p.type = std::move(ts);
                        else { p.type.name = ts.name; p.type.key = ts.key; p.type.isArray = ts.isArray; p.type.isPtr = ts.isPtr;
                               p.type.line = ts.line; p.type.col = ts.col; if (ts.isArray || ts.isPtr) fail("E102", "Syntax error", "Grouped parameters must use a simple type"); }
                        r->params.push_back(std::move(p));
                    }
                    if (op(",")) { adv(); continue; }
                    break;
                }
            }
            expectOp(")", "Check that every opening bracket has a matching closing bracket.");
        }
        if (r->isFunc) { expectKw("RETURNS", "A FUNCTION header needs RETURNS and a type."); r->ret = std::make_unique<TypeSpec>(parseType()); }
        r->body = block({r->isFunc ? "ENDFUNCTION" : "ENDPROCEDURE"}, r->isFunc ? "FUNCTION" : "PROCEDURE", ln);
        adv(); endStmt();
        return r;
    }
    StmtP typeDef() {
        auto s = mk<Stmt>(); s->k = Stmt::TypeDefn; int ln = cur().line; adv();
        auto td = mk<TypeDef>();
        if (cur().t != Tok::Ident) unexpected("a type name");
        td->name = cur().text; td->key = cur().key; adv();
        if (op("=")) {
            adv();
            if (op("(")) {
                adv(); td->kind = TypeDef::Enum;
                for (;;) {
                    if (cur().t != Tok::Ident) unexpected("an enumeration value name");
                    td->enumNames.push_back(cur().text); adv();
                    if (op(",")) { adv(); continue; }
                    break;
                }
                expectOp(")");
            } else if (op("^")) {
                adv(); td->kind = TypeDef::Pointer; td->target = std::make_unique<TypeSpec>(parseType());
            } else { td->kind = TypeDef::Alias; td->target = std::make_unique<TypeSpec>(parseType()); }
            endStmt();
        } else {
            td->kind = TypeDef::Record;
            for (;;) {
                skipNL();
                if (cur().t == Tok::End) fail("E103", "Syntax error", "Reached the end of the file while looking for ENDTYPE.\nThe TYPE block opened on line " + std::to_string(ln) + " is never closed.", "Add ENDTYPE.");
                if (kw("ENDTYPE")) break;
                if (kw("DECLARE")) adv();
                members(td->fields, false);
            }
            adv(); endStmt();
        }
        s->tdef = std::move(td); return s;
    }
    void members(std::vector<FieldDecl>& out, bool priv) {
        std::vector<Token> names;
        for (;;) {
            if (cur().t != Tok::Ident) unexpected("a field name");
            names.push_back(cur()); adv();
            if (op(",")) { adv(); continue; }
            break;
        }
        expectOp(":", "Write fields as Name : TYPE.");
        TypeSpec ts = parseType();
        for (size_t n = 0; n < names.size(); ++n) {
            FieldDecl f; f.name = names[n].text; f.key = names[n].key; f.isPrivate = priv; f.line = names[n].line; f.col = names[n].col;
            if (n + 1 == names.size()) f.type = std::move(ts);
            else { f.type.name = ts.name; f.type.key = ts.key; f.type.isArray = ts.isArray; f.type.isPtr = ts.isPtr; f.type.line = ts.line; f.type.col = ts.col; }
            out.push_back(std::move(f));
        }
        endStmt();
    }
    StmtP classDef() {
        auto s = mk<Stmt>(); s->k = Stmt::ClassDefn; int ln = cur().line; adv();
        auto cd = mk<ClassDef>();
        if (cur().t != Tok::Ident) unexpected("a class name");
        cd->name = cur().text; cd->key = cur().key; adv();
        if (kw("INHERITS")) {
            adv(); if (cur().t != Tok::Ident) unexpected("a parent class name");
            cd->parentName = cur().text; cd->parentKey = cur().key; adv();
        }
        endStmt();
        for (;;) {
            skipNL();
            if (cur().t == Tok::End) fail("E103", "Syntax error", "Reached the end of the file while looking for ENDCLASS.\nThe CLASS block opened on line " + std::to_string(ln) + " is never closed.", "Add ENDCLASS.");
            if (kw("ENDCLASS")) break;
            bool priv = false;
            if (kw("PUBLIC")) adv(); else if (kw("PRIVATE")) { priv = true; adv(); }
            if (kw("PROCEDURE") || kw("FUNCTION")) { auto r = routine(true); r->isPrivate = priv; cd->methods.push_back(std::move(r)); }
            else { if (kw("DECLARE")) adv(); members(cd->fields, priv); }
        }
        adv(); endStmt();
        s->cls = std::move(cd); return s;
    }
};

} // namespace

Program parseSource(const std::string& source, const std::string& fileName) {
    Lexer lx(source, fileName);
    P p(lx.tokenize(), fileName);
    return p.run();
}

} // namespace pcs
