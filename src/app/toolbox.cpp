#include "toolbox.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
namespace he {

static std::string readCapped(const std::string& path, size_t cap) {
    std::ifstream f(fs::u8path(path), std::ios::binary); if (!f) return std::string();
    std::string s(cap, '\0'); f.read(&s[0], (std::streamsize)cap); s.resize((size_t)f.gcount()); return s;
}
static std::string safeName(const std::string& n) {
    std::string r; for (char c : n) r.push_back((std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? c : '_');
    return r.empty() ? std::string("file") : r;
}

Toolbox::~Toolbox() {
    cancel_ = true;
    if (scanThread_.joinable()) scanThread_.join();
    if (jobThread_.joinable()) jobThread_.join();
}

void Toolbox::configure(const ToolboxEnv& env, const std::map<std::string, std::string>& userPaths) {
    std::lock_guard<std::mutex> lk(mu_);
    env_ = env; userPaths_ = userPaths;
}

void Toolbox::writeSupportFiles() {
    std::error_code ec;
    fs::create_directories(fs::u8path(env_.workDir), ec);
    fs::create_directories(fs::u8path(decompDir()), ec);
    fs::create_directories(fs::u8path(buildDir()), ec);
    std::ofstream f(fs::u8path(env_.workDir + "/" + heng::ghidraScriptName()), std::ios::binary | std::ios::trunc);
    if (f) f << heng::ghidraScriptSource();
}

void Toolbox::rescan() {
    ToolboxEnv env; std::map<std::string, std::string> up;
    { std::lock_guard<std::mutex> lk(mu_); env = env_; up = userPaths_; }
    std::vector<ToolStatus> out;
    for (const heng::ToolDef& d : heng::toolCatalog()) {
        ToolStatus s; s.id = d.id;
        std::map<std::string, std::string>::const_iterator it = up.find(d.id);
        s.path = heng::locateTool(d, it == up.end() ? std::string() : it->second, env.pathDirs, env.roots, env.windows);
        s.found = !s.path.empty();
        if (s.found && !d.versionArg.empty()) {
            RunResult r = runProcess({s.path, d.versionArg}, "", 4, nullptr, nullptr, 4096);
            if (r.started) { std::string v = r.output; size_t nl = v.find('\n'); if (nl != std::string::npos) v = v.substr(0, nl); while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back(); if (v.size() > 90) v.resize(90); s.version = v; }
        }
        out.push_back(s);
    }
    std::lock_guard<std::mutex> lk(mu_); st_ = out;
}

void Toolbox::rescanAsync() {
    if (scanning_.exchange(true)) return;
    if (scanThread_.joinable()) scanThread_.join();
    scanThread_ = std::thread([this] { rescan(); scanning_ = false; });
}

std::vector<ToolStatus> Toolbox::statuses() const { std::lock_guard<std::mutex> lk(mu_); return st_; }
ToolStatus Toolbox::status(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const ToolStatus& s : st_) if (s.id == id) return s;
    ToolStatus n; n.id = id; return n;
}
std::string Toolbox::pathFor(const std::string& id) const { return status(id).path; }

std::string Toolbox::pickDecompiler(const std::string& wanted, std::string& err) const {
    if (!wanted.empty()) {
        const heng::ToolDef* d = heng::findToolDef(wanted);
        if (!d || !d->decompiles()) { err = "unknown decompiler '" + wanted + "'"; return std::string(); }
        if (!status(wanted).found) { err = d->name + " is not installed / not found (see the Tools page)"; return std::string(); }
        return wanted;
    }
    static const char* order[] = {"ghidra", "retdec", "radare2", "rizin", "objdump"};
    for (const char* id : order) {
        const heng::ToolDef* d = heng::findToolDef(id);
        if (!status(id).found) continue;
        if (d && d->needs64 && !env_.is64) continue;
        return id;
    }
    err = "no decompiler/disassembler found - install one on the Tools page (Ghidra, radare2, objdump ...)";
    return std::string();
}

std::string Toolbox::pickCompiler(const std::string& wanted, const std::string& lang, int bits, std::string& err) const {
    if (!wanted.empty()) {
        const heng::ToolDef* d = heng::findToolDef(wanted);
        if (!d || !d->builds()) { err = "unknown compiler '" + wanted + "'"; return std::string(); }
        if (!status(wanted).found) { err = d->name + " is not installed / not found (see the Tools page)"; return std::string(); }
        if (!heng::compilerSupports(*d, lang)) { err = d->name + " cannot build ." + lang + " files"; return std::string(); }
        return wanted;
    }
    std::vector<std::string> order;
    if (lang == "asm") order = {"nasm"};
    else if (env_.windows) {
        if (bits == 32) order = {"mingw32", lang == "cpp" ? "g++" : "gcc", "clang", "tcc", "msvc"};
        else if (bits == 64) order = {"mingw64", lang == "cpp" ? "g++" : "gcc", "clang", "msvc"};
        else order = {lang == "cpp" ? "g++" : "gcc", lang == "cpp" ? "clang++" : "clang", "mingw32", "mingw64", "msvc", "tcc"};
    } else {
        order = {lang == "cpp" ? "g++" : "gcc", lang == "cpp" ? "clang++" : "clang", "mingw64", "mingw32", "tcc"};
        if (bits == 32) order = {"mingw32", lang == "cpp" ? "g++" : "gcc", "clang"};
        if (bits == 64) order = {"mingw64", lang == "cpp" ? "g++" : "gcc", "clang"};
    }
    for (const std::string& id : order) {
        const heng::ToolDef* d = heng::findToolDef(id);
        if (d && status(id).found && heng::compilerSupports(*d, lang)) return id;
    }
    err = "no suitable " + std::string(lang == "asm" ? "assembler" : "compiler") + " found - install one on the Tools page";
    return std::string();
}

JobResult Toolbox::decompile(const std::string& toolId, const std::string& input, const std::string& function,
                             bool useCache, std::atomic<bool>* cancel) {
    JobResult jr; jr.title = "Decompile " + heng::baseNameNoDir(input);
    std::string err; std::string id = pickDecompiler(toolId, err);
    if (id.empty()) { jr.error = err; jr.done = true; return jr; }
    const heng::ToolDef* d = heng::findToolDef(id); jr.tool = d->name;
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(input), ec)) { jr.error = "input file not found: " + input; jr.done = true; return jr; }
    if (d->needs64 && !env_.is64) { jr.error = d->name + " needs a 64-bit system. Use radare2 or objdump here, or run Happy on a 64-bit PC."; jr.done = true; return jr; }
    writeSupportFiles();
    std::string tag = id + (function.empty() ? "" : "." + safeName(function));
    std::string base = decompDir() + "/" + safeName(heng::baseNameNoDir(input)) + "." + tag;
    heng::DecompileOpts o; o.function = function;
    heng::CmdSpec c = heng::buildDecompileCmd(*d, status(id).path, input, base, env_.workDir, o);
    if (!c.error.empty()) { jr.error = c.error; jr.done = true; return jr; }
    std::string outFile = c.outFile.empty() ? base + "." + c.outExt : c.outFile;
    if (useCache) {
        std::error_code e1, e2;
        auto tin = fs::last_write_time(fs::u8path(input), e1), tout = fs::last_write_time(fs::u8path(outFile), e2);
        if (!e1 && !e2 && tout >= tin && fs::file_size(fs::u8path(outFile), e2) > 0) {
            jr.ok = true; jr.fromCache = true; jr.outFile = outFile; jr.text = readCapped(outFile, 400000); jr.done = true; return jr;
        }
    }
    if (!c.mkdir.empty()) { fs::remove_all(fs::u8path(c.mkdir), ec); fs::create_directories(fs::u8path(c.mkdir), ec); }
    fs::remove(fs::u8path(outFile), ec);
    appendLog("$ " + [&] { std::string s; for (size_t i = 0; i < c.argv.size(); ++i) { if (i) s += ' '; s += c.argv[i].find(' ') != std::string::npos ? "\"" + c.argv[i] + "\"" : c.argv[i]; } return s; }() + "\n");
    int timeout = (id == "ghidra" || id == "retdec") ? 1800 : 300;
    RunResult r = runProcess(c.argv, env_.workDir, timeout, cancel, [this](const std::string& s) { appendLog(s); });
    if (!r.started) { jr.error = r.error; jr.done = true; return jr; }
    if (r.cancelled) { jr.error = "cancelled"; jr.done = true; return jr; }
    if (r.timedOut) { jr.error = "timed out after " + std::to_string(timeout) + " s"; jr.done = true; return jr; }
    if (c.outFile.empty()) {
        std::ofstream f(fs::u8path(outFile), std::ios::binary | std::ios::trunc); f << r.output;
    }
    std::string text = readCapped(outFile, 400000);
    if (text.empty()) {
        jr.error = "no output produced (exit code " + std::to_string(r.exitCode) + "). Tool log:\n" + r.output.substr(r.output.size() > 3000 ? r.output.size() - 3000 : 0);
        jr.done = true; return jr;
    }
    jr.ok = true; jr.outFile = outFile; jr.text = text; jr.done = true;
    return jr;
}

JobResult Toolbox::compile(const std::string& toolId, const std::vector<std::string>& sources, const std::string& outName,
                           const std::string& flags, int bits, std::atomic<bool>* cancel) {
    JobResult jr; jr.title = "Build " + outName;
    if (sources.empty()) { jr.error = "no source files"; jr.done = true; return jr; }
    std::string lang = heng::sourceLang(sources[0]);
    if (lang.empty()) { jr.error = "unknown source type (use .c, .cpp or .asm)"; jr.done = true; return jr; }
    std::string err; std::string id = pickCompiler(toolId, lang, bits, err);
    if (id.empty()) { jr.error = err; jr.done = true; return jr; }
    const heng::ToolDef* d = heng::findToolDef(id); jr.tool = d->name;
    writeSupportFiles();
    std::string nm = safeName(outName.empty() ? std::string("out") : outName);
    bool winOut = env_.windows || d->id == "mingw32" || d->id == "mingw64" || d->id == "msvc";
    if (lang != "asm" && winOut && nm.find('.') == std::string::npos) nm += ".exe";
    if (lang == "asm" && nm.find('.') == std::string::npos) nm += ".o";
    heng::CompileOpts o; o.sources = sources; o.output = buildDir() + "/" + nm; o.flags = flags; o.bits = bits; o.windowsHost = env_.windows;
    heng::CmdSpec c = heng::buildCompileCmd(*d, status(id).path, o);
    if (!c.error.empty()) { jr.error = c.error; jr.done = true; return jr; }
    std::error_code ec; fs::remove(fs::u8path(o.output), ec);
    std::string line = "$ ";
    for (size_t i = 0; i < c.argv.size(); ++i) { if (i) line += ' '; line += c.argv[i].find(' ') != std::string::npos ? "\"" + c.argv[i] + "\"" : c.argv[i]; }
    appendLog(line + "\n");
    RunResult r = runProcess(c.argv, env_.workDir, 300, cancel, [this](const std::string& s) { appendLog(s); });
    jr.text = r.output;
    if (!r.started) { jr.error = r.error; jr.done = true; return jr; }
    if (r.cancelled) { jr.error = "cancelled"; jr.done = true; return jr; }
    if (r.timedOut) { jr.error = "timed out"; jr.done = true; return jr; }
    if (r.exitCode != 0 || !fs::is_regular_file(fs::u8path(o.output), ec)) { jr.error = "build failed (exit code " + std::to_string(r.exitCode) + ")"; jr.done = true; return jr; }
    jr.ok = true; jr.outFile = o.output; jr.done = true;
    return jr;
}

void Toolbox::appendLog(const std::string& s) {
    std::lock_guard<std::mutex> lk(mu_);
    log_ += s; if (log_.size() > 600000) log_.erase(0, log_.size() - 400000);
}
std::string Toolbox::liveLog() const { std::lock_guard<std::mutex> lk(mu_); return log_; }
JobResult Toolbox::lastResult() const { std::lock_guard<std::mutex> lk(mu_); return last_; }

void Toolbox::joinJob() { if (jobThread_.joinable()) jobThread_.join(); }

void Toolbox::startDecompile(const std::string& toolId, const std::string& input, const std::string& function) {
    if (busy_.exchange(true)) return;
    joinJob(); cancel_ = false;
    { std::lock_guard<std::mutex> lk(mu_); log_.clear(); last_ = JobResult(); }
    jobThread_ = std::thread([=] {
        JobResult r = decompile(toolId, input, function, false, &cancel_);
        if (!r.ok) appendLog("\nERROR: " + r.error + "\n"); else appendLog("\nDone -> " + r.outFile + "\n");
        { std::lock_guard<std::mutex> lk(mu_); last_ = r; }
        busy_ = false;
    });
}
void Toolbox::startCompile(const std::string& toolId, const std::vector<std::string>& sources, const std::string& outName,
                           const std::string& flags, int bits) {
    if (busy_.exchange(true)) return;
    joinJob(); cancel_ = false;
    { std::lock_guard<std::mutex> lk(mu_); log_.clear(); last_ = JobResult(); }
    jobThread_ = std::thread([=] {
        JobResult r = compile(toolId, sources, outName, flags, bits, &cancel_);
        if (!r.ok) appendLog("\nERROR: " + r.error + "\n"); else appendLog("\nBuilt -> " + r.outFile + "\n");
        { std::lock_guard<std::mutex> lk(mu_); last_ = r; }
        busy_ = false;
    });
}

std::vector<std::string> Toolbox::listBuilds() const {
    std::vector<std::string> out; std::error_code ec;
    for (fs::directory_iterator it(fs::u8path(buildDir()), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) out.push_back(it->path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace he
