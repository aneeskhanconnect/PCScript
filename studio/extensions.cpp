#include <cctype>
#include "extensions.hpp"
#include <algorithm>
#include "files.hpp"
#include "json.hpp"
#include "scan.hpp"

namespace studio {
namespace fs = std::filesystem;

namespace {
bool validId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')) return false;
    return true;
}

// Resolves a path from a manifest and refuses anything that escapes the extension's own folder.
bool resolveInside(const fs::path& base, const std::string& rel, fs::path& out) {
    if (rel.empty()) return false;
    fs::path p = files::fromUtf8(rel);
    if (p.is_absolute() || p.has_root_name()) return false;
    std::error_code ec;
    fs::path full = fs::weakly_canonical(base / p, ec);
    fs::path b = fs::weakly_canonical(base, ec);
    auto bi = b.begin(), be = b.end();
    auto fi = full.begin(), fe = full.end();
    for (; bi != be; ++bi, ++fi) if (fi == fe || *fi != *bi) return false;
    out = full;
    return true;
}
} // namespace

ExtensionManager::ExtensionManager(fs::path bundled, fs::path user) : bundled_(std::move(bundled)), user_(std::move(user)) {}

bool ExtensionManager::loadManifest(const fs::path& dir, Extension& ex, std::string* error) {
    std::string text, err; json::Value v;
    fs::path mf = dir / "extension.json";
    if (!files::readText(mf, text, &err)) { if (error) *error = "no extension.json"; return false; }
    if (!json::parse(text, v, &err)) { if (error) *error = "extension.json is not valid JSON (" + err + ")"; return false; }
    ex = Extension{};
    ex.dir = dir;
    ex.id = v.get("id").asString(); ex.name = v.get("name").asString(); ex.version = v.get("version").asString("0.0.0");
    ex.author = v.get("author").asString(); ex.description = v.get("description").asString();
    if (!validId(ex.id)) { if (error) *error = "\"id\" must be 1-64 characters: lower-case letters, digits, '.', '-' or '_'"; return false; }
    if (ex.name.empty()) { if (error) *error = "\"name\" is missing"; return false; }

    const json::Value& c = v.get("contributes");
    for (const json::Value& t : c.get("themes").items()) {
        json::Value def = t;
        if (t.has("file")) {
            fs::path p; std::string body, e2;
            if (!resolveInside(dir, t.get("file").asString(), p) || !files::readText(p, body, &e2) || !json::parse(body, def, &e2)) {
                ex.problems.push_back("theme file '" + t.get("file").asString() + "' could not be loaded");
                continue;
            }
        }
        Theme th; std::string e3;
        if (themeFromJson(def, th, &e3)) ex.themes.push_back(std::move(th)); else ex.problems.push_back("theme: " + e3);
    }
    for (const json::Value& s : c.get("snippets").items()) {
        Snippet sn; sn.prefix = s.get("prefix").asString(); sn.description = s.get("description").asString(sn.prefix); sn.body = s.get("body").asString();
        bool ok = !sn.prefix.empty() && sn.prefix.size() <= 32 && !sn.body.empty() && sn.body.size() <= 20000;
        for (char ch : sn.prefix) if (!isWordChar(ch)) ok = false;
        if (ok) ex.snippets.push_back(std::move(sn)); else ex.problems.push_back("snippet '" + sn.prefix + "' is invalid (needs a word-character prefix and a body)");
    }
    for (const json::Value& t : c.get("templates").items()) {
        ProgramTemplate pt; pt.name = t.get("name").asString(); pt.description = t.get("description").asString();
        if (t.has("file")) {
            fs::path p; std::string e2;
            if (!resolveInside(dir, t.get("file").asString(), p) || !files::readText(p, pt.body, &e2)) { ex.problems.push_back("template file '" + t.get("file").asString() + "' could not be loaded"); continue; }
        } else pt.body = t.get("body").asString();
        if (pt.name.empty() || pt.body.size() > 200000) { ex.problems.push_back("a template has no name or is too large"); continue; }
        ex.templates.push_back(std::move(pt));
    }
    return true;
}

void ExtensionManager::scan() {
    exts_.clear();
    std::set<std::string> seen;
    auto scanRoot = [&](const fs::path& root, bool builtin) {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec)) return;
        for (const files::DirEntry& e : files::listDir(root)) {
            if (!e.isDir) continue;
            Extension x; std::string err;
            if (!loadManifest(e.full, x, &err)) {
                x = Extension{}; x.id = "invalid:" + e.name; x.name = e.name; x.dir = e.full; x.problems.push_back(err); x.enabled = false;
                x.builtin = builtin;
                exts_.push_back(std::move(x));
                continue;
            }
            if (!seen.insert(x.id).second) continue;   // the first copy wins (bundled before user)
            x.builtin = builtin;
            x.enabled = disabled_.count(x.id) == 0;
            exts_.push_back(std::move(x));
        }
    };
    scanRoot(bundled_, true);
    scanRoot(user_, false);
}

void ExtensionManager::setDisabled(const std::set<std::string>& ids) {
    disabled_ = ids;
    for (Extension& e : exts_) e.enabled = !disabled_.count(e.id) && e.id.rfind("invalid:", 0) != 0;
}

void ExtensionManager::setEnabled(const std::string& id, bool on) {
    if (on) disabled_.erase(id); else disabled_.insert(id);
    for (Extension& e : exts_) if (e.id == id) e.enabled = on;
}

std::vector<Theme> ExtensionManager::themes() const {
    std::vector<Theme> o;
    for (const Extension& e : exts_) if (e.enabled) o.insert(o.end(), e.themes.begin(), e.themes.end());
    return o;
}
std::vector<Snippet> ExtensionManager::snippets() const {
    std::vector<Snippet> o;
    for (const Extension& e : exts_) if (e.enabled) o.insert(o.end(), e.snippets.begin(), e.snippets.end());
    return o;
}
std::vector<ProgramTemplate> ExtensionManager::templates() const {
    std::vector<ProgramTemplate> o;
    for (const Extension& e : exts_) if (e.enabled) o.insert(o.end(), e.templates.begin(), e.templates.end());
    return o;
}

bool ExtensionManager::install(const fs::path& folder, std::string* error) {
    Extension x; std::string err;
    if (!loadManifest(folder, x, &err)) { if (error) *error = "This folder is not a valid extension: " + err; return false; }
    if (files::treeSize(folder) > 5ull * 1024 * 1024) { if (error) *error = "The extension is larger than 5 MB."; return false; }
    for (const Extension& e : exts_) if (e.id == x.id && e.builtin) { if (error) *error = "A built-in extension already uses the id '" + x.id + "'."; return false; }
    fs::path dest = user_ / files::fromUtf8(x.id);
    std::error_code ec;
    fs::remove_all(dest, ec);
    if (!files::copyTree(folder, dest, error)) return false;
    scan();
    return true;
}

bool ExtensionManager::uninstall(const std::string& id, std::string* error) {
    for (const Extension& e : exts_) {
        if (e.id != id) continue;
        if (e.builtin) { if (error) *error = "Built-in extensions cannot be uninstalled; disable it instead."; return false; }
        std::error_code ec;
        fs::remove_all(e.dir, ec);
        if (ec) { if (error) *error = "Could not remove " + files::toUtf8(e.dir) + ": " + ec.message(); return false; }
        scan();
        return true;
    }
    if (error) *error = "No such extension.";
    return false;
}

} // namespace studio
