// Fake platform layer for headless screenshots / UI tests (no network, fake processes).
#include "platform/platform.h"
#include <cstdlib>
namespace he { namespace platform {
std::string configDir() { return "/tmp/claude-0/shot_cfg"; }
std::string documentsDir() { return "/tmp/claude-0/shot_docs"; }
std::string exeDir() { return "."; }
std::string homeDir() { return "/tmp/claude-0"; }
std::string osName() { return "Linux"; }
bool openInFileManager(const std::string&) { return true; }
bool is64BitSystem() { return true; }
std::string sealSecret(const std::string& p) { return p; }
std::string unsealSecret(const std::string& p) { return p; }
const char* secretProtectionName() { return "obfuscated and stored in a private (chmod 600) file - not strong encryption"; }
HttpResult httpPost(const heng::ApiRequest&, int, std::atomic<bool>*) { HttpResult r; r.error = "offline (screenshot build)"; return r; }
std::vector<ProcessInfo> listProcesses() {
    std::vector<ProcessInfo> v; const char* n[] = {"explorer.exe", "firefox", "vicecity.exe", "steam", "game.exe", "bash", "code", "dosbox.exe"};
    for (int i = 0; i < 8; ++i) { ProcessInfo p; p.pid = 1200 + i * 37; p.name = n[i]; v.push_back(p); }
    return v;
}
bool processModules(uint32_t, std::string& m, std::vector<heng::ModuleSummary>&) { m = "/tmp/claude-0/demo_game/game.exe"; return true; }
std::unique_ptr<heng::IMemory> openProcessMemory(uint32_t, std::string& e) { e = "demo"; return nullptr; }
bool processAlive(uint32_t) { return true; }
std::string memoryAccessHint() { return "Some processes need Happy Engineering to run as administrator."; }
bool setSecureFileMode(const std::string&) { return true; }
}}
