// toolbox.h -- detects external RE/build tools and runs decompile / recompile jobs.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include "core/toolkit.h"
#include "core/install.h"
#include <condition_variable>
#include <functional>
#include "process.h"

namespace he {

struct ToolboxEnv {
    std::vector<std::string> pathDirs;   // PATH entries
    std::vector<std::string> roots;      // folders that may contain unpacked tools
    std::string workDir;                 // scratch + results (decomp/, build/)
    std::string toolsDir;                // where Happy installs downloaded tools
    bool windows = false;
    bool is64 = true;                    // 64-bit system?
};

// Network access for installs; implemented by the GUI with the platform HTTP layer (and by a fake in tests).
class IDownloader {
public:
    virtual ~IDownloader() {}
    virtual bool getString(const std::string& url, std::string& out, std::string& err, std::atomic<bool>* cancel) = 0;
    virtual bool getFile(const std::string& url, const std::string& dest, std::string& sha256Out, std::string& err, std::atomic<bool>* cancel,
                         const std::function<void(uint64_t, uint64_t)>& progress) = 0;
};

struct InstallRequest { std::string package, title, fileName, url, note, destDir; uint64_t size = 0; bool hasChecksum = false; std::vector<std::string> alsoInstalls; };
struct InstallState { bool active = false; std::string stage, target; uint64_t done = 0, total = 0; };
struct InstallOutcome { bool ok = false; std::string message, path; };

struct ToolStatus { std::string id; bool found = false; std::string path, version; };

struct JobResult {
    bool done = false, ok = false;
    std::string title, tool, outFile, text, error;   // text = (capped) result text or tool log
    bool fromCache = false;
};

class Toolbox {
public:
    Toolbox() {}
    ~Toolbox();
    void configure(const ToolboxEnv& env, const std::map<std::string, std::string>& userPaths);
    void rescan();                       // blocking; call from a worker or at start-up
    void rescanAsync();
    bool scanning() const { return scanning_.load(); }
    std::vector<ToolStatus> statuses() const;
    ToolStatus status(const std::string& id) const;
    std::string pathFor(const std::string& id) const;
    const ToolboxEnv& env() const { return env_; }
    std::string decompDir() const { return env_.workDir + "/decomp"; }
    std::string buildDir() const { return env_.workDir + "/build"; }

    // Pick the best installed tool for a job ("" = auto). Returns "" if none.
    std::string pickDecompiler(const std::string& wanted, std::string& err) const;
    std::string pickCompiler(const std::string& wanted, const std::string& lang, int bits, std::string& err) const;

    // Synchronous (used by the AI agent thread and by the background jobs).
    JobResult decompile(const std::string& toolId, const std::string& input, const std::string& function,
                        bool useCache, std::atomic<bool>* cancel);
    JobResult compile(const std::string& toolId, const std::vector<std::string>& sources, const std::string& outName,
                      const std::string& flags, int bits, std::atomic<bool>* cancel);

    // Background job for the UI.
    void startDecompile(const std::string& toolId, const std::string& input, const std::string& function);
    void startCompile(const std::string& toolId, const std::vector<std::string>& sources, const std::string& outName,
                      const std::string& flags, int bits);
    void cancelJob() { cancel_ = true; }
    bool busy() const { return busy_.load(); }
    std::string liveLog() const;
    JobResult lastResult() const;
    std::vector<std::string> listBuilds() const;     // files in buildDir

    // ---- downloading / installing tools -----------------------------------------
    void setDownloader(IDownloader* d) { dl_ = d; }
    void setAutoApprove(bool v) { autoApprove_ = v; }
    std::string toolsDir() const { return env_.toolsDir; }
    // Can this tool be downloaded on this system? why = hint when it cannot.
    bool installable(const std::string& toolId, std::string* why) const;
    // Blocking. approved=true skips the confirmation (the user pressed the button themselves).
    InstallOutcome install(const std::string& toolId, bool approved, std::atomic<bool>* cancel);
    void startInstall(const std::string& toolId);
    bool installing() const { return installing_.load(); }
    void cancelInstall() { installCancel_ = true; }
    InstallState installState() const;
    InstallOutcome lastInstall() const;
    bool pendingApproval(InstallRequest& out) const;
    void answerApproval(bool yes);

private:
    void writeSupportFiles();
    void joinJob();
    void appendLog(const std::string& s);

    InstallOutcome installImpl(const std::string& toolId, bool approved, std::atomic<bool>* cancel);
    InstallOutcome installPackage(const heng::InstallSpec& spec, bool approved, std::atomic<bool>* cancel, std::vector<std::string>& log);
    bool askApproval(const InstallRequest& r, std::atomic<bool>* cancel);
    bool extractZip(const std::string& zipPath, const std::string& dest, std::string& err, std::atomic<bool>* cancel);
    void setState(const std::string& stage, const std::string& target, uint64_t done, uint64_t total, bool active);
    void postInstall(const std::string& package, const std::string& dir);
    IDownloader* dl_ = nullptr;
    std::atomic<bool> autoApprove_{false}, installing_{false}, installCancel_{false};
    std::thread installThread_;
    InstallState istate_; InstallOutcome ilast_;
    bool pendingActive_ = false; int pendingAnswer_ = 0; InstallRequest pending_;
    std::condition_variable cv_;
    ToolboxEnv env_;
    std::map<std::string, std::string> userPaths_;
    mutable std::mutex mu_;
    std::vector<ToolStatus> st_;
    std::atomic<bool> scanning_{false}, busy_{false}, cancel_{false};
    std::thread scanThread_, jobThread_;
    std::string log_;
    JobResult last_;
};

} // namespace he
