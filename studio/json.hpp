// Minimal JSON value, parser and writer (no external dependencies). Used for settings, accounts, themes and extensions.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace studio::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() = default;
    Value(bool b) : type_(Type::Bool), b_(b) {}
    Value(int n) : type_(Type::Number), n_(n) {}
    Value(long long n) : type_(Type::Number), n_(static_cast<double>(n)) {}
    Value(double n) : type_(Type::Number), n_(n) {}
    Value(const char* s) : type_(Type::String), s_(s) {}
    Value(std::string s) : type_(Type::String), s_(std::move(s)) {}
    static Value array() { Value v; v.type_ = Type::Array; return v; }
    static Value object() { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const { return isBool() ? b_ : def; }
    double asNumber(double def = 0) const { return isNumber() ? n_ : def; }
    int asInt(int def = 0) const { return isNumber() ? static_cast<int>(n_) : def; }
    std::string asString(const std::string& def = "") const { return isString() ? s_ : def; }

    const Array& items() const { return a_; }
    Array& items() { return a_; }
    const Object& members() const { return o_; }
    Object& members() { return o_; }

    // Object access. get() returns a reference to a shared null value when the key is missing.
    const Value& get(const std::string& key) const;
    bool has(const std::string& key) const { return isObject() && o_.count(key) != 0; }
    Value& set(const std::string& key, Value v) { if (!isObject()) *this = object(); return o_[key] = std::move(v); }
    void push(Value v) { if (!isArray()) *this = array(); a_.push_back(std::move(v)); }

private:
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0;
    std::string s_;
    Array a_;
    Object o_;
};

// Parses text (UTF-8, optional BOM). Returns false and fills *error ("line N: message") on failure.
bool parse(const std::string& text, Value& out, std::string* error = nullptr);
// indent < 0 gives compact output.
std::string dump(const Value& v, int indent = 2);

} // namespace studio::json
