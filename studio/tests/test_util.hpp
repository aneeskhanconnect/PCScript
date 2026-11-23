#pragma once
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace T {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline const char*& current() { static const char* n = ""; return n; }
struct Reg { Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::cout << "FAIL [" << current() << "] " << file << ":" << line << "\n    " << msg << "\n";
}
template <class X> inline void put(std::ostringstream& o, const X& x) {
    if constexpr (std::is_enum_v<X>) o << static_cast<int>(x); else o << x;
}
template <class A, class B> inline std::string show(const A& a, const B& b) {
    std::ostringstream o; o << "got:      <"; put(o, a); o << ">\n    expected: <"; put(o, b); o << ">"; return o.str();
}
inline std::filesystem::path tempDir(const std::string& name) {
    auto p = std::filesystem::temp_directory_path() / ("pcs_studio_test_" + name);
    std::error_code ec; std::filesystem::remove_all(p, ec); std::filesystem::create_directories(p, ec);
    return p;
}
} // namespace T

#define TEST(name) static void test_##name(); static T::Reg reg_##name(#name, test_##name); static void test_##name()
#define CHECK(c) do { ++T::checks(); if (!(c)) T::fail(__FILE__, __LINE__, "CHECK(" #c ")"); } while (0)
#define CHECK_EQ(a, b) do { ++T::checks(); auto _a = (a); auto _b = (b); if (!(_a == _b)) T::fail(__FILE__, __LINE__, T::show(_a, _b)); } while (0)
