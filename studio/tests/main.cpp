// studio_tests: tests for the portable part of PCScript Studio. Run through ctest or directly.
#include <cstring>
#include "test_util.hpp"

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (auto& c : T::registry()) {
        if (only && !std::strstr(c.name, only)) continue;
        T::current() = c.name;
        int before = T::failures();
        try { c.fn(); } catch (const std::exception& e) { T::fail(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what()); }
        ++ran;
        std::cout << (T::failures() == before ? "  ok    " : "  FAIL  ") << c.name << "\n";
    }
    std::cout << "\n" << ran << " test groups, " << T::checks() << " checks, " << T::failures() << " failures\n";
    return T::failures() == 0 ? 0 : 1;
}
