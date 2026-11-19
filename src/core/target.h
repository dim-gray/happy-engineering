// target.h -- Happy Enginiring
// Describes what the user picked to analyze: a file on disk (exe/dll/etc.),
// an HTML file, or a running process by PID.
#pragma once
#include <string>
#include <cstdint>

namespace heng {

enum class TargetType { None, File, Html, Folder, Process };

struct Target {
    TargetType type = TargetType::None;
    std::string path;     // for File / Html / Folder
    uint32_t pid = 0;     // for Process
    std::string processName; // display name, for Process

    bool isSet() const { return type != TargetType::None; }
};

} // namespace heng
