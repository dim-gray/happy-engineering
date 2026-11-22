// process.h -- run an external program, capture stdout+stderr, support timeout/cancel.
#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace he {

struct RunResult {
    bool started = false, timedOut = false, cancelled = false;
    int exitCode = -1;
    std::string output;     // merged stdout + stderr (capped)
    std::string error;      // why it did not start
};

// argv[0] is the program (full path or on PATH). No shell is involved.
// onOutput (optional) is called from the calling thread with new text as it arrives.
RunResult runProcess(const std::vector<std::string>& argv, const std::string& workDir, int timeoutSec,
                     std::atomic<bool>* cancel, const std::function<void(const std::string&)>& onOutput = nullptr,
                     size_t maxOutput = 8u * 1024 * 1024);

} // namespace he
