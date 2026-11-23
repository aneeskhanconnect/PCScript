#include <cctype>
#include <cstdio>
#include "profile.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include "files.hpp"
#include "scan.hpp"

namespace studio {
namespace fs = std::filesystem;

static std::string safeFileName(const std::string& s) {
    std::string o;
    for (char c : s) o += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ') ? c : '_';
    if (o.empty()) o = "theme";
    return o.substr(0, 60);
}

bool UserProfile::load(std::string* warning) {
    settings = Settings{};
    session = Session{};
    disabledExtensions.clear();
    auto warn = [&](const std::string& m) { if (warning) *warning += (warning->empty() ? "" : "\n") + m; };

    std::string text, err;
    json::Value v;
    if (fs::exists(dir_ / "settings.json")) {
        if (!files::readText(dir_ / "settings.json", text, &err)) warn(err);
        else if (!json::parse(text, v, &err)) warn("settings.json is damaged (" + err + "); default settings are used.");
        else settings.fromJson(v);
    }
    if (fs::exists(dir_ / "session.json")) {
        json::Value s;
        if (!files::readText(dir_ / "session.json", text, &err) || !json::parse(text, s, &err)) warn("session.json could not be read; starting with no open files.");
        else {
            for (const json::Value& t : s.get("tabs").items()) {
                SessionTab st; st.path = t.get("path").asString(); st.recoveryId = t.get("recoveryId").asString();
                st.line = t.get("line").asInt(); st.col = t.get("col").asInt(); st.topLine = t.get("topLine").asInt();
                if (!st.path.empty() || !st.recoveryId.empty()) session.tabs.push_back(std::move(st));
            }
            session.activeTab = s.get("activeTab").asInt();
            for (const json::Value& r : s.get("recent").items()) if (r.isString() && session.recentFiles.size() < 12) session.recentFiles.push_back(r.asString());
            session.explorerRoot = s.get("explorerRoot").asString();
            const json::Value& w = s.get("window");
            session.window.x = w.get("x").asInt(-1); session.window.y = w.get("y").asInt(-1);
            session.window.w = std::max(640, w.get("w").asInt(1280)); session.window.h = std::max(480, w.get("h").asInt(800));
            session.window.maximized = w.get("maximized").asBool(false);
            session.window.explorerWidth = std::max(120, std::min(600, w.get("explorerWidth").asInt(240)));
            session.window.consoleHeight = std::max(80, std::min(900, w.get("consoleHeight").asInt(220)));
        }
    }
    if (fs::exists(dir_ / "extensions.json")) {
        json::Value e;
        if (files::readText(dir_ / "extensions.json", text, &err) && json::parse(text, e, &err))
            for (const json::Value& d : e.get("disabled").items()) if (d.isString()) disabledExtensions.insert(d.asString());
    }
    if (session.activeTab < 0 || session.activeTab >= static_cast<int>(session.tabs.size())) session.activeTab = 0;
    return true;
}

bool UserProfile::save(std::string* error) const {
    if (!files::ensureDir(dir_, error)) return false;
    if (!files::writeTextAtomic(dir_ / "settings.json", json::dump(settings.toJson()), error)) return false;
    json::Value s = json::Value::object(), tabs = json::Value::array(), recent = json::Value::array(), w = json::Value::object();
    for (const SessionTab& t : session.tabs) {
        json::Value o = json::Value::object();
        o.set("path", t.path); o.set("recoveryId", t.recoveryId); o.set("line", t.line); o.set("col", t.col); o.set("topLine", t.topLine);
        tabs.push(std::move(o));
    }
    for (const std::string& r : session.recentFiles) recent.push(r);
    w.set("x", session.window.x); w.set("y", session.window.y); w.set("w", session.window.w); w.set("h", session.window.h);
    w.set("maximized", session.window.maximized); w.set("explorerWidth", session.window.explorerWidth); w.set("consoleHeight", session.window.consoleHeight);
    s.set("tabs", std::move(tabs)); s.set("activeTab", session.activeTab); s.set("recent", std::move(recent));
    s.set("explorerRoot", session.explorerRoot); s.set("window", std::move(w));
    if (!files::writeTextAtomic(dir_ / "session.json", json::dump(s), error)) return false;
    json::Value e = json::Value::object(), d = json::Value::array();
    for (const std::string& x : disabledExtensions) d.push(x);
    e.set("disabled", std::move(d));
    return files::writeTextAtomic(dir_ / "extensions.json", json::dump(e), error);
}

void UserProfile::addRecent(const std::string& path) {
    removeRecent(path);
    session.recentFiles.insert(session.recentFiles.begin(), path);
    if (session.recentFiles.size() > 12) session.recentFiles.resize(12);
}
void UserProfile::removeRecent(const std::string& path) {
    std::string k = toLower(path);
    session.recentFiles.erase(std::remove_if(session.recentFiles.begin(), session.recentFiles.end(), [&](const std::string& p) { return toLower(p) == k; }), session.recentFiles.end());
}

std::vector<Theme> UserProfile::loadUserThemes(std::vector<std::string>* errors) const {
    std::vector<Theme> out;
    std::error_code ec;
    if (!fs::is_directory(themesDir(), ec)) return out;
    for (const files::DirEntry& e : files::listDir(themesDir())) {
        if (e.isDir || !files::hasExtension(e.full, ".json")) continue;
        std::string text, err; json::Value v; Theme t;
        if (!files::readText(e.full, text, &err) || !json::parse(text, v, &err) || !themeFromJson(v, t, &err)) {
            if (errors) errors->push_back(e.name + ": " + err);
            continue;
        }
        out.push_back(std::move(t));
    }
    return out;
}

bool UserProfile::saveUserTheme(const Theme& t, std::string* error) const {
    return files::writeTextAtomic(themesDir() / files::fromUtf8(safeFileName(t.name) + ".json"), json::dump(t.toJson()), error);
}

std::string UserProfile::newRecoveryId() {
    std::random_device rd;
    char b[32];
    std::snprintf(b, sizeof b, "%08x%08x", static_cast<unsigned>(rd()), static_cast<unsigned>(rd()));
    return b;
}

bool UserProfile::saveRecovery(const RecoveryItem& item, std::string* error) const {
    json::Value v = json::Value::object();
    v.set("title", item.title); v.set("path", item.originalPath); v.set("content", item.content);
    return files::writeTextAtomic(recoveryDir() / files::fromUtf8(safeFileName(item.id) + ".json"), json::dump(v), error);
}

std::vector<RecoveryItem> UserProfile::loadRecovery() const {
    std::vector<RecoveryItem> out;
    std::error_code ec;
    if (!fs::is_directory(recoveryDir(), ec)) return out;
    for (const files::DirEntry& e : files::listDir(recoveryDir())) {
        if (e.isDir || !files::hasExtension(e.full, ".json")) continue;
        std::string text; json::Value v;
        if (!files::readText(e.full, text) || !json::parse(text, v)) continue;
        RecoveryItem r; r.id = files::toUtf8(e.full.stem()); r.title = v.get("title").asString(); r.originalPath = v.get("path").asString(); r.content = v.get("content").asString();
        out.push_back(std::move(r));
    }
    return out;
}

void UserProfile::clearRecovery(const std::string& id) const {
    std::error_code ec;
    fs::remove(recoveryDir() / files::fromUtf8(safeFileName(id) + ".json"), ec);
}

} // namespace studio
