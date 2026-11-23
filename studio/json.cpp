#include "json.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace studio::json {

const Value& Value::get(const std::string& key) const {
    static const Value kNull;
    if (!isObject()) return kNull;
    auto it = o_.find(key);
    return it == o_.end() ? kNull : it->second;
}

namespace {

struct Parser {
    const std::string& t;
    size_t p = 0;
    int line = 1;
    std::string err;
    int depth = 0;

    explicit Parser(const std::string& s) : t(s) {}

    bool fail(const std::string& m) { if (err.empty()) err = "line " + std::to_string(line) + ": " + m; return false; }
    void ws() {
        while (p < t.size()) {
            char c = t[p];
            if (c == '\n') { ++line; ++p; }
            else if (c == ' ' || c == '\t' || c == '\r') ++p;
            else break;
        }
    }
    static void utf8(std::string& o, unsigned cp) {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    bool hex4(unsigned& v) {
        if (p + 4 > t.size()) return fail("bad \\u escape");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t[p++]; v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool str(std::string& o) {
        ++p;   // opening quote
        for (;;) {
            if (p >= t.size()) return fail("unterminated string");
            char c = t[p++];
            if (c == '"') return true;
            if (c == '\n') return fail("newline inside string");
            if (c != '\\') { o += c; continue; }
            if (p >= t.size()) return fail("unterminated string");
            char e = t[p++];
            switch (e) {
                case '"': o += '"'; break; case '\\': o += '\\'; break; case '/': o += '/'; break;
                case 'b': o += '\b'; break; case 'f': o += '\f'; break; case 'n': o += '\n'; break;
                case 'r': o += '\r'; break; case 't': o += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < t.size() && t[p] == '\\' && t[p + 1] == 'u') {
                        p += 2; unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, cp); break;
                }
                default: return fail("bad escape");
            }
        }
    }
    bool value(Value& out) {
        ws();
        if (p >= t.size()) return fail("unexpected end of input");
        char c = t[p];
        if (c == '{') {
            if (++depth > 64) return fail("nesting too deep");
            ++p; out = Value::object(); ws();
            if (p < t.size() && t[p] == '}') { ++p; --depth; return true; }
            for (;;) {
                ws();
                if (p >= t.size() || t[p] != '"') return fail("expected a string key");
                std::string k; if (!str(k)) return false;
                ws(); if (p >= t.size() || t[p] != ':') return fail("expected ':'");
                ++p; Value v; if (!value(v)) return false;
                out.members()[k] = std::move(v);
                ws();
                if (p < t.size() && t[p] == ',') { ++p; continue; }
                if (p < t.size() && t[p] == '}') { ++p; --depth; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            if (++depth > 64) return fail("nesting too deep");
            ++p; out = Value::array(); ws();
            if (p < t.size() && t[p] == ']') { ++p; --depth; return true; }
            for (;;) {
                Value v; if (!value(v)) return false;
                out.items().push_back(std::move(v));
                ws();
                if (p < t.size() && t[p] == ',') { ++p; continue; }
                if (p < t.size() && t[p] == ']') { ++p; --depth; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { std::string s; if (!str(s)) return false; out = Value(std::move(s)); return true; }
        if (t.compare(p, 4, "true") == 0) { p += 4; out = Value(true); return true; }
        if (t.compare(p, 5, "false") == 0) { p += 5; out = Value(false); return true; }
        if (t.compare(p, 4, "null") == 0) { p += 4; out = Value(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* end = nullptr;
            double d = std::strtod(t.c_str() + p, &end);
            if (end == t.c_str() + p) return fail("bad number");
            p = static_cast<size_t>(end - t.c_str());
            out = Value(d); return true;
        }
        return fail(std::string("unexpected character '") + c + "'");
    }
};

void esc(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += static_cast<char>(c);
        }
    }
    o += '"';
}

void write(std::string& o, const Value& v, int indent, int level) {
    auto nl = [&](int lv) { if (indent >= 0) { o += '\n'; o.append(static_cast<size_t>(lv * indent), ' '); } };
    switch (v.type()) {
        case Value::Type::Null: o += "null"; break;
        case Value::Type::Bool: o += v.asBool() ? "true" : "false"; break;
        case Value::Type::Number: {
            double d = v.asNumber();
            char b[40];
            if (std::floor(d) == d && std::fabs(d) < 1e15) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(d));
            else std::snprintf(b, sizeof b, "%.15g", d);
            o += b; break;
        }
        case Value::Type::String: esc(o, v.asString()); break;
        case Value::Type::Array:
            if (v.items().empty()) { o += "[]"; break; }
            o += '[';
            for (size_t i = 0; i < v.items().size(); ++i) { if (i) o += ','; nl(level + 1); write(o, v.items()[i], indent, level + 1); }
            nl(level); o += ']'; break;
        case Value::Type::Object: {
            if (v.members().empty()) { o += "{}"; break; }
            o += '{'; bool first = true;
            for (const auto& kv : v.members()) {
                if (!first) o += ',';
                first = false;
                nl(level + 1); esc(o, kv.first); o += indent >= 0 ? ": " : ":"; write(o, kv.second, indent, level + 1);
            }
            nl(level); o += '}'; break;
        }
    }
}

} // namespace

bool parse(const std::string& text, Value& out, std::string* error) {
    size_t start = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) start = 3;
    std::string body = text.substr(start);
    Parser ps(body);
    Value v;
    if (!ps.value(v)) { if (error) *error = ps.err; return false; }
    ps.ws();
    if (ps.p != body.size()) { if (error) *error = "line " + std::to_string(ps.line) + ": unexpected text after the value"; return false; }
    out = std::move(v);
    return true;
}

std::string dump(const Value& v, int indent) {
    std::string o; write(o, v, indent, 0);
    if (indent >= 0) o += '\n';
    return o;
}

} // namespace studio::json
