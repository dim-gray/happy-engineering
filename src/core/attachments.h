// attachments.h -- Happy Enginiring
// Files the user attaches (images, audio, text, anything) so the AI can
// reference them in an edit plan. This header classifies them and reads
// just enough of their headers (image size, audio format...) to describe
// them to the AI and to sanity-check a "replace this game file with that
// attachment" action. Pure byte parsing, no OS calls.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "textutil.h"

namespace heng {

enum class AttachmentKind { ImageVision, ImageOther, Audio, Text, Binary };

struct Attachment {
    std::string name;   // file name only (what the AI uses to refer to it)
    std::string path;   // full path on disk (UTF-8)
    uint64_t size = 0;
    AttachmentKind kind = AttachmentKind::Binary;
};

inline std::string baseNameOf(const std::string& path) {
    size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

// Lower-case extension without the dot ("" if none).
inline std::string extensionOf(const std::string& name) {
    std::string b = baseNameOf(name);
    size_t p = b.find_last_of('.');
    if (p == std::string::npos || p + 1 >= b.size()) return std::string();
    return lowerCopy(b.substr(p + 1));
}

inline bool inList(const std::string& ext, const char* const* list) {
    for (; *list; ++list) if (ext == *list) return true;
    return false;
}

inline AttachmentKind classifyByExtension(const std::string& fileName) {
    static const char* const vision[] = {"png", "jpg", "jpeg", "gif", "webp", nullptr};
    static const char* const otherImg[] = {"bmp", "tga", "dds", "ico", "tif", "tiff", "psd", nullptr};
    static const char* const audio[] = {"wav", "mp3", "ogg", "flac", "m4a", "aac", "wma", "opus", "mid", "midi", nullptr};
    static const char* const text[] = {"txt", "ini", "cfg", "conf", "json", "xml", "lua", "csv", "tsv",
        "yml", "yaml", "md", "toml", "properties", "lang", "loc", "html", "htm", "css", "js", "ts",
        "py", "c", "cpp", "h", "hpp", "cs", "java", "log", "bat", "cmd", "svg", "xaml", "resx",
        "rc", "po", "strings", "srt", "vtt", "ass", "tres", "tscn", "gd", "uproject", nullptr};
    std::string e = extensionOf(fileName);
    if (inList(e, vision)) return AttachmentKind::ImageVision;
    if (inList(e, otherImg)) return AttachmentKind::ImageOther;
    if (inList(e, audio)) return AttachmentKind::Audio;
    if (inList(e, text)) return AttachmentKind::Text;
    return AttachmentKind::Binary;
}

inline bool isTextExtension(const std::string& fileName) {
    return classifyByExtension(fileName) == AttachmentKind::Text;
}

inline const char* kindName(AttachmentKind k) {
    switch (k) {
        case AttachmentKind::ImageVision: return "imagine";
        case AttachmentKind::ImageOther:  return "imagine (format de joc)";
        case AttachmentKind::Audio:       return "audio";
        case AttachmentKind::Text:        return "text";
        default:                          return "binar";
    }
}

inline std::string imageMimeFor(const std::string& fileName) {
    std::string e = extensionOf(fileName);
    if (e == "png") return "image/png";
    if (e == "jpg" || e == "jpeg") return "image/jpeg";
    if (e == "gif") return "image/gif";
    if (e == "webp") return "image/webp";
    return std::string();
}

// ---- Media header inspection ---------------------------------------------
struct MediaInfo {
    bool known = false;
    std::string format;           // "PNG", "WAV PCM", "DDS DXT5", ...
    uint32_t width = 0, height = 0;
    uint32_t sampleRate = 0, channels = 0, bits = 0;
    double seconds = 0.0;
};

namespace media_detail {
    inline uint32_t le16(const uint8_t* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8)); }
    inline uint32_t le32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }
    inline uint32_t be16(const uint8_t* p) { return static_cast<uint32_t>((p[0] << 8) | p[1]); }
    inline uint32_t be32(const uint8_t* p) {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    }

    inline bool jpegSize(const uint8_t* d, size_t n, uint32_t& w, uint32_t& h) {
        if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return false;
        size_t i = 2;
        while (i + 9 < n) {
            if (d[i] != 0xFF) { ++i; continue; }
            uint8_t m = d[i + 1];
            if (m == 0xFF) { ++i; continue; }
            if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
            if (m == 0xD9 || m == 0xDA) return false;
            size_t len = be16(d + i + 2);
            if (len < 2) return false;
            bool sof = (m >= 0xC0 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC;
            if (sof) { h = be16(d + i + 5); w = be16(d + i + 7); return true; }
            i += 2 + len;
        }
        return false;
    }

    inline bool webpSize(const uint8_t* d, size_t n, uint32_t& w, uint32_t& h) {
        if (n < 30 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WEBP", 4) != 0) return false;
        if (std::memcmp(d + 12, "VP8X", 4) == 0) {
            w = 1 + (d[24] | (d[25] << 8) | (d[26] << 16));
            h = 1 + (d[27] | (d[28] << 8) | (d[29] << 16));
            return true;
        }
        if (std::memcmp(d + 12, "VP8L", 4) == 0) {
            if (d[20] != 0x2F) return false;
            uint32_t bits = le32(d + 21);
            w = 1 + (bits & 0x3FFF);
            h = 1 + ((bits >> 14) & 0x3FFF);
            return true;
        }
        if (std::memcmp(d + 12, "VP8 ", 4) == 0) {
            if (d[23] != 0x9D || d[24] != 0x01 || d[25] != 0x2A) return false;
            w = le16(d + 26) & 0x3FFF;
            h = le16(d + 28) & 0x3FFF;
            return true;
        }
        return false;
    }

    // totalSize: size of the whole file when `d` only holds its first bytes
    // (so the duration is right even for a header-only read).
    inline bool wavInfo(const uint8_t* d, size_t n, MediaInfo& mi, uint64_t totalSize = 0) {
        if (n < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) return false;
        size_t i = 12;
        bool fmt = false;
        uint32_t tag = 0, byteRate = 0, dataSize = 0;
        while (i + 8 <= n) {
            uint32_t sz = le32(d + i + 4);
            if (std::memcmp(d + i, "fmt ", 4) == 0 && i + 8 + 16 <= n) {
                tag = le16(d + i + 8);
                mi.channels = le16(d + i + 10);
                mi.sampleRate = le32(d + i + 12);
                byteRate = le32(d + i + 16);
                mi.bits = le16(d + i + 22);
                fmt = true;
            } else if (std::memcmp(d + i, "data", 4) == 0) {
                uint64_t whole = totalSize > n ? totalSize : n;
                uint64_t avail = whole - (i + 8);
                dataSize = sz > avail ? static_cast<uint32_t>(avail) : sz;
                break;
            }
            uint64_t adv = 8ULL + sz + (sz & 1);
            if (i + adv > n) break;
            i += static_cast<size_t>(adv);
        }
        if (!fmt) return false;
        if (tag == 1) mi.format = "WAV PCM";
        else if (tag == 3) mi.format = "WAV float";
        else if (tag == 0xFFFE) mi.format = "WAV extensible";
        else mi.format = "WAV (cod " + std::to_string(tag) + ")";
        if (byteRate > 0) mi.seconds = static_cast<double>(dataSize) / byteRate;
        return true;
    }
}

inline MediaInfo inspectMedia(const std::vector<uint8_t>& bytes, const std::string& fileName,
                              uint64_t totalSize = 0) {
    using namespace media_detail;
    MediaInfo mi;
    const uint8_t* d = bytes.data();
    size_t n = bytes.size();
    if (n >= 24 && std::memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0) {
        mi.known = true; mi.format = "PNG";
        mi.width = be32(d + 16); mi.height = be32(d + 20);
    } else if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8) {
        mi.format = "JPEG";
        mi.known = jpegSize(d, n, mi.width, mi.height);
    } else if (n >= 10 && (std::memcmp(d, "GIF87a", 6) == 0 || std::memcmp(d, "GIF89a", 6) == 0)) {
        mi.known = true; mi.format = "GIF";
        mi.width = le16(d + 6); mi.height = le16(d + 8);
    } else if (n >= 26 && d[0] == 'B' && d[1] == 'M') {
        mi.known = true; mi.format = "BMP";
        mi.width = le32(d + 18);
        int32_t hh = static_cast<int32_t>(le32(d + 22));
        mi.height = static_cast<uint32_t>(hh < 0 ? -hh : hh);
    } else if (n >= 88 && std::memcmp(d, "DDS ", 4) == 0) {
        mi.known = true; mi.format = "DDS";
        mi.height = le32(d + 12); mi.width = le32(d + 16);
        if (le32(d + 80) & 0x4) {                // DDPF_FOURCC
            char cc[5] = {static_cast<char>(d[84]), static_cast<char>(d[85]),
                          static_cast<char>(d[86]), static_cast<char>(d[87]), 0};
            mi.format += std::string(" ") + cc;
        }
    } else if (n >= 30 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0) {
        mi.format = "WebP";
        mi.known = webpSize(d, n, mi.width, mi.height);
    } else if (n >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WAVE", 4) == 0) {
        mi.known = wavInfo(d, n, mi, totalSize);
        if (!mi.known) mi.format = "WAV";
    } else if (n >= 4 && std::memcmp(d, "OggS", 4) == 0) {
        mi.known = true; mi.format = "OGG";
    } else if (n >= 4 && std::memcmp(d, "fLaC", 4) == 0) {
        mi.known = true; mi.format = "FLAC";
    } else if (n >= 4 && std::memcmp(d, "MThd", 4) == 0) {
        mi.known = true; mi.format = "MIDI";
    } else if (n >= 3 && (std::memcmp(d, "ID3", 3) == 0 || (d[0] == 0xFF && (d[1] & 0xE0) == 0xE0))) {
        mi.known = true; mi.format = "MP3";
    } else if (n >= 18 && extensionOf(fileName) == "tga") {
        uint8_t t = d[2];
        if (t == 1 || t == 2 || t == 3 || t == 9 || t == 10 || t == 11) {
            mi.known = true; mi.format = "TGA";
            mi.width = le16(d + 12); mi.height = le16(d + 14);
        }
    }
    return mi;
}

inline std::string describeMedia(const MediaInfo& mi) {
    if (!mi.known && mi.format.empty()) return std::string();
    std::string s = mi.format;
    char buf[96];
    if (mi.width && mi.height) {
        std::snprintf(buf, sizeof(buf), " %ux%u", mi.width, mi.height);
        s += buf;
    }
    if (mi.sampleRate) {
        std::snprintf(buf, sizeof(buf), ", %u Hz, %u-bit, %u canale", mi.sampleRate, mi.bits, mi.channels);
        s += buf;
        if (mi.seconds > 0) { std::snprintf(buf, sizeof(buf), ", %.1f s", mi.seconds); s += buf; }
    }
    return s;
}

// One-line description of an attachment for the AI's context.
inline std::string describeAttachment(const Attachment& a, const std::vector<uint8_t>& bytes) {
    std::string s = "- " + a.name + " - " + kindName(a.kind) + ", " + humanSize(a.size);
    std::string media = describeMedia(inspectMedia(bytes, a.name, a.size));
    if (!media.empty()) s += ", " + media;
    return s;
}

} // namespace heng
