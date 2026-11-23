// Local accounts. These are separate *profiles on one computer* (settings, open tabs, a personal folder for programs),
// protected by a password that is stored only as a salted PBKDF2-HMAC-SHA256 hash. They are not online accounts: there is
// no sync and no password recovery. Someone with full access to the PC can still read the files on disk, so the password
// is a convenience for a shared school computer, not strong protection.
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "json.hpp"

namespace studio {

struct Account {
    std::string username;        // as typed when the account was created; unique ignoring case
    std::string displayName;
    std::string saltHex, hashHex;
    int iterations = 0;
    long long created = 0, lastLogin = 0;   // seconds since 1970
};

class AccountStore {
public:
    // root: the IDE's data folder (accounts.json and users/<name>/ live in it).
    explicit AccountStore(std::filesystem::path root, int pbkdf2Iterations = 120000);

    bool load(std::string* error = nullptr);       // a missing file is fine (no accounts yet)
    bool save(std::string* error = nullptr) const;

    std::vector<std::string> usernames() const;
    const Account* find(const std::string& username) const;   // case-insensitive
    static bool validUsername(const std::string& name, std::string* why = nullptr);
    static bool validPassword(const std::string& pw, std::string* why = nullptr);

    bool create(const std::string& username, const std::string& password, const std::string& displayName, std::string* error = nullptr);
    bool verify(const std::string& username, const std::string& password) const;
    bool changePassword(const std::string& username, const std::string& oldPw, const std::string& newPw, std::string* error = nullptr);
    // Removes the account (its profile folder is kept on disk; the person's programs are never deleted).
    bool remove(const std::string& username, const std::string& password, std::string* error = nullptr);

    // ---- who is signed in ----
    bool login(const std::string& username, const std::string& password, std::string* error = nullptr);
    void loginGuest() { current_.clear(); guest_ = true; }
    void logout() { current_.clear(); guest_ = false; }
    bool signedIn() const { return guest_ || !current_.empty(); }
    bool isGuest() const { return guest_; }
    std::string currentUser() const { return guest_ ? "Guest" : current_; }
    std::string currentDisplayName() const;

    // Folder for a user's settings/session ("Guest" for guests).
    std::filesystem::path profileDir(const std::string& user) const;
    const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
    int iterations_;
    std::vector<Account> accounts_;
    std::string current_;
    bool guest_ = false;
    Account* findMutable(const std::string& username);
};

} // namespace studio
