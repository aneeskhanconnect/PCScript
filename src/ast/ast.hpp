#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace pcs {

enum class Op { Add, Sub, Mul, Div, IDiv, Mod, Pow, Eq, Ne, Lt, Gt, Le, Ge, And, Or, Not, Neg, Concat };

struct Expr;
using ExprP = std::unique_ptr<Expr>;

struct Expr {
    enum K { IntLit, RealLit, StrLit, CharLit, BoolLit, Var, Index, Field, Call, MethodCall,
             SuperCall, Unary, Binary, New, Deref, AddrOf } k = IntLit;
    int line = 0, col = 0;
    long long i = 0;
    double r = 0;
    std::string s;      // literal text / identifier / member name
    std::string key;    // lower-case identifier key
    Op op = Op::Add;
    ExprP a, b;         // operands / base expression
    std::vector<ExprP> args;
};

struct TypeSpec {
    std::string name, key;
    bool isArray = false, isPtr = false;
    std::vector<std::pair<ExprP, ExprP>> dims;   // empty for an open array parameter
    std::unique_ptr<TypeSpec> elem;              // array element / pointee
    int line = 0, col = 0;
};

struct FieldDecl { std::string name, key; TypeSpec type; bool isPrivate = false; int line = 0, col = 0; };
struct Param { std::string name, key; TypeSpec type; bool byRef = false; };

struct Stmt;
using StmtP = std::unique_ptr<Stmt>;

struct Routine {
    std::string name, key;
    std::vector<Param> params;
    std::unique_ptr<TypeSpec> ret;
    std::vector<StmtP> body;
    bool isFunc = false, isPrivate = false;
    int line = 0, col = 0;
};

struct ClassDef {
    std::string name, key, parentName, parentKey;
    std::vector<FieldDecl> fields;
    std::vector<std::unique_ptr<Routine>> methods;
    int line = 0, col = 0;
};

struct TypeDef {
    enum Kind { Record, Enum, Pointer, Alias } kind = Record;
    std::string name, key;
    std::vector<FieldDecl> fields;
    std::vector<std::string> enumNames;
    std::unique_ptr<TypeSpec> target;   // pointer pointee / alias target
    int line = 0, col = 0;
};

struct Branch {
    std::vector<std::pair<ExprP, ExprP>> vals;   // second == null for a single value
    std::vector<StmtP> body;
};

struct Stmt {
    enum K { Declare, Constant, Assign, Input, Output, If, Case, For, While, Repeat, Call, Return,
             TypeDefn, ClassDefn, ProcDef, OpenFile, CloseFile, ReadFile, WriteFile, Seek,
             GetRecord, PutRecord } k = Declare;
    int line = 0, col = 0;
    std::vector<std::string> names, keys;
    std::unique_ptr<TypeSpec> tspec;
    ExprP e1, e2, e3, e4;
    std::vector<ExprP> args;
    std::vector<StmtP> body, elseBody, otherwise;
    std::vector<Branch> branches;
    int mode = 0;   // file mode: 0 READ, 1 WRITE, 2 APPEND, 3 RANDOM
    std::unique_ptr<Routine> routine;
    std::unique_ptr<ClassDef> cls;
    std::unique_ptr<TypeDef> tdef;
};

struct Program {
    std::string file;
    std::vector<StmtP> stmts;
};

} // namespace pcs
