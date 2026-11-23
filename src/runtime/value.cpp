#include "runtime/value.hpp"
#include <cmath>
#include <cstdio>

namespace pcs {

std::string typeName(const Value& v) {
    switch (v.t) {
        case VT::Null: return "NULL"; case VT::Int: return "INTEGER"; case VT::Real: return "REAL";
        case VT::Str: return "STRING"; case VT::Char: return "CHAR"; case VT::Bool: return "BOOLEAN";
        case VT::Date: return "DATE"; case VT::Enum: return "enumeration"; case VT::Array: return "ARRAY";
        case VT::Record: return "record"; case VT::Object: return "object"; case VT::Ptr: return "pointer";
    }
    return "?";
}

std::string formatReal(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.15g", d);
    return buf;
}

long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

void civilFromDays(long long z, long long& y, unsigned& m, unsigned& d) {
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
}

bool validDate(long long y, long long m, long long d) {
    if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1) return false;
    long long back_y; unsigned bm, bd;
    civilFromDays(daysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d)), back_y, bm, bd);
    return back_y == y && bm == m && bd == d;
}

std::string valueToString(const Value& v) {
    switch (v.t) {
        case VT::Int: return std::to_string(v.i);
        case VT::Real: return formatReal(v.r);
        case VT::Str: return v.s;
        case VT::Char: return std::string(1, static_cast<char>(v.i));
        case VT::Bool: return v.i ? "TRUE" : "FALSE";
        case VT::Date: {
            long long y; unsigned m, d; civilFromDays(v.i, y, m, d);
            char buf[32]; std::snprintf(buf, sizeof buf, "%02u/%02u/%04lld", d, m, y); return buf;
        }
        default: return "<" + typeName(v) + ">";
    }
}

Value copyValue(const Value& v) {
    if (v.t == VT::Array) {
        auto src = v.as<ArrayData>();
        auto n = std::make_shared<ArrayData>();
        n->bounds = src->bounds; n->elem = src->elem; n->data.reserve(src->data.size());
        for (const auto& e : src->data) n->data.push_back(copyValue(e));
        Value x = v; x.h = n; return x;
    }
    if (v.t == VT::Record) {
        auto src = v.as<RecordData>();
        auto n = std::make_shared<RecordData>();
        n->type = src->type;
        for (const auto& kv : src->f) n->f.emplace(kv.first, copyValue(kv.second));
        Value x = v; x.h = n; return x;
    }
    return v;
}

} // namespace pcs
