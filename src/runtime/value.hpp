#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pcs {

struct ClassInfo;
struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
    enum K { Int, Real, Str, Char, Bool, Date, Array, Record, Enum, Class, Pointer } k = Int;
    std::string name;
    std::vector<std::pair<long long, long long>> bounds;   // Array (empty = open array parameter)
    TypePtr elem;                                          // Array
    std::vector<std::pair<std::string, TypePtr>> fields;   // Record (declaration order)
    std::vector<std::string> enumNames;                    // Enum
    const ClassInfo* cls = nullptr;                        // Class
};

enum class VT { Null, Int, Real, Str, Char, Bool, Date, Enum, Array, Record, Object, Ptr };

struct Cell;
struct ArrayData;
struct RecordData;
struct ObjectData;

struct Value {
    VT t = VT::Null;
    long long i = 0;               // Int, Char code, Bool, Date (days since 1970-01-01), Enum ordinal
    double r = 0;
    std::string s;                 // Str; Enum type key
    std::shared_ptr<void> h;       // ArrayData / RecordData / ObjectData / Cell

    static Value Int(long long v) { Value x; x.t = VT::Int; x.i = v; return x; }
    static Value Real(double v) { Value x; x.t = VT::Real; x.r = v; return x; }
    static Value Str(std::string v) { Value x; x.t = VT::Str; x.s = std::move(v); return x; }
    static Value Char(char c) { Value x; x.t = VT::Char; x.i = static_cast<unsigned char>(c); return x; }
    static Value Bool(bool b) { Value x; x.t = VT::Bool; x.i = b; return x; }
    static Value Date(long long d) { Value x; x.t = VT::Date; x.i = d; return x; }
    template <class T> T* as() const { return static_cast<T*>(h.get()); }
};

struct Cell { Value v; TypePtr t; bool isConst = false; const void* site = nullptr; /* declaring statement */ };
using CellP = std::shared_ptr<Cell>;
struct ArrayData { std::vector<std::pair<long long, long long>> bounds; std::vector<Value> data; TypePtr elem; };
struct RecordData { TypePtr type; std::unordered_map<std::string, Value> f; };
struct ObjectData { const ClassInfo* cls = nullptr; std::unordered_map<std::string, CellP> f; };

std::string typeName(const Value& v);
std::string formatReal(double d);
std::string valueToString(const Value& v);          // Enum values need the interpreter (handled there)
Value copyValue(const Value& v);                    // deep copy of arrays/records
long long daysFromCivil(long long y, unsigned m, unsigned d);
void civilFromDays(long long z, long long& y, unsigned& m, unsigned& d);
bool validDate(long long y, long long m, long long d);

} // namespace pcs
