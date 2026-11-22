// Windows implementation of the platform layer.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wincrypt.h>
#include "platform.h"

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif

namespace he { namespace platform {

static std::wstring w(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n);
    return r;
}
static std::string u(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string r(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &r[0], n, nullptr, nullptr);
    return r;
}
static std::string folder(int csidl) {
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, csidl | CSIDL_FLAG_CREATE, nullptr, 0, buf))) return u(buf);
    return std::string();
}

std::string configDir() {
    std::string d = folder(CSIDL_APPDATA);
    if (d.empty()) d = exeDir();
    d += "\\HappyEngineering";
    CreateDirectoryW(w(d).c_str(), nullptr);
    return d;
}
std::string documentsDir() { std::string d = folder(CSIDL_PERSONAL); return d.empty() ? exeDir() : d; }
std::string exeDir() {
    wchar_t buf[MAX_PATH * 2] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::string p = u(buf);
    size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? "." : p.substr(0, s);
}
std::string homeDir() { std::string d = folder(CSIDL_PROFILE); return d.empty() ? "C:\\" : d; }
std::string osName() { return "Windows"; }

bool is64BitSystem() {
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    return si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 || si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64;
}

bool openInFileManager(const std::string& path) {
    return (INT_PTR)ShellExecuteW(nullptr, L"open", w(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

static std::string toHex(const std::string& in) {
    static const char* d = "0123456789abcdef";
    std::string o; for (unsigned char c : in) { o.push_back(d[c >> 4]); o.push_back(d[c & 15]); } return o;
}
static std::string fromHex(const std::string& in) {
    std::string o;
    for (size_t i = 0; i + 1 < in.size(); i += 2) o.push_back((char)std::stoi(in.substr(i, 2), nullptr, 16));
    return o;
}
std::string sealSecret(const std::string& plain) {
    if (plain.empty()) return std::string();
    DATA_BLOB in, out;
    in.pbData = (BYTE*)plain.data(); in.cbData = (DWORD)plain.size();
    if (!CryptProtectData(&in, L"HappyEngineering", nullptr, nullptr, nullptr, 0, &out)) return "plain:" + toHex(plain);
    std::string r = "dpapi:" + toHex(std::string((char*)out.pbData, out.cbData));
    LocalFree(out.pbData);
    return r;
}
std::string unsealSecret(const std::string& sealed) {
    if (sealed.empty()) return std::string();
    if (sealed.compare(0, 6, "plain:") == 0) return fromHex(sealed.substr(6));
    if (sealed.compare(0, 6, "dpapi:") != 0) return std::string();
    std::string raw = fromHex(sealed.substr(6));
    DATA_BLOB in, out;
    in.pbData = (BYTE*)raw.data(); in.cbData = (DWORD)raw.size();
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return std::string();
    std::string r((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return r;
}
const char* secretProtectionName() { return "Windows DPAPI (encrypted for your Windows account)"; }
bool setSecureFileMode(const std::string&) { return true; }

static std::string httpErr(const char* what, DWORD code) {
    std::string s = std::string(what) + " (error " + std::to_string((unsigned long)code) + ")";
    switch (code) {
        case 12002: s += ": timed out - the server did not answer in time"; break;
        case 12007: s += ": server name not found (check your internet / DNS)"; break;
        case 12029: s += ": cannot connect to the server (check internet / firewall / base URL)"; break;
        case 12030: s += ": connection was interrupted"; break;
        case 12175: s += ": TLS/SSL security error - check the PC date/time and Windows updates (TLS 1.2 root certificates)"; break;
        case 12038: case 12044: case 12045: s += ": the server certificate is not trusted"; break;
        default: break;
    }
    return s;
}
struct H { HINTERNET h; explicit H(HINTERNET x = nullptr) : h(x) {} ~H() { if (h) WinHttpCloseHandle(h); } };

HttpResult httpPost(const heng::ApiRequest& req, int timeoutSec, std::atomic<bool>* cancel) {
    HttpResult res;
    if (cancel && cancel->load()) { res.error = "cancelled"; return res; }
    H session(WinHttpOpen(L"HappyEngineering/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) { res.error = httpErr("WinHttpOpen failed", GetLastError()); return res; }
    int recvMs = timeoutSec * 1000;
    WinHttpSetTimeouts(session.h, 15000, 30000, 120000, recvMs);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    INTERNET_PORT port = req.port ? (INTERNET_PORT)req.port : (req.tls ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
    H conn(WinHttpConnect(session.h, w(req.host).c_str(), port, 0));
    if (!conn.h) { res.error = httpErr("WinHttpConnect failed", GetLastError()); return res; }
    H r(WinHttpOpenRequest(conn.h, L"POST", w(req.path).c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, req.tls ? WINHTTP_FLAG_SECURE : 0));
    if (!r.h) { res.error = httpErr("WinHttpOpenRequest failed", GetLastError()); return res; }
    std::wstring hdr;
    for (auto& kv : req.headers) hdr += w(kv.first) + L": " + w(kv.second) + L"\r\n";
    BOOL ok = WinHttpSendRequest(r.h, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), hdr.empty() ? 0 : (DWORD)hdr.size(),
                                 (LPVOID)req.body.data(), (DWORD)req.body.size(), (DWORD)req.body.size(), 0);
    if (!ok) { res.error = httpErr("WinHttpSendRequest failed", GetLastError()); return res; }
    if (!WinHttpReceiveResponse(r.h, nullptr)) { res.error = httpErr("WinHttpReceiveResponse failed", GetLastError()); return res; }
    DWORD code = 0, sz = sizeof(code);
    WinHttpQueryHeaders(r.h, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
    res.status = (int)code;
    for (;;) {
        if (cancel && cancel->load()) { res.error = "cancelled"; return res; }
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(r.h, &avail)) { if (res.body.empty()) { res.error = httpErr("reading the reply failed", GetLastError()); return res; } break; }
        if (!avail) break;
        std::vector<char> chunk(avail); DWORD got = 0;
        if (!WinHttpReadData(r.h, chunk.data(), avail, &got) || !got) break;
        res.body.append(chunk.data(), got);
    }
    res.sent = true;
    return res;
}

std::vector<ProcessInfo> listProcesses() {
    std::vector<ProcessInfo> out;
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(s, &pe)) do { if (pe.th32ProcessID) { ProcessInfo p; p.pid = pe.th32ProcessID; p.name = u(pe.szExeFile); out.push_back(p); } } while (Process32NextW(s, &pe));
    CloseHandle(s);
    return out;
}
bool processModules(uint32_t pid, std::string& mainPath, std::vector<heng::ModuleSummary>& mods) {
    HANDLE s = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 5; ++i) {
        s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (s != INVALID_HANDLE_VALUE) break;
        DWORD e = GetLastError(); if (e != ERROR_PARTIAL_COPY && e != ERROR_BAD_LENGTH) break;
        Sleep(50);
    }
    if (s == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me; me.dwSize = sizeof(me);
    if (Module32FirstW(s, &me)) do { heng::ModuleSummary m; m.name = u(me.szModule); m.path = u(me.szExePath); mods.push_back(m); } while (Module32NextW(s, &me));
    CloseHandle(s);
    if (!mods.empty()) mainPath = mods[0].path;
    return !mods.empty();
}

typedef BOOL (WINAPI *IsWow64Fn)(HANDLE, PBOOL);
static bool wow(HANDLE h, bool& v) {
    HMODULE k = GetModuleHandleW(L"kernel32.dll"); if (!k) return false;
    IsWow64Fn fn = (IsWow64Fn)(void*)GetProcAddress(k, "IsWow64Process"); if (!fn) return false;
    BOOL b = FALSE; if (!fn(h, &b)) return false; v = b != FALSE; return true;
}
class ProcMem : public heng::IMemory {
public:
    explicit ProcMem(HANDLE h) : h_(h) {}
    ~ProcMem() { CloseHandle(h_); }
    std::vector<heng::MemRegion> regions() override {
        std::vector<heng::MemRegion> out;
        SYSTEM_INFO si; GetSystemInfo(&si);
        uint64_t addr = (uint64_t)(uintptr_t)si.lpMinimumApplicationAddress, mx = (uint64_t)(uintptr_t)si.lpMaximumApplicationAddress;
        while (addr < mx) {
            MEMORY_BASIC_INFORMATION m;
            if (!VirtualQueryEx(h_, (LPCVOID)(uintptr_t)addr, &m, sizeof(m))) break;
            uint64_t base = (uint64_t)(uintptr_t)m.BaseAddress, size = (uint64_t)m.RegionSize;
            if (!size) break;
            if (m.State == MEM_COMMIT && !(m.Protect & (PAGE_GUARD | PAGE_NOACCESS)) && (m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
                if (!out.empty() && out.back().base + out.back().size == base) out.back().size += size;
                else { heng::MemRegion r; r.base = base; r.size = size; out.push_back(r); }
            }
            if (base + size <= addr) break;
            addr = base + size;
        }
        return out;
    }
    static bool fits(uint64_t a, size_t n) { const uint64_t mp = (uint64_t)~(uintptr_t)0; return a <= mp && (uint64_t)n <= mp - a; }
    bool read(uint64_t a, void* b, size_t n) override {
        if (!n || !fits(a, n)) return false; SIZE_T g = 0;
        return ReadProcessMemory(h_, (LPCVOID)(uintptr_t)a, b, n, &g) && g == n;
    }
    bool write(uint64_t a, const void* b, size_t n) override {
        if (!n || !fits(a, n)) return false; SIZE_T g = 0;
        return WriteProcessMemory(h_, (LPVOID)(uintptr_t)a, b, n, &g) && g == n;
    }
private: HANDLE h_;
};
std::unique_ptr<heng::IMemory> openProcessMemory(uint32_t pid, std::string& err) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | SYNCHRONIZE, FALSE, pid);
    if (!h) {
        DWORD c = GetLastError();
        err = "cannot open the process (error " + std::to_string((unsigned long)c) + ")";
        if (c == ERROR_ACCESS_DENIED) err += ": access denied - run Happy Engineering as administrator; protected processes cannot be opened at all";
        else if (c == ERROR_INVALID_PARAMETER) err += ": the process no longer exists";
        return nullptr;
    }
    bool self = false, target = false;
    if (wow(GetCurrentProcess(), self) && wow(h, target) && self && !target) {
        CloseHandle(h);
        err = "the target is a 64-bit process but this is the 32-bit build, which cannot read its memory. Use the 64-bit build.";
        return nullptr;
    }
    return std::unique_ptr<heng::IMemory>(new ProcMem(h));
}
bool processAlive(uint32_t pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}
std::string memoryAccessHint() { return "Some processes need Happy Engineering to run as administrator. A 32-bit build cannot read 64-bit processes."; }

}} // namespace
#endif
