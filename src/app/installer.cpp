// installer.cpp -- downloads and sets up external tools (official sources only, checksum verified when published).
#include "toolbox.h"
#include "archive.h"
#include "fsutil.h"
#include "core/sha256.h"
#include "core/executor.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
namespace he {

static std::string humanSize(uint64_t b) {
    char buf[40];
    if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.1f GB", b / 1073741824.0);
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.0f MB", b / 1048576.0);
    else std::snprintf(buf, sizeof buf, "%.0f KB", b / 1024.0);
    return buf;
}

bool Toolbox::installable(const std::string& toolId, std::string* why) const {
    std::vector<heng::InstallSpec> cat = heng::installCatalog(env_.windows, env_.is64);
    if (heng::findInstallFor(cat, toolId)) return true;
    if (why) {
        const heng::ToolDef* d = heng::findToolDef(toolId);
        if (!d) *why = "unknown tool";
        else if (d->needs64 && !env_.is64) *why = d->name + " needs a 64-bit system";
        else *why = "Happy cannot download " + d->name + " for this system. " + (env_.windows ? d->installWin : d->installLinux) + "  (" + d->url + ")";
    }
    return false;
}

void Toolbox::setState(const std::string& stage, const std::string& target, uint64_t done, uint64_t total, bool active) {
    std::lock_guard<std::mutex> lk(mu_); istate_.stage = stage; istate_.target = target; istate_.done = done; istate_.total = total; istate_.active = active;
}
InstallState Toolbox::installState() const { std::lock_guard<std::mutex> lk(mu_); return istate_; }
InstallOutcome Toolbox::lastInstall() const { std::lock_guard<std::mutex> lk(mu_); return ilast_; }

bool Toolbox::pendingApproval(InstallRequest& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (!pendingActive_ || pendingAnswer_ != 0) return false;
    out = pending_; return true;
}
void Toolbox::answerApproval(bool yes) {
    { std::lock_guard<std::mutex> lk(mu_); if (!pendingActive_) return; pendingAnswer_ = yes ? 1 : 2; }
    cv_.notify_all();
}
bool Toolbox::askApproval(const InstallRequest& r, std::atomic<bool>* cancel) {
    if (autoApprove_.load()) return true;
    std::unique_lock<std::mutex> lk(mu_);
    pending_ = r; pendingActive_ = true; pendingAnswer_ = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    while (pendingAnswer_ == 0) {
        if (cv_.wait_until(lk, std::chrono::steady_clock::now() + std::chrono::milliseconds(300)) == std::cv_status::timeout) {
            if ((cancel && cancel->load()) || installCancel_.load() || std::chrono::steady_clock::now() > deadline) { pendingAnswer_ = 2; break; }
        }
    }
    bool yes = pendingAnswer_ == 1; pendingActive_ = false; pendingAnswer_ = 0;
    return yes;
}

bool Toolbox::extractZip(const std::string& zipPath, const std::string& dest, std::string& err, std::atomic<bool>* cancel) {
    ZipReader z; if (!z.open(zipPath, err)) return false;
    std::vector<ArchiveEntry> es = z.entries(); size_t n = 0;
    std::error_code ec; fs::create_directories(fs::u8path(dest), ec);
    for (const ArchiveEntry& e : es) {
        if (cancel && cancel->load()) { err = "cancelled"; return false; }
        if ((++n & 31) == 0) setState("Unpacking", "", n, es.size(), true);
        std::string norm; std::string nm = e.name; while (!nm.empty() && nm.back() == '/') nm.pop_back();
        if (nm.empty()) continue;
        if (!heng::normalizeRelPath(nm, norm)) { err = "refusing unsafe path in archive: " + e.name; return false; }   // blocks ../ and absolute paths
        fs::path target = fs::u8path(dest) / fs::u8path(norm);
        if (e.isDir || e.name.back() == '/') { fs::create_directories(target, ec); continue; }
        fs::create_directories(target.parent_path(), ec);
        std::vector<uint8_t> data; std::string e2;
        if (!z.extract(e.name, data, e2)) { err = "cannot unpack " + e.name + ": " + e2; return false; }
        std::ofstream f(target, std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot write " + target.string(); return false; }
        f.write((const char*)data.data(), (std::streamsize)data.size());
        f.close();
#ifndef _WIN32
        chmod(target.c_str(), 0755);
#endif
    }
    return true;
}

static std::string findJavaHome(const std::string& toolsDir) {
    std::error_code ec;
    fs::path jdk = fs::u8path(toolsDir) / "jdk";
    for (fs::directory_iterator it(jdk, ec), end; !ec && it != end; it.increment(ec)) {
        fs::path p = it->path();
        if (fs::exists(p / "bin" / "java", ec) || fs::exists(p / "bin" / "java.exe", ec)) return p.string();
    }
    return std::string();
}

void Toolbox::postInstall(const std::string& package, const std::string& dir) {
    if (package != "ghidra") return;
    std::string java = findJavaHome(env_.toolsDir); if (java.empty()) return;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(fs::u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        if (it.depth() > 2) { it.disable_recursion_pending(); continue; }
        if (it->path().filename() != "launch.properties") continue;
        std::ifstream in(it->path(), std::ios::binary); std::string txt((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); in.close();
        std::string out, line; size_t pos = 0; bool done = false;
        while (pos <= txt.size()) {
            size_t nl = txt.find('\n', pos); line = txt.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            std::string trimmed = line; while (!trimmed.empty() && (trimmed.back() == '\r')) trimmed.pop_back();
            if (trimmed.compare(0, 18, "JAVA_HOME_OVERRIDE") == 0 || trimmed.compare(0, 19, "#JAVA_HOME_OVERRIDE") == 0) { if (!done) { out += "JAVA_HOME_OVERRIDE=" + java + "\n"; done = true; } }
            else if (nl != std::string::npos || !trimmed.empty()) out += trimmed + "\n";
            if (nl == std::string::npos) break; pos = nl + 1;
        }
        if (!done) out += "JAVA_HOME_OVERRIDE=" + java + "\n";
        std::ofstream o(it->path(), std::ios::binary | std::ios::trunc); o << out;
        return;
    }
}

InstallOutcome Toolbox::installPackage(const heng::InstallSpec& spec, bool approved, std::atomic<bool>* cancel, std::vector<std::string>& log) {
    InstallOutcome oc;
    if (!dl_) { oc.message = "network access is not available in this build"; return oc; }
    if (env_.toolsDir.empty()) { oc.message = "no tools folder configured"; return oc; }
    std::error_code ec; fs::create_directories(fs::u8path(env_.toolsDir + "/_downloads"), ec);
    // 1. dependencies
    std::vector<heng::InstallSpec> cat = heng::installCatalog(env_.windows, env_.is64);
    std::vector<std::string> also;
    for (const std::string& need : spec.needs) {
        const heng::InstallSpec* ns = heng::findInstallFor(cat, need);
        if (!ns) { oc.message = "dependency '" + need + "' cannot be installed here"; return oc; }
        const heng::ToolDef* nd = heng::findToolDef(ns->id);
        if (nd && status(nd->id).found) continue;
        also.push_back(ns->title);
    }
    // 2. resolve what to download
    setState("Looking up the latest version", spec.title, 0, 0, true);
    heng::ResolvedDownload rd; std::string err, body;
    if (spec.src == heng::InstallSrc::GitHub) {
        if (!dl_->getString("https://api.github.com/repos/" + spec.repo + "/releases/latest", body, err, cancel)) { oc.message = "cannot reach GitHub: " + err; return oc; }
        rd = heng::pickGithubAsset(body, spec.assetRegex);
    } else if (spec.src == heng::InstallSrc::Adoptium) {
        if (!dl_->getString("https://api.adoptium.net" + spec.url, body, err, cancel)) { oc.message = "cannot reach adoptium.net: " + err; return oc; }
        rd = heng::pickAdoptiumAsset(body);
    } else { rd.url = spec.url; rd.name = heng::baseNameNoDir(spec.url); }
    if (!rd.error.empty()) { oc.message = rd.error; return oc; }
    std::string host, path; bool tls; int port;
    if (!heng::splitUrl(rd.url, host, path, tls, port) || !heng::isAllowedDownloadHost(host)) { oc.message = "refusing to download from '" + rd.url + "' (not an official source)"; return oc; }
    // 3. disk space
    if (rd.size) {
        std::error_code e2; fs::space_info sp = fs::space(fs::u8path(env_.toolsDir), e2);
        if (!e2 && sp.available < rd.size * 2 + (200ull << 20)) { oc.message = "not enough free disk space: need about " + humanSize(rd.size * 2) + " in " + env_.toolsDir; return oc; }
    }
    // 4. ask the user
    InstallRequest rq; rq.package = spec.package; rq.title = spec.title; rq.fileName = rd.name; rq.url = rd.url; rq.size = rd.size; rq.note = spec.note;
    rq.destDir = env_.toolsDir + "/" + spec.package; rq.hasChecksum = !rd.sha256.empty(); rq.alsoInstalls = also;
    if (!approved && !askApproval(rq, cancel)) { oc.message = "the user did not allow the download"; return oc; }
    // 5. download
    std::string part = env_.toolsDir + "/_downloads/" + rd.name + ".part";
    std::string sha;
    setState("Downloading " + rd.name, spec.title, 0, rd.size, true);
    bool okDl = dl_->getFile(rd.url, part, sha, err, cancel, [this, &spec, &rd](uint64_t d, uint64_t t) { setState("Downloading " + rd.name, spec.title, d, t ? t : rd.size, true); });
    if (!okDl) { fs::remove(fs::u8path(part), ec); oc.message = "download failed: " + err; return oc; }
    if (!rd.sha256.empty()) {
        if (sha != rd.sha256) { fs::remove(fs::u8path(part), ec); oc.message = "checksum mismatch - the download was discarded (expected " + rd.sha256.substr(0, 12) + "..., got " + sha.substr(0, 12) + "...)"; return oc; }
        log.push_back("SHA-256 verified");
    } else log.push_back("no checksum published for this file; it came from the official source over HTTPS");
    // 6. unpack
    std::string dest = env_.toolsDir + "/" + spec.package;
    fs::remove_all(fs::u8path(dest), ec); fs::create_directories(fs::u8path(dest), ec);
    setState("Unpacking", spec.title, 0, 0, true);
    if (spec.format == "zip") {
        if (!extractZip(part, dest, err, cancel)) { fs::remove_all(fs::u8path(dest), ec); fs::remove(fs::u8path(part), ec); oc.message = "unpacking failed: " + err; return oc; }
    } else {
        RunResult r = runProcess({"tar", "-xf", part, "-C", dest}, "", 1800, cancel);
        if (!r.started || r.exitCode != 0) { fs::remove_all(fs::u8path(dest), ec); fs::remove(fs::u8path(part), ec); oc.message = "unpacking failed: " + (r.started ? r.output.substr(0, 500) : r.error); return oc; }
    }
    fs::remove(fs::u8path(part), ec);
    postInstall(spec.package, dest);
    oc.ok = true; oc.path = dest; oc.message = spec.title + " installed in " + dest;
    return oc;
}

InstallOutcome Toolbox::install(const std::string& toolId, bool approved, std::atomic<bool>* cancel) {
    InstallOutcome oc;
    if (installing_.exchange(true)) { oc.message = "another install is already running"; return oc; }
    oc = installImpl(toolId, approved, cancel);
    installing_ = false; setState("", "", 0, 0, false);
    { std::lock_guard<std::mutex> lk(mu_); ilast_ = oc; }
    return oc;
}

InstallOutcome Toolbox::installImpl(const std::string& toolId, bool approved, std::atomic<bool>* cancel) {
    InstallOutcome oc;
    std::vector<heng::InstallSpec> cat = heng::installCatalog(env_.windows, env_.is64);
    const heng::InstallSpec* spec = heng::findInstallFor(cat, toolId);
    if (!spec) { std::string why; installable(toolId, &why); oc.message = why; return oc; }
    const heng::ToolDef* def = heng::findToolDef(toolId);
    if (def && status(toolId).found) { oc.ok = true; oc.path = status(toolId).path; oc.message = def->name + " is already installed: " + oc.path; return oc; }
    std::vector<std::string> log;
    // dependencies first (e.g. the JDK for Ghidra)
    for (const std::string& need : spec->needs) {
        const heng::InstallSpec* ns = heng::findInstallFor(cat, need);
        if (!ns) continue;
        const heng::ToolDef* nd = heng::findToolDef(ns->id);
        if (nd && status(nd->id).found) continue;
        InstallOutcome o1 = installPackage(*ns, approved, cancel, log);
        if (!o1.ok) { oc.message = ns->title + ": " + o1.message; return oc; }
        rescan();
    }
    InstallOutcome o = installPackage(*spec, approved, cancel, log);
    if (o.ok) {
        setState("Checking the install", spec->title, 0, 0, true);
        rescan();
        ToolStatus ts = def ? status(toolId) : ToolStatus();
        if (def && !ts.found) { o.ok = false; o.message = spec->title + " was unpacked to " + o.path + " but Happy could not find the program inside. Use the Path... button on the Tools page."; }
        else if (def) { o.path = ts.path; o.message = def->name + " installed: " + ts.path; }
        for (const std::string& l : log) o.message += "\n" + l;
        if (def && def->id == "ghidra" && o.ok) o.message += "\nGhidra is ready: use decompile / the Decompile tab.";
    }
    return o;
}

void Toolbox::startInstall(const std::string& toolId) {
    if (installing_.exchange(true)) return;
    if (installThread_.joinable()) installThread_.join();
    installCancel_ = false;
    { std::lock_guard<std::mutex> lk(mu_); ilast_ = InstallOutcome(); }
    installThread_ = std::thread([this, toolId] {
        InstallOutcome o = installImpl(toolId, true, &installCancel_);
        installing_ = false; setState("", "", 0, 0, false);
        std::lock_guard<std::mutex> lk(mu_); ilast_ = o;
    });
}

} // namespace he
