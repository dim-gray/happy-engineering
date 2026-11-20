// fsutil.h -- small UTF-8 friendly filesystem helpers (std::filesystem based).
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>
#include <filesystem>

namespace he {
namespace fs = std::filesystem;

inline fs::path P(const std::string& utf8) { return fs::u8path(utf8); }
inline std::string S(const fs::path& p) { return p.generic_u8string(); }   // always '/' separators

inline bool readWholeFile(const std::string& path, std::vector<uint8_t>& out, std::string& err, uint64_t maxBytes = 512ull * 1024 * 1024) {
    std::error_code ec;
    uint64_t sz = fs::file_size(P(path), ec);
    if (ec) { err = ec.message(); return false; }
    if (sz > maxBytes) { err = "file too large (" + std::to_string(sz) + " bytes)"; return false; }
    std::ifstream f(P(path), std::ios::binary);
    if (!f) { err = "cannot open file"; return false; }
    out.resize((size_t)sz);
    if (sz) f.read((char*)out.data(), (std::streamsize)sz);
    if (!f && sz) { err = "read error"; return false; }
    return true;
}
inline bool writeWholeFile(const std::string& path, const std::vector<uint8_t>& data, std::string& err) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    std::ofstream f(P(path), std::ios::binary | std::ios::trunc);
    if (!f) { err = "cannot create " + path; return false; }
    if (!data.empty()) f.write((const char*)data.data(), (std::streamsize)data.size());
    f.flush();
    if (!f) { err = "write error on " + path; return false; }
    return true;
}
inline bool endsWithI(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    for (size_t i = 0; i < suf.size(); ++i) {
        char a = s[s.size() - suf.size() + i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}
inline bool isArchivePath(const std::string& name) {
    static const char* ex[] = {".zip", ".jar", ".apk", ".pk3", ".pk4", ".pk7", ".xpi", ".love", ".docx", ".xlsx", ".pptx", ".odt", ".epub", ".wad.zip", ".pak.zip", nullptr};
    for (int i = 0; ex[i]; ++i) if (endsWithI(name, ex[i])) return true;
    return false;
}
// "a/b.zip!/x/y.png" -> {"a/b.zip", "x/y.png"}; plain paths -> inner empty.
inline bool splitArchivePath(const std::string& rel, std::string& archive, std::string& inner) {
    size_t p = rel.find("!/");
    if (p == std::string::npos) { archive = rel; inner.clear(); return false; }
    archive = rel.substr(0, p); inner = rel.substr(p + 2);
    return true;
}
} // namespace he
