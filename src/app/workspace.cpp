#include "workspace.h"
#include <ctime>
#include <set>
#include <algorithm>
#include <sstream>

namespace he {

static std::string joinRel(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + "/" + b;
}

void Workspace::setTarget(const heng::Target& t) {
    std::lock_guard<std::mutex> lk(mu_);
    zips_.clear(); cacheKey_.clear(); cacheData_.clear();
    root_.clear(); focus_.clear();
    std::error_code ec;
    if (t.type == heng::TargetType::Folder) {
        root_ = S(P(t.path));
        while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
    } else if (t.type == heng::TargetType::File || t.type == heng::TargetType::Html) {
        fs::path p = P(t.path);
        root_ = S(p.parent_path());
        focus_ = S(p.filename());
    } else if (t.type == heng::TargetType::Process) {
        // root is the folder of the main executable (set by the caller via a Folder-like target)
    }
}

bool Workspace::addAttachment(const std::string& path, std::string& err) {
    std::lock_guard<std::mutex> lk(mu_);
    std::error_code ec;
    fs::path p = P(path);
    if (!fs::is_regular_file(p, ec)) { err = "not a file"; return false; }
    AttachmentFile a; a.path = S(p); a.name = S(p.filename()); a.size = fs::file_size(p, ec);
    a.kind = heng::classifyByExtension(a.name);
    // unique name
    std::string base = a.name; int n = 2;
    auto exists = [&](const std::string& nm) { for (auto& x : att_) if (x.name == nm) return true; return false; };
    while (exists(a.name)) { size_t dot = base.rfind('.'); a.name = (dot == std::string::npos ? base : base.substr(0, dot)) + "_" + std::to_string(n++) + (dot == std::string::npos ? "" : base.substr(dot)); }
    att_.push_back(a);
    return true;
}
void Workspace::removeAttachment(size_t i) { std::lock_guard<std::mutex> lk(mu_); if (i < att_.size()) att_.erase(att_.begin() + (long)i); }
void Workspace::clearAttachments() { std::lock_guard<std::mutex> lk(mu_); att_.clear(); }
std::vector<std::string> Workspace::attachmentNames() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> r; for (auto& a : att_) r.push_back(a.name); return r;
}
bool Workspace::readAttachment(const std::string& name, std::vector<uint8_t>& out, std::string& err) {
    std::string path;
    { std::lock_guard<std::mutex> lk(mu_); for (auto& a : att_) if (a.name == name) path = a.path; }
    if (path.empty()) { err = "no attachment named '" + name + "'"; return false; }
    return readWholeFile(path, out, err);
}

ZipReader* Workspace::zipLocked(const std::string& archiveRel, std::string& err) {
    auto it = zips_.find(archiveRel);
    if (it != zips_.end()) return it->second.get();
    std::unique_ptr<ZipReader> z(new ZipReader());
    if (!z->open(joinRel(root_, archiveRel), err)) return nullptr;
    ZipReader* raw = z.get();
    zips_[archiveRel] = std::move(z);
    return raw;
}

bool Workspace::readFullLocked(const std::string& rel, std::vector<uint8_t>& out, std::string& err) {
    if (root_.empty()) { err = "no target folder selected"; return false; }
    std::string arc, inner;
    if (splitArchivePath(rel, arc, inner)) {
        ZipReader* z = zipLocked(arc, err);
        return z && z->extract(inner, out, err);
    }
    return readWholeFile(joinRel(root_, rel), out, err);
}
bool Workspace::readTarget(const std::string& rel, std::vector<uint8_t>& out, std::string& err) {
    std::lock_guard<std::mutex> lk(mu_);
    return readFullLocked(rel, out, err);
}

bool Workspace::read(const std::string& path, uint64_t offset, uint64_t len, std::vector<uint8_t>& out, uint64_t& total, std::string& err) {
    if (path.compare(0, 7, "_happy/") == 0) return virtualRead(path, offset, len, out, total, err);
    std::lock_guard<std::mutex> lk(mu_);
    if (root_.empty()) { err = "no target folder selected"; return false; }
    std::string arc, inner;
    if (!splitArchivePath(path, arc, inner)) {
        std::error_code ec;
        fs::path p = P(joinRel(root_, path));
        if (!fs::is_regular_file(p, ec)) { err = "no such file: " + path; return false; }
        total = fs::file_size(p, ec);
        std::ifstream f(p, std::ios::binary);
        if (!f) { err = "cannot open " + path; return false; }
        if (offset > total) offset = total;
        uint64_t n = std::min<uint64_t>(len, total - offset);
        out.resize((size_t)n);
        f.seekg((std::streamoff)offset);
        if (n) f.read((char*)out.data(), (std::streamsize)n);
        out.resize((size_t)f.gcount());
        return true;
    }
    if (cacheKey_ != path) {
        if (!readFullLocked(path, cacheData_, err)) { cacheKey_.clear(); return false; }
        cacheKey_ = path;
    }
    total = cacheData_.size();
    if (offset > total) offset = total;
    uint64_t n = std::min<uint64_t>(len, total - offset);
    out.assign(cacheData_.begin() + (long)offset, cacheData_.begin() + (long)(offset + n));
    return true;
}

bool Workspace::list(const std::string& dir, bool recursive, size_t maxEntries, std::vector<heng::HostEntry>& out, std::string& err) {
    if (dir == "_happy" || dir.compare(0, 7, "_happy/") == 0) {
        if (!tb_) { err = "tools are not available"; return false; }
        std::string rest = dir.size() > 7 ? dir.substr(7) : std::string();
        while (!rest.empty() && rest.back() == '/') rest.pop_back();
        if (rest.empty()) {
            heng::HostEntry a; a.path = "_happy/decomp"; a.isDir = true; a.size = 0; out.push_back(a);
            heng::HostEntry b; b.path = "_happy/build"; b.isDir = true; b.size = 0; out.push_back(b);
            return true;
        }
        if (rest != "decomp" && rest != "build") { err = "only _happy/decomp and _happy/build exist"; return false; }
        std::error_code ec;
        for (fs::directory_iterator it(P(tb_->env().workDir + "/" + rest), ec), end; !ec && it != end && out.size() < maxEntries; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            heng::HostEntry e; e.path = "_happy/" + rest + "/" + S(it->path().filename()); e.isDir = false; e.size = fs::file_size(it->path(), ec); out.push_back(e);
        }
        return true;
    }
    std::lock_guard<std::mutex> lk(mu_);
    if (root_.empty()) { err = "no target folder selected"; return false; }
    // inside an archive?
    std::string d = dir; while (!d.empty() && d.back() == '/') d.pop_back();
    size_t bang = d.find('!');
    if (bang != std::string::npos && (bang + 1 == d.size() || d[bang + 1] == '/')) {
        std::string arc = d.substr(0, bang);
        std::string prefix = bang + 2 <= d.size() ? d.substr(std::min(d.size(), bang + 2)) : std::string();
        if (!prefix.empty()) prefix += "/";
        ZipReader* z = zipLocked(arc, err);
        if (!z) return false;
        std::set<std::string> dirs;
        for (auto& e : z->entries()) {
            if (e.name.compare(0, prefix.size(), prefix) != 0) continue;
            std::string rest = e.name.substr(prefix.size());
            if (rest.empty()) continue;
            if (!recursive) {
                size_t sl = rest.find('/');
                if (sl != std::string::npos) { dirs.insert(rest.substr(0, sl)); continue; }
            }
            if (e.isDir) continue;
            heng::HostEntry h; h.path = arc + "!/" + e.name; h.size = e.size; out.push_back(h);
            if (out.size() >= maxEntries) break;
        }
        for (auto& dn : dirs) { heng::HostEntry h; h.path = arc + "!/" + prefix + dn; h.isDir = true; out.push_back(h); }
        return true;
    }
    std::error_code ec;
    fs::path base = P(joinRel(root_, d));
    if (!fs::is_directory(base, ec)) { err = "not a folder: " + dir; return false; }
    size_t archivesOpened = 0;
    auto addFile = [&](const fs::path& p, uint64_t size) {
        heng::HostEntry h; h.path = S(fs::relative(p, P(root_), ec)); h.size = size; out.push_back(h);
        if (recursive && isArchivePath(h.path) && archivesOpened < 40) {
            std::string e2; ZipReader* z = zipLocked(h.path, e2);
            if (z) { ++archivesOpened; for (auto& e : z->entries()) { if (e.isDir) continue; heng::HostEntry x; x.path = h.path + "!/" + e.name; x.size = e.size; out.push_back(x); if (out.size() >= maxEntries) return; } }
        }
    };
    if (recursive) {
        for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
            std::error_code e2;
            if (it->is_regular_file(e2)) addFile(it->path(), it->file_size(e2));
            if (out.size() >= maxEntries) break;
        }
    } else {
        for (fs::directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2)) { heng::HostEntry h; h.path = S(fs::relative(it->path(), P(root_), ec)); h.isDir = true; out.push_back(h); }
            else if (it->is_regular_file(e2)) addFile(it->path(), it->file_size(e2));
            if (out.size() >= maxEntries) break;
        }
    }
    std::sort(out.begin(), out.end(), [](const heng::HostEntry& a, const heng::HostEntry& b) { return a.path < b.path; });
    return true;
}

std::vector<heng::FileEntry> Workspace::scanFolder(size_t maxFiles, bool& truncated) {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<heng::FileEntry> r; truncated = false;
    if (root_.empty()) return r;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(P(root_), fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        if (r.size() >= maxFiles) { truncated = true; break; }
        heng::FileEntry f; f.rel = S(fs::relative(it->path(), P(root_), e2)); f.size = it->file_size(e2);
        if (f.rel.size() > 6 && f.rel.compare(f.rel.size() - 6, 6, ".hebak") == 0) continue;
        r.push_back(f);
    }
    return r;
}

static std::string timestamp() {
    char b[32]; std::time_t t = std::time(nullptr); std::tm* l = std::localtime(&t);
    std::strftime(b, sizeof b, "%Y-%m-%d_%H-%M-%S", l); return b;
}

SaveResult Workspace::saveOutputs(const std::map<std::string, std::vector<uint8_t> >& outputs, const std::string& outBase, const std::string& logText) {
    SaveResult r;
    std::lock_guard<std::mutex> lk(mu_);
    r.dir = outBase + "/" + timestamp();
    std::map<std::string, std::map<std::string, std::vector<uint8_t> > > byArchive;
    for (auto& kv : outputs) {
        std::string arc, inner;
        if (splitArchivePath(kv.first, arc, inner)) { byArchive[arc][inner] = kv.second; continue; }
        std::string err;
        if (!writeWholeFile(r.dir + "/" + kv.first, kv.second, err)) { r.error = err; return r; }
        r.files.push_back(kv.first);
    }
    for (auto& ab : byArchive) {
        std::error_code ec; fs::create_directories(P(r.dir + "/" + ab.first).parent_path(), ec);
        std::string err;
        if (!rewriteZip(joinRel(root_, ab.first), r.dir + "/" + ab.first, ab.second, err)) { r.error = "archive " + ab.first + ": " + err; return r; }
        r.files.push_back(ab.first);
    }
    std::string err;
    std::vector<uint8_t> logBytes(logText.begin(), logText.end());
    writeWholeFile(r.dir + "/_happy_engineering_log.txt", logBytes, err);
    r.ok = true;
    return r;
}

InstallResult Workspace::installOutputs(const std::string& outputDir, const std::string& targetRoot, bool makeBackups) {
    InstallResult r; std::error_code ec;
    if (!fs::is_directory(P(outputDir), ec)) { r.error = "output folder does not exist"; return r; }
    for (fs::recursive_directory_iterator it(P(outputDir), ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        std::string rel = S(fs::relative(it->path(), P(outputDir), e2));
        if (rel == "_happy_engineering_log.txt") continue;
        fs::path dst = P(targetRoot + "/" + rel);
        if (makeBackups && fs::exists(dst, e2)) {
            fs::path bak = dst; bak += ".hebak";
            if (!fs::exists(bak, e2)) {           // keep the TRUE original across repeated installs
                fs::copy_file(dst, bak, fs::copy_options::none, e2);
                if (e2) { r.error = "cannot back up " + rel + ": " + e2.message(); return r; }
                ++r.backedUp;
            }
        }
        fs::create_directories(dst.parent_path(), e2);
        fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, e2);
        if (e2) { r.error = "cannot write " + rel + ": " + e2.message(); return r; }
        r.installed.push_back(rel);
    }
    r.ok = true;
    return r;
}

std::vector<std::string> Workspace::findBackups(const std::string& targetRoot) {
    std::vector<std::string> r; std::error_code ec;
    for (fs::recursive_directory_iterator it(P(targetRoot), fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code e2;
        if (it->is_regular_file(e2) && it->path().extension() == ".hebak") r.push_back(S(fs::relative(it->path(), P(targetRoot), e2)));
    }
    return r;
}
int Workspace::restoreBackups(const std::string& targetRoot, std::string& err) {
    int n = 0;
    for (auto& rel : findBackups(targetRoot)) {
        fs::path bak = P(targetRoot + "/" + rel), orig = bak; orig.replace_extension("");
        std::error_code ec;
        fs::copy_file(bak, orig, fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "cannot restore " + rel + ": " + ec.message(); return n; }
        fs::remove(bak, ec);
        ++n;
    }
    return n;
}

} // namespace he

// ---------------------------------------------------------------- external tools (AI side)
namespace he {

bool Workspace::virtualRead(const std::string& path, uint64_t offset, uint64_t len, std::vector<uint8_t>& out, uint64_t& total, std::string& err) {
    if (!tb_) { err = "tools are not available"; return false; }
    std::string rest = path.substr(7);
    if (rest.compare(0, 7, "decomp/") != 0 && rest.compare(0, 6, "build/") != 0) { err = "only _happy/decomp/ and _happy/build/ exist"; return false; }
    std::string norm; if (!heng::normalizeRelPath(rest, norm)) { err = "bad path"; return false; }
    std::error_code ec; fs::path p = P(tb_->env().workDir + "/" + norm);
    if (!fs::is_regular_file(p, ec)) { err = "no such file: " + path; return false; }
    total = fs::file_size(p, ec);
    std::ifstream f(p, std::ios::binary); if (!f) { err = "cannot open " + path; return false; }
    if (offset > total) offset = total;
    uint64_t n = std::min<uint64_t>(len, total - offset);
    out.resize((size_t)n); f.seekg((std::streamoff)offset); if (n) f.read((char*)out.data(), (std::streamsize)n);
    out.resize((size_t)f.gcount());
    return true;
}

bool Workspace::materialize(const std::string& path, const std::string& attachment, std::string& real, std::string& err) {
    if (!attachment.empty()) {
        std::lock_guard<std::mutex> lk(mu_);
        for (const AttachmentFile& a : att_) if (a.name == attachment) { real = a.path; return true; }
        err = "no attachment named '" + attachment + "'"; return false;
    }
    std::string norm;
    if (!heng::normalizeRelPath(path, norm)) { err = "bad or missing \"path\""; return false; }
    std::string arc, inner;
    if (!splitArchivePath(norm, arc, inner)) {
        std::lock_guard<std::mutex> lk(mu_);
        if (root_.empty()) { err = "no target folder selected"; return false; }
        real = joinRel(root_, norm); return true;
    }
    std::vector<uint8_t> data; uint64_t total = 0;
    if (!read(norm, 0, (uint64_t)1 << 31, data, total, err)) return false;
    std::string dir = tb_->env().workDir + "/tmp"; std::error_code ec; fs::create_directories(P(dir), ec);
    real = dir + "/" + heng::baseNameOf(inner);
    std::ofstream f(P(real), std::ios::binary | std::ios::trunc);
    if (!f) { err = "cannot write temp file"; return false; }
    f.write((const char*)data.data(), (std::streamsize)data.size());
    return true;
}

std::string Workspace::external(const std::string& tool, const heng::JsonValue& a) {
    if (!tb_) return "ERROR: tools are not available";
    std::ostringstream o;
    if (tool == "list_tools") {
        std::vector<ToolStatus> st = tb_->statuses();
        if (st.empty()) return "(tool scan has not finished yet; try again in a moment)";
        for (const ToolStatus& s : st) {
            const heng::ToolDef* d = heng::findToolDef(s.id); if (!d) continue;
            o << (s.found ? "[installed] " : "[missing]   ") << s.id << " - " << d->name << " (" << heng::toolKindName(d->kind) << ")";
            if (s.found && !s.version.empty()) o << "  " << s.version;
            if (!s.found) { std::string why; o << (tb_->installable(s.id, &why) ? "  (can be installed with install_tool)" : "  (not installable by Happy: see the Tools page)"); }
            o << "\n";
        }
        o << "Use decompile on a file; use compile_code to test-build source. Missing tools can be installed from the Tools page.";
        return o.str();
    }
    std::string err;
    if (tool == "install_tool") {
        std::string id = a.getString("tool"); if (id.empty()) return "ERROR: missing \"tool\" (an id from list_tools)";
        InstallOutcome oc = tb_->install(id, false, nullptr);
        return (oc.ok ? "OK: " : "NOT INSTALLED: ") + oc.message;
    }
    if (tool == "decompile") {
        std::string real;
        if (!materialize(a.getString("path"), a.getString("attachment"), real, err)) return "ERROR: " + err;
        JobResult r = tb_->decompile(a.getString("tool"), real, a.getString("function"), true, nullptr);
        if (!r.ok) return "ERROR: " + r.error;
        std::string rel = "_happy/decomp/" + heng::baseNameOf(r.outFile);
        std::string t = r.text; bool cut = t.size() > 9000; if (cut) t = heng::cutUtf8(t, 9000);
        o << "--- " << r.tool << " output" << (r.fromCache ? " (cached)" : "") << ", saved as " << rel << " (" << r.text.size() << " chars) ---\n" << t;
        if (cut) o << "\n...[more: read_text {\"path\":\"" << rel << "\",\"offset\":9000} or search_text in _happy/decomp]";
        return o.str();
    }
    if (tool == "compile_code") {
        std::string code = a.getString("code");
        if (code.empty()) return "ERROR: missing \"code\"";
        if (code.size() > 400000) return "ERROR: code too large";
        std::string lang = a.getString("language"); if (lang.empty()) lang = "c";
        if (lang == "c++") lang = "cpp";
        if (lang != "c" && lang != "cpp" && lang != "asm") return "ERROR: language must be c, cpp or asm";
        std::string name = a.getString("name"); if (name.empty()) name = "happy_build";
        std::string clean; for (char c : name) clean.push_back((std::isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_');
        std::string arch = a.getString("arch"); int bits = arch == "32" ? 32 : arch == "64" ? 64 : 0;
        std::string dir = tb_->env().workDir + "/src"; std::error_code ec; fs::create_directories(P(dir), ec);
        std::string src = dir + "/" + clean + "." + lang;
        { std::ofstream f(P(src), std::ios::binary | std::ios::trunc); if (!f) return "ERROR: cannot write source"; f << code; }
        std::vector<std::string> srcs(1, src);
        JobResult r = tb_->compile(a.getString("compiler"), srcs, clean, a.getString("flags"), bits, nullptr);
        std::string log = r.text.size() > 6000 ? heng::cutUtf8(r.text, 6000) + "\n...[truncated]" : r.text;
        if (!r.ok) return "BUILD FAILED (" + r.tool + "): " + r.error + "\n" + log;
        o << "BUILD OK with " << r.tool << " -> _happy/build/" << heng::baseNameOf(r.outFile) << "\n" << (log.empty() ? "(no compiler messages)" : log);
        return o.str();
    }
    return "ERROR: unknown tool";
}

} // namespace he
