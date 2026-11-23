// File helpers: UTF-8 paths, safe (atomic) saving, directory listing. No OS-specific code.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace studio::files {

namespace fs = std::filesystem;

fs::path fromUtf8(const std::string& s);
std::string toUtf8(const fs::path& p);

// Reads a whole file. A UTF-8 byte order mark is removed. Fails (with a readable message) for missing/unreadable files.
bool readText(const fs::path& p, std::string& out, std::string* error = nullptr);
// Writes to a temporary file in the same folder and renames it over the target, so a crash or full disk can never
// leave a half-written program behind. Creates the folder if needed.
bool writeTextAtomic(const fs::path& p, const std::string& data, std::string* error = nullptr);
bool ensureDir(const fs::path& p, std::string* error = nullptr);
// "Untitled" -> Untitled1.pcs, Untitled2.pcs ... (first name that does not exist in dir)
std::string uniqueFileName(const fs::path& dir, const std::string& base, const std::string& ext);
bool hasExtension(const fs::path& p, const std::string& extLowerWithDot);

struct DirEntry { std::string name; bool isDir = false; fs::path full; };
// Folders first, then files, each sorted case-insensitively; names starting with '.' are skipped.
std::vector<DirEntry> listDir(const fs::path& dir);
// Copies a folder recursively (used when installing extensions). Returns false and a message on failure.
bool copyTree(const fs::path& from, const fs::path& to, std::string* error = nullptr);
unsigned long long treeSize(const fs::path& dir);

} // namespace studio::files
