// Extensions: folders containing an extension.json that contribute colour themes, code snippets and program templates.
// Extensions are declarative data only - they cannot run code - so installing one cannot harm the computer.
//
//   extension.json
//   {
//     "id": "pcs.example", "name": "Example", "version": "1.0.0", "author": "...", "description": "...",
//     "contributes": {
//       "themes":    [ { "file": "themes/ocean.json" }, { "name": "Inline", "base": "dark", "colors": { "keyword": "#ff8800" } } ],
//       "snippets":  [ { "prefix": "fact", "description": "Factorial", "body": "FUNCTION Fact(N : INTEGER) RETURNS INTEGER\n..." } ],
//       "templates": [ { "name": "Menu program", "description": "...", "file": "templates/menu.pcs" } ]
//     }
//   }
#pragma once
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include "completion.hpp"
#include "theme.hpp"

namespace studio {

struct ProgramTemplate { std::string name, description, body; };

struct Extension {
    std::string id, name, version, author, description;
    std::filesystem::path dir;
    bool builtin = false;        // shipped with the IDE (cannot be uninstalled)
    bool enabled = true;
    std::vector<Theme> themes;
    std::vector<Snippet> snippets;
    std::vector<ProgramTemplate> templates;
    std::vector<std::string> problems;   // anything that was wrong in the manifest (the rest of it is still used)
};

class ExtensionManager {
public:
    // bundledDir: the extensions folder installed next to the program (may not exist). userDir: where installs go.
    ExtensionManager(std::filesystem::path bundledDir, std::filesystem::path userDir);

    void scan();
    const std::vector<Extension>& all() const { return exts_; }
    void setDisabled(const std::set<std::string>& ids);
    const std::set<std::string>& disabled() const { return disabled_; }
    void setEnabled(const std::string& id, bool on);

    std::vector<Theme> themes() const;                    // from enabled extensions
    std::vector<Snippet> snippets() const;
    std::vector<ProgramTemplate> templates() const;

    // Copies a folder that contains extension.json into the user extensions folder. Refuses invalid manifests and
    // anything larger than 5 MB.
    bool install(const std::filesystem::path& folder, std::string* error = nullptr);
    bool uninstall(const std::string& id, std::string* error = nullptr);

    static bool loadManifest(const std::filesystem::path& dir, Extension& out, std::string* error);

private:
    std::filesystem::path bundled_, user_;
    std::vector<Extension> exts_;
    std::set<std::string> disabled_;
};

} // namespace studio
