// context.h -- Happy Enginiring
// Builders for the extra context blocks given to the AI when the target is a
// folder, when files are attached, when files were requested via "need_files"
// and when the user has a memory watch list. Pure text assembly: the Windows
// layer lists folders / reads files and passes the results in.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <cstdint>
#include "analysis.h"
#include "attachments.h"
#include "scanner.h"
#include "textutil.h"

namespace heng {

struct FileEntry {
    std::string rel;     // relative path, '/' separators
    uint64_t size = 0;
};

struct TextSnippet {
    std::string rel;
    std::string content;   // decoded to UTF-8, possibly cut
    bool truncated = false;
};

// Which small text files (language files, configs...) are worth showing the
// AI up front. Likely-relevant names first, then smallest first, within a
// byte budget so the prompt stays a sensible size.
inline std::vector<std::string> selectSnippetFiles(const std::vector<FileEntry>& files,
                                                    size_t budgetBytes = 60000,
                                                    size_t maxFiles = 25,
                                                    uint64_t maxEach = 32768) {
    static const char* const hot[] = {"lang", "local", "string", "text", "dialog", "menu",
                                      "config", "setting", "option", "ui", nullptr};
    struct Cand { int prio; uint64_t size; std::string rel; };
    std::vector<Cand> cands;
    for (size_t i = 0; i < files.size(); ++i) {
        const FileEntry& f = files[i];
        if (f.size == 0 || f.size > maxEach || !isTextExtension(f.rel)) continue;
        std::string low = lowerCopy(f.rel);
        int prio = 1;
        for (const char* const* h = hot; *h; ++h)
            if (low.find(*h) != std::string::npos) { prio = 0; break; }
        Cand c; c.prio = prio; c.size = f.size; c.rel = f.rel;
        cands.push_back(c);
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
        if (a.prio != b.prio) return a.prio < b.prio;
        if (a.size != b.size) return a.size < b.size;
        return a.rel < b.rel;
    });
    std::vector<std::string> out;
    size_t used = 0;
    for (size_t i = 0; i < cands.size() && out.size() < maxFiles; ++i) {
        if (used + cands[i].size > budgetBytes) continue;
        used += static_cast<size_t>(cands[i].size);
        out.push_back(cands[i].rel);
    }
    return out;
}

inline std::string buildFolderContext(const std::string& root, const std::vector<FileEntry>& files,
                                      bool listTruncated, const std::vector<TextSnippet>& snippets,
                                      size_t maxListed = 400) {
    std::ostringstream o;
    o << "## Target folder\n";
    o << "Path: " << root << "\n";
    o << "Files found: " << files.size() << (listTruncated ? " (list was capped, more files exist)" : "") << "\n\n";

    std::map<std::string, size_t> ext;
    for (size_t i = 0; i < files.size(); ++i) {
        std::string e = extensionOf(files[i].rel);
        ext[e.empty() ? std::string("(fara extensie)") : "." + e]++;
    }
    std::vector<std::pair<size_t, std::string> > byCount;
    for (std::map<std::string, size_t>::const_iterator it = ext.begin(); it != ext.end(); ++it)
        byCount.push_back(std::make_pair(it->second, it->first));
    std::sort(byCount.begin(), byCount.end(),
              [](const std::pair<size_t, std::string>& a, const std::pair<size_t, std::string>& b) {
                  return a.first != b.first ? a.first > b.first : a.second < b.second; });
    o << "Extensions (file counts): ";
    for (size_t i = 0; i < byCount.size() && i < 14; ++i) {
        if (i) o << ", ";
        o << byCount[i].second << " x" << byCount[i].first;
    }
    o << "\n\n## File list (relative path, size)\n";
    for (size_t i = 0; i < files.size() && i < maxListed; ++i)
        o << "- " << files[i].rel << " (" << humanSize(files[i].size) << ")\n";
    if (files.size() > maxListed) o << "... (" << (files.size() - maxListed) << " files omitted from the list)\n";

    if (!snippets.empty()) {
        o << "\n## Contents of small text files (you can use this text in replace_text)\n";
        for (size_t i = 0; i < snippets.size(); ++i) {
            o << "\n### " << snippets[i].rel << "\n```\n" << snippets[i].content;
            if (snippets[i].truncated) o << "\n...[truncated]";
            o << "\n```\n";
        }
    }
    return truncateWithNote(o.str(), 70000);
}

struct AttachmentContextItem {
    std::string description;   // from describeAttachment()
    std::string textContent;   // for text attachments (already decoded/cut), else empty
    bool sentAsImage = false;  // the image itself is attached to the request
};

inline std::string buildAttachmentsContext(const std::vector<AttachmentContextItem>& items) {
    if (items.empty()) return std::string();
    std::ostringstream o;
    o << "## Attachments (files provided by the user; reference them by name in the plan)\n";
    for (size_t i = 0; i < items.size(); ++i) {
        o << items[i].description;
        if (items[i].sentAsImage) o << "  [the image itself is attached to this message]";
        o << "\n";
        if (!items[i].textContent.empty()) o << "```\n" << items[i].textContent << "\n```\n";
    }
    return truncateWithNote(o.str(), 30000);
}

inline std::string buildWatchContext(const std::vector<WatchEntry>& watch) {
    if (watch.empty()) return std::string();
    std::ostringstream o;
    o << "## Memory watch list (addresses chosen by the user; in a plan you may ONLY use these labels)\n";
    for (size_t i = 0; i < watch.size(); ++i) {
        char addr[32];
        std::snprintf(addr, sizeof(addr), "0x%llX", static_cast<unsigned long long>(watch[i].address));
        o << "- " << watch[i].label << ": " << valueTypeName(watch[i].type) << " at " << addr;
        if (!watch[i].lastText.empty()) o << ", current value " << watch[i].lastText;
        o << "\n";
    }
    return o.str();
}

// A file the AI asked to see via "need_files": text files in full (up to
// 40 KB), anything else as PE summary + extracted strings.
inline std::string buildExtraFileSection(const std::string& rel, const std::vector<uint8_t>& bytes) {
    std::ostringstream o;
    o << "### " << rel << " (" << humanSize(bytes.size()) << ")\n";
    if (looksLikeText(bytes.data(), bytes.size())) {
        std::string text = decodeTextBytes(bytes.data(), bytes.size());
        std::string cut = cutUtf8(text, 40000);
        o << "```\n" << cut << (cut.size() < text.size() ? "\n...[truncated]" : "") << "\n```\n";
    } else {
        o << buildFileContext(rel, bytes) << "\n";
    }
    return o.str();
}

inline std::string buildExtraFilesContext(const std::vector<std::string>& sections) {
    if (sections.empty()) return std::string();
    std::string s = "## Extra files requested by the AI\n";
    for (size_t i = 0; i < sections.size(); ++i) s += "\n" + sections[i];
    return truncateWithNote(s, 90000);
}

} // namespace heng
