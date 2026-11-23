#pragma once
#include <random>
#include <string>
#include <vector>
#include "runtime/value.hpp"

namespace pcs {

struct BuiltinInfo {
    const char* name;      // lower-case key
    int minArgs, maxArgs;
    char ret;              // static result type: i r s c b d or ? (depends on arguments)
};

const BuiltinInfo* findBuiltin(const std::string& key);

struct BuiltinError { std::string code, msg; };

struct BuiltinCtx { std::mt19937_64* rng = nullptr; };

// Evaluates a built-in function. Throws BuiltinError for invalid arguments.
Value callBuiltin(const std::string& key, const std::vector<Value>& a, BuiltinCtx& ctx);

} // namespace pcs
