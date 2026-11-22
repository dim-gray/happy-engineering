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
#include "process.h"

namespace he {

struct ToolboxEnv {
    std::vector<std::string> pathDirs;   // PATH entries
    std::vector<std::string> roots;      // folders that may contain unpacked tools
    std::string workDir;                 // scratch + results (decomp/, build/)
    bool windows = false;
    bool is64 = true;                    // 64-bit system?
};

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

private:
    void writeSupportFiles();
    void joinJob();
    void appendLog(const std::string& s);

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
