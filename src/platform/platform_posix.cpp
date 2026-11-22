// Linux / POSIX implementation of the platform layer.
#ifndef _WIN32
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <curl/curl.h>
#include "platform.h"

namespace he { namespace platform {

static bool mkdirs(const std::string& p) {
    std::string cur;
    for (size_t i = 0; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/') { if (!cur.empty()) mkdir(cur.c_str(), 0700); }
        if (i < p.size()) cur.push_back(p[i]);
    }
    return true;
}
std::string homeDir() {
    const char* h = getenv("HOME");
    if (h && *h) return h;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/tmp";
}
std::string configDir() {
    const char* x = getenv("XDG_CONFIG_HOME");
    std::string d = (x && *x) ? std::string(x) : homeDir() + "/.config";
    d += "/happy-engineering";
    mkdirs(d);
    return d;
}
std::string documentsDir() {
    std::string d = homeDir() + "/Documents";
    struct stat st;
    return (stat(d.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) ? d : homeDir();
}
std::string exeDir() {
    char buf[4096]; ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = 0; std::string p = buf; size_t s = p.rfind('/');
    return s == std::string::npos ? "." : p.substr(0, s);
}
std::string osName() { return "Linux"; }

bool is64BitSystem() {
    struct utsname u; if (uname(&u) == 0) { std::string m = u.machine; return m.find("64") != std::string::npos; }
    return sizeof(void*) == 8;
}

bool openInFileManager(const std::string& path) {
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        setsid();
        int dn = open("/dev/null", O_RDWR); if (dn >= 0) { dup2(dn, 0); dup2(dn, 1); dup2(dn, 2); }
        execlp("xdg-open", "xdg-open", path.c_str(), (char*)nullptr);
        _exit(127);
    }
    signal(SIGCHLD, SIG_IGN);
    return true;
}

static std::string toHex(const std::string& in) {
    static const char* d = "0123456789abcdef";
    std::string o; for (unsigned char c : in) { o.push_back(d[c >> 4]); o.push_back(d[c & 15]); } return o;
}
static std::string fromHex(const std::string& in) {
    std::string o;
    for (size_t i = 0; i + 1 < in.size(); i += 2) o.push_back((char)std::strtol(in.substr(i, 2).c_str(), nullptr, 16));
    return o;
}
static std::string machineKey() {
    char host[256] = {0}; gethostname(host, sizeof(host) - 1);
    return std::string("HappyEngineering|") + homeDir() + "|" + host;
}
static std::string xorWith(const std::string& in) {
    std::string k = machineKey(), o(in.size(), '\0');
    for (size_t i = 0; i < in.size(); ++i) o[i] = in[i] ^ k[i % k.size()];
    return o;
}
std::string sealSecret(const std::string& plain) { return plain.empty() ? std::string() : "obf:" + toHex(xorWith(plain)); }
std::string unsealSecret(const std::string& s) {
    if (s.compare(0, 4, "obf:") != 0) return std::string();
    return xorWith(fromHex(s.substr(4)));
}
const char* secretProtectionName() { return "obfuscated and stored in a private (chmod 600) file - not strong encryption"; }
bool setSecureFileMode(const std::string& path) { return chmod(path.c_str(), 0600) == 0; }

static size_t writeCb(char* p, size_t sz, size_t n, void* ud) { ((std::string*)ud)->append(p, sz * n); return sz * n; }
static int progressCb(void* ud, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    std::atomic<bool>* c = (std::atomic<bool>*)ud; return (c && c->load()) ? 1 : 0;
}
HttpResult httpPost(const heng::ApiRequest& req, int timeoutSec, std::atomic<bool>* cancel) {
    HttpResult res;
    static bool inited = false; if (!inited) { curl_global_init(CURL_GLOBAL_DEFAULT); inited = true; }
    CURL* c = curl_easy_init();
    if (!c) { res.error = "libcurl init failed"; return res; }
    std::string url = std::string(req.tls ? "https://" : "http://") + req.host;
    if (req.port) url += ":" + std::to_string(req.port);
    url += req.path;
    struct curl_slist* hl = nullptr;
    for (auto& kv : req.headers) hl = curl_slist_append(hl, (kv.first + ": " + kv.second).c_str());
    char eb[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req.body.size());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, eb);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)timeoutSec);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progressCb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, cancel);
    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_OK) {
        long code = 0; curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        res.status = (int)code; res.sent = true;
    } else {
        res.error = (cancel && cancel->load()) ? "cancelled" : std::string("network error: ") + (eb[0] ? eb : curl_easy_strerror(rc));
    }
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    return res;
}

static bool readFileStr(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    std::ostringstream o; o << f.rdbuf(); out = o.str(); return true;
}
std::vector<ProcessInfo> listProcesses() {
    std::vector<ProcessInfo> out;
    DIR* d = opendir("/proc"); if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        ProcessInfo p; p.pid = (uint32_t)atoi(e->d_name);
        std::string comm; readFileStr(std::string("/proc/") + e->d_name + "/comm", comm);
        while (!comm.empty() && (comm.back() == '\n' || comm.back() == '\r')) comm.pop_back();
        if (comm.empty()) continue;
        p.name = comm; out.push_back(p);
    }
    closedir(d);
    return out;
}
bool processModules(uint32_t pid, std::string& mainPath, std::vector<heng::ModuleSummary>& mods) {
    char buf[4096]; ssize_t n = readlink(("/proc/" + std::to_string(pid) + "/exe").c_str(), buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = 0; mainPath = buf; }
    std::ifstream f("/proc/" + std::to_string(pid) + "/maps");
    if (!f) return !mainPath.empty();
    std::string line, last;
    while (std::getline(f, line)) {
        size_t s = line.find('/'); if (s == std::string::npos) continue;
        std::string path = line.substr(s);
        if (path == last) continue; last = path;
        bool dup = false; for (auto& m : mods) if (m.path == path) { dup = true; break; }
        if (dup) continue;
        heng::ModuleSummary m; m.path = path; size_t sl = path.rfind('/'); m.name = path.substr(sl + 1); mods.push_back(m);
    }
    return !mods.empty() || !mainPath.empty();
}

class ProcMem : public heng::IMemory {
public:
    ProcMem(uint32_t pid, int fd) : pid_(pid), fd_(fd) {}
    ~ProcMem() { close(fd_); }
    std::vector<heng::MemRegion> regions() override {
        std::vector<heng::MemRegion> out;
        std::ifstream f("/proc/" + std::to_string(pid_) + "/maps"); std::string line;
        while (std::getline(f, line)) {
            unsigned long long a = 0, b = 0; char perms[8] = {0};
            if (sscanf(line.c_str(), "%llx-%llx %7s", &a, &b, perms) != 3) continue;
            if (perms[0] != 'r' || perms[1] != 'w') continue;                // plain read/write data only
            if (line.find("[vvar]") != std::string::npos || line.find("[vsyscall]") != std::string::npos) continue;
            if (!out.empty() && out.back().base + out.back().size == a) out.back().size += b - a;
            else { heng::MemRegion r; r.base = a; r.size = b - a; out.push_back(r); }
        }
        return out;
    }
    bool read(uint64_t a, void* b, size_t n) override { return n && pread(fd_, b, n, (off_t)a) == (ssize_t)n; }
    bool write(uint64_t a, const void* b, size_t n) override { return n && pwrite(fd_, b, n, (off_t)a) == (ssize_t)n; }
private: uint32_t pid_; int fd_;
};
std::unique_ptr<heng::IMemory> openProcessMemory(uint32_t pid, std::string& err) {
    int fd = open(("/proc/" + std::to_string(pid) + "/mem").c_str(), O_RDWR);
    if (fd < 0) {
        err = std::string("cannot open /proc/") + std::to_string(pid) + "/mem: " + strerror(errno);
        if (errno == EACCES || errno == EPERM) err += ". Run Happy Engineering as the same user as the game (or with sudo), and make sure /proc/sys/kernel/yama/ptrace_scope is 0 (or grant CAP_SYS_PTRACE).";
        return nullptr;
    }
    return std::unique_ptr<heng::IMemory>(new ProcMem(pid, fd));
}
bool processAlive(uint32_t pid) { return kill((pid_t)pid, 0) == 0 || errno == EPERM; }
std::string memoryAccessHint() { return "Linux: the game must run as your user and ptrace_scope must allow it (or run with sudo)."; }

}} // namespace
#endif
