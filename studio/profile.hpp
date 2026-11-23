// Everything that belongs to one signed-in person: settings, open tabs (session), recent files, user themes and the
// crash-recovery store for unsaved text. Stored as JSON under <data>/users/<name>/.
#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include "settings.hpp"
#include "theme.hpp"

namespace studio {

struct SessionTab {
    std::string path;          // empty for an untitled document (its text is in the recovery store under recoveryId)
    std::string recoveryId;
    int line = 0, col = 0;
    int topLine = 0;
};

struct WindowState {
    int x = -1, y = -1, w = 1280, h = 800;   // x/y = -1: let the system place it
    bool maximized = false;
    int explorerWidth = 240;
    int consoleHeight = 220;
};

struct Session {
    std::vector<SessionTab> tabs;
    int activeTab = 0;
    std::vector<std::string> recentFiles;       // most recent first, at most 12
    std::string explorerRoot;                   // empty = the user's programs folder
    WindowState window;
};

struct RecoveryItem { std::string id, title, originalPath, content; };

class UserProfile {
public:
    explicit UserProfile(std::filesystem::path dir) : dir_(std::move(dir)) {}

    Settings settings;
    Session session;
    std::set<std::string> disabledExtensions;

    bool load(std::string* warning = nullptr);   // missing files give defaults; damaged files are reported in *warning and ignored
    bool save(std::string* error = nullptr) const;

    void addRecent(const std::string& path);
    void removeRecent(const std::string& path);

    std::filesystem::path dir() const { return dir_; }
    std::filesystem::path themesDir() const { return dir_ / "themes"; }
    std::filesystem::path recoveryDir() const { return dir_ / "recovery"; }
    // Themes saved as .json files in themesDir(). Problems are appended to *errors, never thrown.
    std::vector<Theme> loadUserThemes(std::vector<std::string>* errors = nullptr) const;
    bool saveUserTheme(const Theme& t, std::string* error = nullptr) const;

    bool saveRecovery(const RecoveryItem& item, std::string* error = nullptr) const;
    std::vector<RecoveryItem> loadRecovery() const;
    void clearRecovery(const std::string& id) const;
    static std::string newRecoveryId();

private:
    std::filesystem::path dir_;
};

} // namespace studio
