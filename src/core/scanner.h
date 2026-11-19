// scanner.h -- Happy Enginiring
// The engine behind the "Memorie" window: a Cheat-Engine-style value scanner
// (first scan for an exact value, then narrow with "changed / unchanged /
// increased / decreased / exact") plus value encoding and a named watch list.
//
// It works on an abstract IMemory, so this header is portable and unit-tested
// with a fake memory; the Windows build plugs in ReadProcessMemory /
// WriteProcessMemory (src/win/memory_win.h). Scope is deliberately limited to
// reading and writing VALUES at addresses the user picked -- no DLL
// injection, no hooking, no code execution inside the target.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "actions.h"
#include "textutil.h"

namespace heng {

enum class ValueType { Int8, Int16, Int32, Int64, Float, Double };
enum class ScanMode { Exact, Changed, Unchanged, Increased, Decreased };

inline size_t valueSize(ValueType t) {
    switch (t) {
        case ValueType::Int8: return 1;
        case ValueType::Int16: return 2;
        case ValueType::Int32: return 4;
        case ValueType::Int64: return 8;
        case ValueType::Float: return 4;
        case ValueType::Double: return 8;
    }
    return 4;
}

inline const char* valueTypeName(ValueType t) {
    switch (t) {
        case ValueType::Int8: return "int8";
        case ValueType::Int16: return "int16";
        case ValueType::Int32: return "int32";
        case ValueType::Int64: return "int64";
        case ValueType::Float: return "float";
        case ValueType::Double: return "double";
    }
    return "int32";
}

inline bool valueTypeFromName(const std::string& name, ValueType& out) {
    std::string n = lowerCopy(trimCopy(name));
    if (n == "int8" || n == "byte") out = ValueType::Int8;
    else if (n == "int16" || n == "short") out = ValueType::Int16;
    else if (n == "int32" || n == "int" || n == "dword") out = ValueType::Int32;
    else if (n == "int64" || n == "long" || n == "qword") out = ValueType::Int64;
    else if (n == "float" || n == "single") out = ValueType::Float;
    else if (n == "double") out = ValueType::Double;
    else return false;
    return true;
}

namespace scan_detail {
    inline void putLe(std::vector<uint8_t>& out, uint64_t v, size_t n) {
        for (size_t i = 0; i < n; ++i) out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    inline uint64_t getLe(const uint8_t* p, size_t n) {
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
        return v;
    }
    inline int64_t signExtend(uint64_t v, size_t bytes) {
        if (bytes >= 8) return static_cast<int64_t>(v);
        uint64_t sign = 1ULL << (bytes * 8 - 1);
        return static_cast<int64_t>((v ^ sign) - sign);
    }
    inline double decodeFloat(const uint8_t* p, size_t bytes) {
        uint64_t raw = getLe(p, bytes);
        if (bytes == 4) { uint32_t r = static_cast<uint32_t>(raw); float f; std::memcpy(&f, &r, 4); return f; }
        double d; std::memcpy(&d, &raw, 8); return d;
    }
}

// Text -> little-endian bytes for the given type. Integers accept decimal or
// 0x-hex and the full signed-or-unsigned range of the type; floats accept
// '.' or ',' as the decimal separator.
inline bool parseValue(ValueType t, const std::string& text, std::vector<uint8_t>& out, std::string& err) {
    using namespace scan_detail;
    out.clear();
    std::string s = trimCopy(text);
    if (s.empty()) { err = "valoarea este goala"; return false; }
    size_t n = valueSize(t);
    if (t == ValueType::Float || t == ValueType::Double) {
        for (size_t i = 0; i < s.size(); ++i) if (s[i] == ',') s[i] = '.';
        char* end = nullptr;
        double d = std::strtod(s.c_str(), &end);
        if (end == s.c_str() || *end != '\0' || !std::isfinite(d)) { err = "numar zecimal invalid: " + text; return false; }
        if (t == ValueType::Float) {
            float f = static_cast<float>(d);
            uint32_t raw; std::memcpy(&raw, &f, 4);
            putLe(out, raw, 4);
        } else {
            uint64_t raw; std::memcpy(&raw, &d, 8);
            putLe(out, raw, 8);
        }
        return true;
    }
    bool neg = false;
    if (s[0] == '-') { neg = true; s = s.substr(1); }
    else if (s[0] == '+') { s = s.substr(1); }
    uint64_t mag;
    if (!parseUInt64(s, mag)) { err = "numar intreg invalid: " + text; return false; }
    uint64_t limitNeg = 1ULL << (n * 8 - 1);                       // |min|
    uint64_t limitPos = (n == 8) ? UINT64_MAX : ((1ULL << (n * 8)) - 1);
    if ((neg && mag > limitNeg) || (!neg && mag > limitPos)) { err = "valoarea nu incape in " + std::string(valueTypeName(t)); return false; }
    uint64_t raw = neg ? (~mag + 1) : mag;
    putLe(out, raw, n);
    return true;
}

inline std::string formatValue(ValueType t, const uint8_t* p) {
    using namespace scan_detail;
    char buf[64];
    size_t n = valueSize(t);
    if (t == ValueType::Float) std::snprintf(buf, sizeof(buf), "%.7g", decodeFloat(p, 4));
    else if (t == ValueType::Double) std::snprintf(buf, sizeof(buf), "%.15g", decodeFloat(p, 8));
    else std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(signExtend(getLe(p, n), n)));
    return buf;
}

// -1 / 0 / +1 comparing two encoded values of the same type.
inline int compareValues(ValueType t, const uint8_t* a, const uint8_t* b) {
    using namespace scan_detail;
    size_t n = valueSize(t);
    if (t == ValueType::Float || t == ValueType::Double) {
        double x = decodeFloat(a, n), y = decodeFloat(b, n);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    int64_t x = signExtend(getLe(a, n), n), y = signExtend(getLe(b, n), n);
    return x < y ? -1 : (x > y ? 1 : 0);
}

// ---- memory abstraction -----------------------------------------------------
struct MemRegion { uint64_t base = 0; uint64_t size = 0; };

class IMemory {
public:
    virtual ~IMemory() {}
    virtual std::vector<MemRegion> regions() = 0;                       // readable+writable committed regions
    virtual bool read(uint64_t addr, void* buf, size_t len) = 0;        // false if not fully readable
    virtual bool write(uint64_t addr, const void* buf, size_t len) = 0; // false if the write failed
};

inline bool readValue(IMemory& m, uint64_t addr, ValueType t, std::vector<uint8_t>& out) {
    out.assign(valueSize(t), 0);
    return m.read(addr, &out[0], out.size());
}

// Parses `text` for `t` and writes it at `addr`.
inline bool writeValue(IMemory& m, uint64_t addr, ValueType t, const std::string& text, std::string& err) {
    std::vector<uint8_t> bytes;
    if (!parseValue(t, text, bytes, err)) return false;
    if (!m.write(addr, bytes.data(), bytes.size())) { err = "scrierea in memorie a esuat (adresa invalida sau protejata)"; return false; }
    return true;
}

struct ScanHit {
    uint64_t addr = 0;
    uint8_t prev[8];
    uint8_t cur[8];
    ScanHit() { std::memset(prev, 0, 8); std::memset(cur, 0, 8); }
};

class Scanner {
public:
    explicit Scanner(IMemory& m) : mem_(m), type_(ValueType::Int32), active_(false), truncated_(false) {}

    void reset() { hits_.clear(); active_ = false; truncated_ = false; }
    bool active() const { return active_; }
    bool truncated() const { return truncated_; }
    ValueType type() const { return type_; }
    const std::vector<ScanHit>& hits() const { return hits_; }

    // First scan: every aligned address whose bytes equal `needle`.
    // alignment 0 = natural alignment of the type (1 for int8, 4 for int32...).
    size_t firstScan(ValueType t, const std::vector<uint8_t>& needle, size_t alignment = 0,
                     size_t maxHits = 200000) {
        reset();
        type_ = t;
        const size_t vs = valueSize(t);
        if (needle.size() != vs) return 0;
        const size_t align = alignment ? alignment : vs;
        const size_t CHUNK = 1u << 20;
        std::vector<uint8_t> buf(CHUNK + vs);
        std::vector<MemRegion> regs = mem_.regions();
        for (size_t r = 0; r < regs.size() && !truncated_; ++r) {
            const MemRegion& reg = regs[r];
            for (uint64_t off = 0; off < reg.size && !truncated_; off += CHUNK) {
                uint64_t left = reg.size - off;
                size_t starts = static_cast<size_t>(left < CHUNK ? left : CHUNK);   // start positions in this chunk
                size_t want = static_cast<size_t>(left < CHUNK + vs - 1 ? left : CHUNK + vs - 1);
                if (!mem_.read(reg.base + off, &buf[0], want)) continue;           // unreadable chunk: skip
                for (size_t p = 0; p < starts && p + vs <= want; p += align) {
                    if (buf[p] != needle[0]) continue;
                    if (std::memcmp(&buf[p], needle.data(), vs) != 0) continue;
                    ScanHit h;
                    h.addr = reg.base + off + p;
                    std::memcpy(h.prev, &buf[p], vs);
                    std::memcpy(h.cur, &buf[p], vs);
                    hits_.push_back(h);
                    if (hits_.size() >= maxHits) { truncated_ = true; break; }
                }
            }
        }
        active_ = true;
        return hits_.size();
    }

    // Narrows the current hits by re-reading them. `needle` is required for Exact.
    size_t nextScan(ScanMode mode, const std::vector<uint8_t>* needle = nullptr) {
        if (!active_) return 0;
        const size_t vs = valueSize(type_);
        std::vector<ScanHit> kept;
        kept.reserve(hits_.size());
        for (size_t i = 0; i < hits_.size(); ++i) {
            ScanHit h = hits_[i];
            uint8_t now[8];
            if (!mem_.read(h.addr, now, vs)) continue;                              // memory went away
            bool keep = false;
            switch (mode) {
                case ScanMode::Exact:     keep = needle && needle->size() == vs && std::memcmp(now, needle->data(), vs) == 0; break;
                case ScanMode::Changed:   keep = std::memcmp(now, h.cur, vs) != 0; break;
                case ScanMode::Unchanged: keep = std::memcmp(now, h.cur, vs) == 0; break;
                case ScanMode::Increased: keep = compareValues(type_, now, h.cur) > 0; break;
                case ScanMode::Decreased: keep = compareValues(type_, now, h.cur) < 0; break;
            }
            if (!keep) continue;
            std::memcpy(h.prev, h.cur, 8);
            std::memcpy(h.cur, now, vs);
            kept.push_back(h);
        }
        hits_.swap(kept);
        return hits_.size();
    }

private:
    IMemory& mem_;
    ValueType type_;
    bool active_;
    bool truncated_;
    std::vector<ScanHit> hits_;
};

// ---- the user's named watch list ------------------------------------------
struct WatchEntry {
    std::string label;                  // unique, chosen by the user ("gold", "hp"...)
    uint64_t address = 0;
    ValueType type = ValueType::Int32;
    bool frozen = false;                // keep re-writing freezeBytes
    std::vector<uint8_t> freezeBytes;
    std::string lastText;               // last value shown (also given to the AI as context)
};

inline const WatchEntry* findWatch(const std::vector<WatchEntry>& list, const std::string& label) {
    std::string want = lowerCopy(trimCopy(label));
    for (size_t i = 0; i < list.size(); ++i)
        if (lowerCopy(trimCopy(list[i].label)) == want) return &list[i];
    return nullptr;
}

} // namespace heng
