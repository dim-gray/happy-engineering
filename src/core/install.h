// install.h -- Happy Engineering
// Where each tool can be downloaded from (official release pages only), how to pick
// the right file for this OS/architecture, and the safety rules for downloads.
#pragma once
#include <string>
#include <vector>
#include <regex>
#include <cstdlib>
#include <cctype>
#include "json.h"

namespace heng {

enum class InstallSrc { GitHub, Adoptium, Url };

struct InstallSpec {
    std::string id;           // tool id this installs (several ids can share one package)
    std::string package;      // package folder name under <tools dir>
    std::string title;        // shown to the user
    InstallSrc src = InstallSrc::Url;
    std::string repo;         // GitHub "owner/name"
    std::string assetRegex;   // GitHub: asset file name pattern
    std::string url;          // Url source, or Adoptium query path
    std::string format;       // "zip" or "tar"
    std::vector<std::string> needs;   // packages that must be installed first (package names)
    std::string note;
};

// Packages that can be downloaded for this platform. Empty = not installable here.
inline std::vector<InstallSpec> installCatalog(bool windows, bool is64) {
    std::vector<InstallSpec> v;
    auto gh = [&](const std::string& id, const std::string& pkg, const std::string& title, const std::string& repo, const std::string& rx, const std::string& fmt) {
        InstallSpec s; s.id = id; s.package = pkg; s.title = title; s.src = InstallSrc::GitHub; s.repo = repo; s.assetRegex = rx; s.format = fmt; v.push_back(s); return &v.back(); };
    if (is64) {
        InstallSpec j; j.id = "java"; j.package = "jdk"; j.title = "Temurin JDK 21 (needed by Ghidra)"; j.src = InstallSrc::Adoptium;
        j.url = std::string("/v3/assets/latest/21/hotspot?architecture=x64&image_type=jdk&vendor=eclipse&os=") + (windows ? "windows" : "linux");
        j.format = windows ? "zip" : "tar"; v.push_back(j);
        InstallSpec* g = gh("ghidra", "ghidra", "Ghidra", "NationalSecurityAgency/ghidra", "^ghidra_[0-9.]+_PUBLIC_[0-9]+\\.zip$", "zip");
        g->needs.push_back("jdk"); g->note = "About 570 MB. Happy also sets up the JDK it needs.";
    }
    if (windows) {
        gh("radare2", "radare2", "radare2", "radareorg/radare2", is64 ? "^radare2-[0-9.]+-w64\\.zip$" : "^radare2-[0-9.]+-w32\\.zip$", "zip");
        if (is64) gh("rizin", "rizin", "Rizin", "rizinorg/rizin", "^rizin-windows-shared64-v[0-9.]+\\.zip$", "zip");
        InstallSpec n; n.id = "nasm"; n.package = "nasm"; n.title = "NASM 2.16.03"; n.src = InstallSrc::Url; n.format = "zip";
        n.url = std::string("https://www.nasm.us/pub/nasm/releasebuilds/2.16.03/") + (is64 ? "win64/nasm-2.16.03-win64.zip" : "win32/nasm-2.16.03-win32.zip"); v.push_back(n);
        // WinLibs: gcc, g++, objdump, ld ... one package for all of them
        const char* ids[] = {"gcc", "g++", "objdump", is64 ? "mingw64" : "mingw32"};
        for (const char* id : ids) {
            InstallSpec w; w.id = id; w.package = "winlibs"; w.title = std::string("MinGW-w64 GCC (WinLibs, ") + (is64 ? "64" : "32") + "-bit)"; w.src = InstallSrc::GitHub; w.repo = "brechtsanders/winlibs_mingw"; w.format = "zip";
            w.assetRegex = is64 ? "^winlibs-x86_64-posix-seh-gcc-[0-9.]+-mingw-w64ucrt-[0-9.]+-r[0-9]+\\.zip$" : "^winlibs-i686-posix-dwarf-gcc-[0-9.]+-mingw-w64ucrt-[0-9.]+-r[0-9]+\\.zip$";
            w.note = "About 270 MB: gcc, g++, objdump, ld, gdb and more. Needs the Windows UCRT (built into Windows 10/11; Windows 8.1 needs update KB2999226).";
            v.push_back(w);
        }
    } else if (is64) {
        gh("rizin", "rizin", "Rizin", "rizinorg/rizin", "^rizin-v[0-9.]+-static-x86_64\\.tar\\.xz$", "tar");
        gh("retdec", "retdec", "RetDec", "avast/retdec", "^RetDec-v[0-9.]+-Linux-Release\\.tar\\.xz$", "tar");
    }
    return v;
}

inline const InstallSpec* findInstallFor(const std::vector<InstallSpec>& cat, const std::string& idOrPackage) {
    for (const InstallSpec& s : cat) if (s.id == idOrPackage) return &s;
    for (const InstallSpec& s : cat) if (s.package == idOrPackage) return &s;
    return nullptr;
}

// ---- download safety ------------------------------------------------------
inline bool splitUrl(const std::string& url, std::string& host, std::string& path, bool& tls, int& port) {
    tls = true; port = 0; size_t p = 0;
    if (url.compare(0, 8, "https://") == 0) p = 8; else return false;       // https only
    size_t slash = url.find('/', p); host = url.substr(p, slash == std::string::npos ? std::string::npos : slash - p);
    path = slash == std::string::npos ? "/" : url.substr(slash);
    size_t colon = host.find(':'); if (colon != std::string::npos) { port = std::atoi(host.c_str() + colon + 1); host.resize(colon); }
    return !host.empty();
}
// Only these official hosts may be contacted for installs (redirects from them go to GitHub's CDN).
inline bool isAllowedDownloadHost(const std::string& hostIn) {
    std::string h; for (char c : hostIn) h.push_back((char)std::tolower((unsigned char)c));
    static const char* ok[] = {"api.github.com", "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com",
                               "api.adoptium.net", "www.nasm.us"};
    for (const char* a : ok) if (h == a) return true;
    return false;
}

struct ResolvedDownload { std::string name, url, sha256; uint64_t size = 0; std::string error; };

inline std::string lowerHex(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

// GitHub "releases/latest" JSON -> the asset matching the pattern
inline ResolvedDownload pickGithubAsset(const std::string& json, const std::string& assetRegex) {
    ResolvedDownload r; bool ok = false; JsonValue j = parseJson(json, &ok);
    if (!ok || !j.isObject()) { r.error = "could not read the release information"; return r; }
    const JsonValue* assets = j.find("assets");
    if (!assets || !assets->isArray() || !assets->arr) { r.error = "release has no assets"; return r; }
    std::regex rx(assetRegex);
    for (const JsonValue& a : *assets->arr) {
        std::string n = a.getString("name");
        if (!std::regex_match(n, rx)) continue;
        r.name = n; r.url = a.getString("browser_download_url");
        const JsonValue* sz = a.find("size"); if (sz && sz->type == JsonType::Number) r.size = (uint64_t)sz->num;
        std::string d = a.getString("digest"); if (d.compare(0, 7, "sha256:") == 0) r.sha256 = lowerHex(d.substr(7));
        return r;
    }
    r.error = "no matching download in the latest release (the project may have renamed its files)";
    return r;
}

// Adoptium "assets/latest" JSON -> the package link + checksum
inline ResolvedDownload pickAdoptiumAsset(const std::string& json) {
    ResolvedDownload r; bool ok = false; JsonValue j = parseJson(json, &ok);
    if (!ok || !j.isArray() || !j.arr || j.arr->empty()) { r.error = "could not read the JDK information"; return r; }
    for (const JsonValue& rel : *j.arr) {
        const JsonValue* bins = rel.find("binaries"); if (!bins || !bins->isArray() || !bins->arr) continue;
        for (const JsonValue& b : *bins->arr) {
            const JsonValue* pk = b.find("package"); if (!pk) continue;
            r.name = pk->getString("name"); r.url = pk->getString("link"); r.sha256 = lowerHex(pk->getString("checksum"));
            const JsonValue* sz = pk->find("size"); if (sz && sz->type == JsonType::Number) r.size = (uint64_t)sz->num;
            if (!r.url.empty()) return r;
        }
    }
    r.error = "no JDK download found"; return r;
}

} // namespace heng
