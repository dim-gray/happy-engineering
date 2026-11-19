// textutil.h -- Happy Enginiring
// Small, dependency-free text helpers: UTF-8 <-> UTF-16, BOM-aware decoding
// of text files, and a few string utilities shared by the patcher, the
// attachment inspector and the prompt builders.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstdio>

namespace heng {

inline std::string lowerCopy(std::string s) {
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') s[i] = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

inline void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// UTF-16 (little or big endian) -> UTF-8. Unpaired surrogates become U+FFFD.
inline std::string utf16ToUtf8(const uint8_t* d, size_t n, bool bigEndian) {
    std::string out;
    out.reserve(n / 2);
    size_t i = 0;
    auto unit = [&](size_t pos) -> uint32_t {
        return bigEndian ? ((static_cast<uint32_t>(d[pos]) << 8) | d[pos + 1])
                         : ((static_cast<uint32_t>(d[pos + 1]) << 8) | d[pos]);
    };
    while (i + 1 < n) {
        uint32_t u = unit(i);
        i += 2;
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < n) {
                uint32_t lo = unit(i);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    i += 2;
                    appendUtf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                    continue;
                }
            }
            appendUtf8(out, 0xFFFD);
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            appendUtf8(out, 0xFFFD);
        } else {
            appendUtf8(out, u);
        }
    }
    return out;
}

// UTF-8 -> UTF-16LE bytes. Returns false if the input is not valid UTF-8.
inline bool utf8ToUtf16le(const std::string& s, std::vector<uint8_t>& out) {
    out.clear();
    size_t i = 0, n = s.size();
    auto put = [&](uint32_t u) {
        out.push_back(static_cast<uint8_t>(u & 0xFF));
        out.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    };
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp; size_t extra;
        if (c < 0x80)            { cp = c; extra = 0; }
        else if ((c >> 5) == 0x6)  { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE)  { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
        else return false;
        if (extra > 0 && i + extra >= n) return false;
        for (size_t k = 1; k <= extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += extra + 1;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp >= 0x10000) {
            uint32_t v = cp - 0x10000;
            put(0xD800 + (v >> 10));
            put(0xDC00 + (v & 0x3FF));
        } else {
            put(cp);
        }
    }
    return true;
}

inline bool isAsciiOnly(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (static_cast<unsigned char>(s[i]) >= 0x80) return false;
    return true;
}

// Heuristic: does this buffer look like a text file (UTF-8/ASCII, or
// UTF-16 with a BOM)? Looks only at the first 4 KB.
inline bool looksLikeText(const uint8_t* d, size_t n) {
    if (n >= 2 && ((d[0] == 0xFF && d[1] == 0xFE) || (d[0] == 0xFE && d[1] == 0xFF))) return true;
    size_t lim = n < 4096 ? n : 4096;
    for (size_t i = 0; i < lim; ++i) {
        uint8_t c = d[i];
        if (c == 0) return false;
        if (c < 0x09 || (c > 0x0D && c < 0x20)) return false;
    }
    return true;
}

// Decodes a text file's bytes to UTF-8, honoring UTF-8/UTF-16 BOMs.
inline std::string decodeTextBytes(const uint8_t* d, size_t n) {
    if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF)
        return std::string(reinterpret_cast<const char*>(d + 3), n - 3);
    if (n >= 2 && d[0] == 0xFF && d[1] == 0xFE) return utf16ToUtf8(d + 2, n - 2, false);
    if (n >= 2 && d[0] == 0xFE && d[1] == 0xFF) return utf16ToUtf8(d + 2, n - 2, true);
    return std::string(reinterpret_cast<const char*>(d), n);
}

// First maxChars bytes of s, cut on a UTF-8 character boundary.
inline std::string cutUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut);
}

inline std::string humanSize(uint64_t bytes) {
    char buf[64];
    if (bytes < 1024) std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    else if (bytes < 1024ULL * 1024) std::snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ULL * 1024 * 1024) std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    else std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

} // namespace heng
