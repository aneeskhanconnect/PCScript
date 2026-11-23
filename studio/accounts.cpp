#include "accounts.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>
#include "files.hpp"
#include "scan.hpp"
#include "sha256.hpp"

namespace studio {

namespace {
long long nowSeconds() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::vector<std::uint8_t> randomBytes(size_t n) {
    std::random_device rd;
    std::vector<std::uint8_t> v(n);
    for (auto& b : v) b = static_cast<std::uint8_t>(rd() & 0xFF);
    return v;
}
} // namespace

AccountStore::AccountStore(std::filesystem::path root, int it) : root_(std::move(root)), iterations_(std::max(1, it)) {}

bool AccountStore::validUsername(const std::string& n, std::string* why) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (n.size() < 3 || n.size() > 24) return no("The user name must be 3 to 24 characters long.");
    if (!std::isalnum(static_cast<unsigned char>(n[0]))) return no("The user name must start with a letter or digit.");
    for (char c : n) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.')) return no("Use only letters, digits, '_', '-' and '.' in a user name.");
    if (n.back() == '.') return no("The user name cannot end with a dot.");
    std::string l = toLower(n);
    static const char* const reserved[] = {"guest", "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "lpt1", "lpt2", "lpt3", "users", "accounts"};
    for (const char* r : reserved) if (l == r) return no("That user name is reserved. Please choose another.");
    return true;
}

bool AccountStore::validPassword(const std::string& pw, std::string* why) {
    if (pw.size() < 6) { if (why) *why = "The password must be at least 6 characters long."; return false; }
    if (pw.size() > 128) { if (why) *why = "The password is too long (128 characters at most)."; return false; }
    return true;
}

bool AccountStore::load(std::string* error) {
    accounts_.clear();
    std::string text;
    auto path = root_ / "accounts.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true;
    if (!files::readText(path, text, error)) return false;
    json::Value v; std::string perr;
    if (!json::parse(text, v, &perr)) { if (error) *error = "accounts.json is damaged (" + perr + "). It was not changed."; return false; }
    for (const json::Value& a : v.get("accounts").items()) {
        Account x;
        x.username = a.get("username").asString(); x.displayName = a.get("displayName").asString(x.username);
        x.saltHex = a.get("salt").asString(); x.hashHex = a.get("hash").asString();
        x.iterations = a.get("iterations").asInt(0);
        x.created = static_cast<long long>(a.get("created").asNumber()); x.lastLogin = static_cast<long long>(a.get("lastLogin").asNumber());
        if (!x.username.empty() && !x.saltHex.empty() && !x.hashHex.empty() && x.iterations > 0) accounts_.push_back(std::move(x));
    }
    return true;
}

bool AccountStore::save(std::string* error) const {
    json::Value v = json::Value::object(), arr = json::Value::array();
    v.set("version", 1);
    for (const Account& a : accounts_) {
        json::Value o = json::Value::object();
        o.set("username", a.username); o.set("displayName", a.displayName); o.set("salt", a.saltHex); o.set("hash", a.hashHex);
        o.set("iterations", a.iterations); o.set("created", a.created); o.set("lastLogin", a.lastLogin);
        arr.push(std::move(o));
    }
    v.set("accounts", std::move(arr));
    return files::writeTextAtomic(root_ / "accounts.json", json::dump(v), error);
}

std::vector<std::string> AccountStore::usernames() const {
    std::vector<std::string> o;
    for (const Account& a : accounts_) o.push_back(a.username);
    std::sort(o.begin(), o.end(), [](const std::string& a, const std::string& b) { return toLower(a) < toLower(b); });
    return o;
}

const Account* AccountStore::find(const std::string& u) const {
    std::string k = toLower(u);
    for (const Account& a : accounts_) if (toLower(a.username) == k) return &a;
    return nullptr;
}
Account* AccountStore::findMutable(const std::string& u) { return const_cast<Account*>(static_cast<const AccountStore*>(this)->find(u)); }

bool AccountStore::create(const std::string& username, const std::string& password, const std::string& displayName, std::string* error) {
    std::string why;
    if (!validUsername(username, &why) || !validPassword(password, &why)) { if (error) *error = why; return false; }
    if (find(username)) { if (error) *error = "That user name is already taken."; return false; }
    Account a;
    a.username = username; a.displayName = displayName.empty() ? username : displayName.substr(0, 60);
    auto salt = randomBytes(16);
    a.saltHex = toHex(salt.data(), salt.size());
    a.iterations = iterations_;
    auto h = pbkdf2Sha256(password, salt, a.iterations);
    a.hashHex = toHex(h.data(), h.size());
    a.created = nowSeconds();
    accounts_.push_back(std::move(a));
    if (!save(error)) { accounts_.pop_back(); return false; }
    return true;
}

bool AccountStore::verify(const std::string& username, const std::string& password) const {
    const Account* a = find(username);
    if (!a) return false;
    std::vector<std::uint8_t> salt, want;
    if (!fromHex(a->saltHex, salt) || !fromHex(a->hashHex, want)) return false;
    return constantTimeEqual(pbkdf2Sha256(password, salt, a->iterations, want.size()), want);
}

bool AccountStore::login(const std::string& username, const std::string& password, std::string* error) {
    if (!verify(username, password)) { if (error) *error = "Wrong user name or password."; return false; }
    Account* a = findMutable(username);
    current_ = a->username; guest_ = false;
    a->lastLogin = nowSeconds();
    save();   // best effort: a failure here must not stop sign-in
    return true;
}

bool AccountStore::changePassword(const std::string& username, const std::string& oldPw, const std::string& newPw, std::string* error) {
    if (!verify(username, oldPw)) { if (error) *error = "The current password is wrong."; return false; }
    std::string why;
    if (!validPassword(newPw, &why)) { if (error) *error = why; return false; }
    Account* a = findMutable(username);
    Account backup = *a;
    auto salt = randomBytes(16);
    a->saltHex = toHex(salt.data(), salt.size());
    a->iterations = iterations_;
    auto h = pbkdf2Sha256(newPw, salt, a->iterations);
    a->hashHex = toHex(h.data(), h.size());
    if (!save(error)) { *a = backup; return false; }
    return true;
}

bool AccountStore::remove(const std::string& username, const std::string& password, std::string* error) {
    if (!verify(username, password)) { if (error) *error = "The password is wrong."; return false; }
    std::string k = toLower(username);
    auto old = accounts_;
    accounts_.erase(std::remove_if(accounts_.begin(), accounts_.end(), [&](const Account& a) { return toLower(a.username) == k; }), accounts_.end());
    if (!save(error)) { accounts_ = old; return false; }
    if (toLower(current_) == k) logout();
    return true;
}

std::string AccountStore::currentDisplayName() const {
    if (guest_) return "Guest";
    const Account* a = find(current_);
    return a ? a->displayName : std::string();
}

std::filesystem::path AccountStore::profileDir(const std::string& user) const {
    std::string name = user.empty() ? "Guest" : user;
    const Account* a = find(name);
    return root_ / "users" / files::fromUtf8(a ? a->username : name);
}

} // namespace studio
