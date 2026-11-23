#include "stdlib/builtins.hpp"
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace pcs {

static const BuiltinInfo kTable[] = {
    {"length", 1, 1, 'i'}, {"left", 2, 2, 's'}, {"right", 2, 2, 's'}, {"mid", 3, 3, 's'},
    {"substring", 3, 3, 's'}, {"lcase", 1, 1, '?'}, {"ucase", 1, 1, '?'}, {"to_upper", 1, 1, '?'},
    {"to_lower", 1, 1, '?'}, {"chr", 1, 1, 'c'}, {"asc", 1, 1, 'i'}, {"int", 1, 1, 'i'},
    {"rand", 1, 1, 'r'}, {"round", 2, 2, 'r'}, {"num_to_str", 1, 1, 's'}, {"str_to_num", 1, 1, '?'},
    {"is_num", 1, 1, 'b'}, {"day", 1, 1, 'i'}, {"month", 1, 1, 'i'}, {"year", 1, 1, 'i'},
    {"dayindex", 1, 1, 'i'}, {"setdate", 3, 3, 'd'}, {"today", 0, 0, 'd'}, {"eof", 1, 1, 'b'}};

const BuiltinInfo* findBuiltin(const std::string& key) {
    for (const auto& b : kTable) if (key == b.name) return &b;
    return nullptr;
}

namespace {
[[noreturn]] void bad(const std::string& code, const std::string& msg) { throw BuiltinError{code, msg}; }

bool isText(const Value& v) { return v.t == VT::Str || v.t == VT::Char; }
std::string text(const Value& v, const char* fn) {
    if (!isText(v)) bad("R303", std::string(fn) + " needs a STRING argument but received " + typeName(v));
    return valueToString(v);
}
long long integer(const Value& v, const char* fn) {
    if (v.t != VT::Int) bad("R303", std::string(fn) + " needs an INTEGER argument but received " + typeName(v));
    return v.i;
}
double number(const Value& v, const char* fn) {
    if (v.t == VT::Int) return static_cast<double>(v.i);
    if (v.t == VT::Real) return v.r;
    bad("R303", std::string(fn) + " needs a numeric argument but received " + typeName(v));
}
bool parseNum(const std::string& in, bool& isInt, long long& iv, double& dv) {
    size_t b = 0, e = in.size();
    while (b < e && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
    std::string s = in.substr(b, e - b);
    if (s.empty()) return false;
    size_t p = 0; if (s[0] == '-' || s[0] == '+') p = 1;
    size_t digits = 0, dots = 0;
    for (size_t k = p; k < s.size(); ++k) {
        if (std::isdigit(static_cast<unsigned char>(s[k]))) ++digits;
        else if (s[k] == '.') ++dots; else return false;
    }
    if (digits == 0 || dots > 1) return false;
    isInt = dots == 0;
    if (isInt) { errno = 0; iv = std::strtoll(s.c_str(), nullptr, 10); if (errno == ERANGE) return false; }
    dv = std::strtod(s.c_str(), nullptr);
    return true;
}
} // namespace

Value callBuiltin(const std::string& k, const std::vector<Value>& a, BuiltinCtx& ctx) {
    if (k == "length") return Value::Int(static_cast<long long>(text(a[0], "LENGTH").size()));
    if (k == "left" || k == "right") {
        std::string s = text(a[0], k == "left" ? "LEFT" : "RIGHT"); long long n = integer(a[1], "LEFT/RIGHT");
        if (n < 0 || n > static_cast<long long>(s.size())) bad("R316", std::string(k == "left" ? "LEFT" : "RIGHT") + ": count " + std::to_string(n) + " is outside 0.." + std::to_string(s.size()));
        return Value::Str(k == "left" ? s.substr(0, static_cast<size_t>(n)) : s.substr(s.size() - static_cast<size_t>(n)));
    }
    if (k == "mid" || k == "substring") {
        std::string s = text(a[0], "MID"); long long st = integer(a[1], "MID"), n = integer(a[2], "MID");
        if (st < 1 || n < 0 || st - 1 + n > static_cast<long long>(s.size()))
            bad("R316", "MID: start " + std::to_string(st) + ", length " + std::to_string(n) + " is outside the string (length " + std::to_string(s.size()) + ")");
        return Value::Str(s.substr(static_cast<size_t>(st - 1), static_cast<size_t>(n)));
    }
    if (k == "ucase" || k == "lcase" || k == "to_upper" || k == "to_lower") {
        bool up = k == "ucase" || k == "to_upper";
        std::string s = text(a[0], up ? "UCASE" : "LCASE");
        for (char& c : s) c = static_cast<char>(up ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
        return a[0].t == VT::Char ? Value::Char(s[0]) : Value::Str(s);
    }
    if (k == "chr") {
        long long n = integer(a[0], "CHR");
        if (n < 0 || n > 255) bad("R316", "CHR: code " + std::to_string(n) + " is outside 0..255");
        return Value::Char(static_cast<char>(n));
    }
    if (k == "asc") {
        std::string s = text(a[0], "ASC");
        if (s.size() != 1) bad("R316", "ASC needs exactly one character");
        return Value::Int(static_cast<unsigned char>(s[0]));
    }
    if (k == "int") return Value::Int(static_cast<long long>(std::trunc(number(a[0], "INT"))));
    if (k == "rand") {
        double x = number(a[0], "RAND");
        std::uniform_real_distribution<double> d(0.0, 1.0);
        return Value::Real(d(*ctx.rng) * x);
    }
    if (k == "round") {
        double x = number(a[0], "ROUND"); long long p = integer(a[1], "ROUND");
        if (p < 0 || p > 15) bad("R316", "ROUND: places must be between 0 and 15");
        double f = std::pow(10.0, static_cast<double>(p));
        return Value::Real(std::round(x * f) / f);
    }
    if (k == "num_to_str") {
        if (a[0].t != VT::Int && a[0].t != VT::Real) bad("R303", "NUM_TO_STR needs a number but received " + typeName(a[0]));
        return Value::Str(valueToString(a[0]));
    }
    if (k == "str_to_num" || k == "is_num") {
        std::string s = text(a[0], k == "is_num" ? "IS_NUM" : "STR_TO_NUM");
        bool isInt = false; long long iv = 0; double dv = 0;
        bool ok = parseNum(s, isInt, iv, dv);
        if (k == "is_num") return Value::Bool(ok);
        if (!ok) bad("R304", "STR_TO_NUM: \"" + s + "\" is not a valid number");
        return isInt ? Value::Int(iv) : Value::Real(dv);
    }
    if (k == "day" || k == "month" || k == "year") {
        if (a[0].t != VT::Date) bad("R303", "DAY/MONTH/YEAR need a DATE argument but received " + typeName(a[0]));
        long long y; unsigned m, d; civilFromDays(a[0].i, y, m, d);
        return Value::Int(k == "day" ? d : k == "month" ? m : y);
    }
    if (k == "dayindex") {
        if (a[0].t != VT::Date) bad("R303", "DAYINDEX needs a DATE argument but received " + typeName(a[0]));
        long long wd = ((a[0].i % 7) + 7 + 4) % 7;   // 1970-01-01 was a Thursday; 0 = Sunday
        return Value::Int(wd + 1);                   // Cambridge: Sunday = 1
    }
    if (k == "setdate") {
        long long d = integer(a[0], "SETDATE"), m = integer(a[1], "SETDATE"), y = integer(a[2], "SETDATE");
        if (!validDate(y, m, d)) bad("R316", "SETDATE: " + std::to_string(d) + "/" + std::to_string(m) + "/" + std::to_string(y) + " is not a valid date");
        return Value::Date(daysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d)));
    }
    if (k == "today") {
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return Value::Date(secs / 86400);
    }
    bad("R314", "Unknown built-in function " + k);
}

} // namespace pcs
