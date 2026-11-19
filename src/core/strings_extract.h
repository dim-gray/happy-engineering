// strings_extract.h -- Happy Enginiring
// Extracts printable ASCII and UTF-16LE string runs from a byte buffer.
// Pure, read-only, platform independent (same job as the classic `strings`
// utility). Used to build static-analysis context for the AI from a file
// on disk or from a bounded, read-only snapshot of a process's memory.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cctype>

namespace heng {

inline bool isPrintableByte(uint8_t c) {
    return (c >= 0x20 && c < 0x7F) || c == '\t';
}

// Extract ASCII strings of at least minLen printable characters.
inline std::vector<std::string> extractAsciiStrings(const uint8_t* data, size_t size, size_t minLen = 4, size_t maxCount = 4000) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < size && out.size() < maxCount; ++i) {
        uint8_t c = data[i];
        if (isPrintableByte(c)) {
            cur.push_back(static_cast<char>(c));
        } else {
            if (cur.size() >= minLen) out.push_back(cur);
            cur.clear();
        }
    }
    if (cur.size() >= minLen && out.size() < maxCount) out.push_back(cur);
    return out;
}

// Extract UTF-16LE strings (common in Windows PE resources/.rdata) of at
// least minLen characters, converted to UTF-8 for display (ASCII-range only;
// non-ASCII code units are skipped to keep this dependency-free).
inline std::vector<std::string> extractUtf16Strings(const uint8_t* data, size_t size, size_t minLen = 4, size_t maxCount = 4000) {
    std::vector<std::string> out;
    std::string cur;
    size_t i = 0;
    while (i + 1 < size && out.size() < maxCount) {
        uint16_t u = static_cast<uint16_t>(data[i]) | (static_cast<uint16_t>(data[i + 1]) << 8);
        if (u >= 0x20 && u < 0x7F) {
            cur.push_back(static_cast<char>(u));
            i += 2;
        } else {
            if (cur.size() >= minLen) out.push_back(cur);
            cur.clear();
            i += 1; // resync on odd boundary too
        }
    }
    if (cur.size() >= minLen && out.size() < maxCount) out.push_back(cur);
    return out;
}

// Convenience: ascii + utf16 strings, deduplicated, longest/most-interesting
// first is NOT guaranteed (kept in file order), capped at maxTotal entries
// so the result is safe to drop straight into an AI prompt.
inline std::vector<std::string> extractInterestingStrings(const uint8_t* data, size_t size, size_t minLen = 5, size_t maxTotal = 300) {
    auto a = extractAsciiStrings(data, size, minLen, maxTotal * 2);
    auto w = extractUtf16Strings(data, size, minLen, maxTotal * 2);
    std::vector<std::string> out;
    out.reserve(a.size() + w.size());
    for (auto& s : a) out.push_back(s);
    for (auto& s : w) out.push_back(s);
    // naive de-dup keeping first occurrence
    std::vector<std::string> dedup;
    dedup.reserve(out.size());
    for (auto& s : out) {
        bool seen = false;
        for (auto& d : dedup) if (d == s) { seen = true; break; }
        if (!seen) dedup.push_back(s);
        if (dedup.size() >= maxTotal) break;
    }
    return dedup;
}

} // namespace heng
