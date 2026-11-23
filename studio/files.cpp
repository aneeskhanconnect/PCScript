#include "files.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include "scan.hpp"

namespace studio::files {

fs::path fromUtf8(const std::string& s) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }
std::string toUtf8(const fs::path& p) { auto u = p.u8string(); return std::string(reinterpret_cast<const char*>(u.data()), u.size()); }

bool readText(const fs::path& p, std::string& out, std::string* error) {
    std::error_code ec;
    if (!fs::exists(p, ec)) { if (error) *error = "The file does not exist: " + toUtf8(p); return false; }
    if (fs::is_directory(p, ec)) { if (error) *error = "That is a folder, not a file: " + toUtf8(p); return false; }
    std::ifstream f(p, std::ios::binary);
    if (!f) { if (error) *error = "Cannot open the file (is it open in another program?): " + toUtf8(p); return false; }
    std::stringstream ss; ss << f.rdbuf();
    out = ss.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF && static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) out.erase(0, 3);
    return true;
}

bool ensureDir(const fs::path& p, std::string* error) {
    std::error_code ec;
    fs::create_directories(p, ec);
    if (ec && !fs::is_directory(p)) { if (error) *error = "Cannot create the folder " + toUtf8(p) + ": " + ec.message(); return false; }
    return true;
}

bool writeTextAtomic(const fs::path& p, const std::string& data, std::string* error) {
    if (p.has_parent_path() && !ensureDir(p.parent_path(), error)) return false;
    fs::path tmp = p; tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) { if (error) *error = "Cannot write to " + toUtf8(tmp) + " (is the folder read-only?)"; return false; }
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f) { if (error) *error = "Writing failed (disk full?): " + toUtf8(tmp); std::error_code ig; fs::remove(tmp, ig); return false; }
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) {   // some systems refuse to rename over an existing file
        fs::remove(p, ec);
        ec.clear();
        fs::rename(tmp, p, ec);
        if (ec) { if (error) *error = "Cannot replace " + toUtf8(p) + ": " + ec.message(); std::error_code ig; fs::remove(tmp, ig); return false; }
    }
    return true;
}

std::string uniqueFileName(const fs::path& dir, const std::string& base, const std::string& ext) {
    std::error_code ec;
    for (int i = 1; i < 10000; ++i) {
        std::string n = base + std::to_string(i) + ext;
        if (!fs::exists(dir / fromUtf8(n), ec)) return n;
    }
    return base + "_new" + ext;
}

bool hasExtension(const fs::path& p, const std::string& ext) { return studio::toLower(toUtf8(p.extension())) == ext; }

std::vector<DirEntry> listDir(const fs::path& dir) {
    std::vector<DirEntry> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        DirEntry e; e.full = it->path(); e.name = toUtf8(it->path().filename());
        if (e.name.empty() || e.name[0] == '.') continue;
        std::error_code e2; e.isDir = it->is_directory(e2);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return studio::toLower(a.name) < studio::toLower(b.name);
    });
    return out;
}

bool copyTree(const fs::path& from, const fs::path& to, std::string* error) {
    std::error_code ec;
    fs::create_directories(to, ec);
    fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) { if (error) *error = "Copy failed: " + ec.message(); return false; }
    return true;
}

unsigned long long treeSize(const fs::path& dir) {
    unsigned long long n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2)) n += it->file_size(e2);
    }
    return n;
}

} // namespace studio::files
