// platform.h -- Happy Engineering
// Everything that differs between Windows and Linux lives behind this header.
#pragma once
#include <string>
#include <functional>
#include <vector>
#include <memory>
#include <atomic>
#include <cstdint>
#include "core/provider.h"
#include "core/analysis.h"
#include "core/scanner.h"

namespace he { namespace platform {

std::string configDir();      // where config.cfg lives (created on demand)
std::string documentsDir();   // user's Documents folder (default Output location root)
std::string exeDir();         // folder of the running executable
std::string homeDir();
bool is64BitSystem();          // false on 32-bit Windows/Linux (64-bit-only tools cannot run)
std::string osName();         // "Windows" / "Linux"
bool openInFileManager(const std::string& path);   // reveal a folder in Explorer / xdg-open

// Encrypt/obfuscate a secret for storing in the config file.
// Windows: DPAPI (bound to the Windows user). Linux: obfuscation + 0600 file mode.
std::string sealSecret(const std::string& plain);
std::string unsealSecret(const std::string& sealed);
const char* secretProtectionName();   // human-readable, shown in Settings

struct HttpResult {
    bool sent = false;
    int status = 0;
    std::string body;
    std::string error;
};
// Blocking HTTP(S) POST. Call from a worker thread. `cancel` may be null.
HttpResult httpPost(const heng::ApiRequest& req, int timeoutSec, std::atomic<bool>* cancel);

struct FetchResult { bool ok = false; int status = 0; std::string error; };
// Streaming HTTPS GET (follows redirects). The body goes to `sink` (return false to abort);
// `progress(done,total)` is optional (total 0 = unknown). Call from a worker thread.
FetchResult httpFetch(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers, int timeoutSec,
                      std::atomic<bool>* cancel, const std::function<bool(const char*, size_t)>& sink,
                      const std::function<void(uint64_t, uint64_t)>& progress);

struct ProcessInfo { uint32_t pid = 0; std::string name; };
std::vector<ProcessInfo> listProcesses();
// Fills the main executable path and the loaded modules (best effort).
bool processModules(uint32_t pid, std::string& mainPath, std::vector<heng::ModuleSummary>& mods);

// Memory access to another process (value scanner). Returns null + err on failure.
std::unique_ptr<heng::IMemory> openProcessMemory(uint32_t pid, std::string& err);
bool processAlive(uint32_t pid);
std::string memoryAccessHint();   // platform specific advice shown when opening fails

bool setSecureFileMode(const std::string& path);   // chmod 600 on Linux, no-op on Windows

}} // namespace he::platform
