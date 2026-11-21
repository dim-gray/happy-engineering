// analysis.h -- Happy Enginiring
// Turns raw, already-gathered data (file bytes, HTML text, process/module
// info) into a bounded text block to hand to the AI, plus the fixed
// system prompt describing the assistant's role. This module never touches
// the filesystem or the OS itself -- callers (main.cpp / win layer) read
// the file or enumerate the process and pass the results in. That split is
// what keeps this header portable and unit-testable on any platform.
#pragma once
#include <string>
#include <sstream>
#include <vector>
#include "pe.h"
#include "strings_extract.h"
#include "target.h"

namespace heng {

// The built-in, factory-default "personality"/system prompt. Shown
// pre-filled in the app's editable "Personalitate AI" box so the user can
// see exactly what's being sent and tweak it to their liking; "Reseteaza
// la implicit" in the UI restores this exact text.
inline const char* defaultSystemPrompt() {
    return
        "You are Happy Engineering, a friendly but precise reverse-engineering and game-modding assistant. "
        "You help the user modify programs and games they are entitled to modify: single-player and offline "
        "games, their own software, translations, re-texturing, balance tweaks, UI text. Base your answers "
        "strictly on the data you are given and say plainly when something is an assumption. Propose concrete "
        "changes as a plan the user reviews before anything happens. You do not help with license-key "
        "generation, DRM or anti-cheat bypass, malware, or cheating in online or multiplayer games. "
        "Answer in the language the user writes in.";
}

// Legacy single-shot plan protocol (the agent protocol in agent.h supersedes it).
inline const char* planProtocolPrompt() {
    return R"HEPLAN(PLAN PROTOCOL (fixed):
If the user asks for changes, explain briefly, then END the reply with exactly one code block tagged happy-plan containing valid JSON:
```happy-plan
{"summary":"short description","actions":[ ... ],"need_files":["relative/path"]}
```
Action types ("note" = reason, optional):
1. {"type":"replace_text","file":"relative/path","find":"exact existing text","replace":"new text","encoding":"utf8","all":true}
2. {"type":"replace_file","file":"relative/path","with_attachment":"name.ext"}
3. {"type":"replace_image","file":"relative/path.png","with_attachment":"name.jpg","fit":"cover"}
4. {"type":"patch_bytes","file":"relative/path","offset":"0x1A2B","expect":"74 05","write":"EB 05"}
5. {"type":"mem_write","label":"label","value":"99999"}  (only labels from the memory list)
Rules: only files from the list you were given; never invent offsets, addresses or content; ask for files via "need_files" instead of guessing. Max 40 actions.)HEPLAN";
}

// Trims the user-edited system prompt; falls back to the default if it was
// cleared to blank (so an empty box can never silently send zero
// instructions to the model).
inline std::string effectiveSystemPrompt(const std::string& custom) {
    size_t a = custom.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return defaultSystemPrompt();
    size_t b = custom.find_last_not_of(" \t\r\n");
    return custom.substr(a, b - a + 1);
}

// What is actually sent as the system message: the personality (custom or
// default) followed by the fixed plan protocol.
inline std::string fullSystemPrompt(const std::string& customPersonality) {
    return effectiveSystemPrompt(customPersonality) + "\n\n" + planProtocolPrompt();
}

inline std::string truncateWithNote(const std::string& s, size_t maxLen) {
    if (s.size() <= maxLen) return s;
    return s.substr(0, maxLen) + "\n...[truncated, " + std::to_string(s.size() - maxLen) + " characters omitted]";
}

// ---- File (exe/dll/any binary) -----------------------------------------
inline std::string buildFileContext(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ostringstream o;
    o << "## Target file\n";
    o << "Path: " << path << "\n";
    o << "Size: " << bytes.size() << " bytes\n\n";

    PeInfo pe = parsePe(bytes.data(), bytes.size());
    if (pe.valid) {
        o << "## Header PE\n";
        o << "Arhitectura: " << pe.machineName << (pe.is64 ? " (PE32+)" : " (PE32)") << "\n";
        o << "Tip: " << (pe.isDll ? "DLL" : "EXE") << "\n";
        o << "Subsistem: " << pe.subsystemName << "\n";
        o << "Entry point RVA: 0x" << std::hex << pe.entryPointRva << std::dec << "\n";
        o << "Image base: 0x" << std::hex << pe.imageBase << std::dec << "\n";
        o << "Timestamp link (raw): " << pe.timestamp << "\n";
        o << "Numar sectiuni: " << pe.numberOfSections << "\n\n";

        o << "## Sectiuni\n";
        for (auto& s : pe.sections) {
            o << "- " << s.name << "  VA=0x" << std::hex << s.virtualAddress
              << " VSize=0x" << s.virtualSize << " RawSize=0x" << s.rawSize << std::dec << "\n";
        }
        o << "\n";

        o << "## Imports (DLL -> some functions)\n";
        size_t shown = 0;
        for (auto& imp : pe.imports) {
            if (shown++ >= 40) { o << "... (remaining imports omitted)\n"; break; }
            o << "- " << imp.dll << ": ";
            for (size_t i = 0; i < imp.functions.size() && i < 12; ++i) {
                if (i) o << ", ";
                o << imp.functions[i];
            }
            if (imp.functions.size() > 12) o << ", ...";
            o << "\n";
        }
        o << "\n";

        if (!pe.exports.empty()) {
            o << "## Exports\n";
            for (size_t i = 0; i < pe.exports.size() && i < 60; ++i) o << "- " << pe.exports[i] << "\n";
            if (pe.exports.size() > 60) o << "... (restul exporturilor omise)\n";
            o << "\n";
        }
    } else {
        o << "## Header PE\n(not a recognised PE file: " << pe.error << ")\n\n";
    }

    auto strs = extractInterestingStrings(bytes.data(), bytes.size(), 5, 250);
    o << "## Relevant strings (sample, max 250)\n";
    for (auto& s : strs) o << "- " << s << "\n";

    return truncateWithNote(o.str(), 16000);
}

// ---- HTML -----------------------------------------------------------------
inline std::string buildHtmlContext(const std::string& path, const std::string& htmlText) {
    std::ostringstream o;
    o << "## Target HTML file\n";
    o << "Path: " << path << "\n";
    o << "Size: " << htmlText.size() << " characters\n\n";

    // naive, dependency-free scan for a few interesting tag attributes
    auto collect = [&](const char* attr) {
        std::vector<std::string> found;
        std::string needle = std::string(attr) + "=\"";
        size_t pos = 0;
        while ((pos = htmlText.find(needle, pos)) != std::string::npos && found.size() < 60) {
            size_t start = pos + needle.size();
            size_t end = htmlText.find('"', start);
            if (end == std::string::npos) break;
            found.push_back(htmlText.substr(start, end - start));
            pos = end + 1;
        }
        return found;
    };
    auto srcs = collect("src");
    auto hrefs = collect("href");

    if (!srcs.empty()) {
        o << "## Resurse incarcate (src=...)\n";
        for (auto& s : srcs) o << "- " << s << "\n";
        o << "\n";
    }
    if (!hrefs.empty()) {
        o << "## Legaturi / stiluri (href=...)\n";
        for (auto& h : hrefs) o << "- " << h << "\n";
        o << "\n";
    }

    o << "## Continut HTML (poate fi trunchiat)\n";
    o << truncateWithNote(htmlText, 10000);
    return truncateWithNote(o.str(), 16000);
}

// ---- Process ------------------------------------------------------------
struct ModuleSummary {
    std::string name;
    std::string path;
};

inline std::string buildProcessContext(uint32_t pid, const std::string& processName,
                                        const std::string& mainModulePath,
                                        const std::vector<ModuleSummary>& modules,
                                        const std::string& mainModulePeContext /* from buildFileContext, or empty */) {
    std::ostringstream o;
    o << "## Target process\n";
    o << "PID: " << pid << "\n";
    o << "Name: " << processName << "\n";
    o << "Main executable: " << mainModulePath << "\n\n";

    o << "## Loaded modules (" << modules.size() << ")\n";
    size_t shown = 0;
    for (auto& m : modules) {
        if (shown++ >= 80) { o << "... (remaining modules omitted)\n"; break; }
        o << "- " << m.name << "  (" << m.path << ")\n";
    }
    o << "\n";

    if (!mainModulePeContext.empty()) {
        o << "## Analiza statica a executabilului principal\n";
        o << mainModulePeContext << "\n";
    }

    return truncateWithNote(o.str(), 16000);
}

// Combines the user's free-text instructions with the gathered context into
// the final prompt sent as the "user" message/content.
inline std::string buildUserPrompt(const std::string& instructions, const std::string& context) {
    std::ostringstream o;
    o << "# User instructions\n" << (instructions.empty() ? "(none -- do a general analysis)" : instructions) << "\n\n";
    o << "# Extracted context about the target\n" << context;
    return o.str();
}

} // namespace heng
