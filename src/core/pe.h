// pe.h -- Happy Enginiring
// Minimal, read-only PE (Portable Executable) parser. Pure byte parsing,
// no OS headers required, so it builds and runs identically on Windows and
// on any other platform (used here for testing). It only ever *reads* the
// bytes you hand it -- it never writes to the file, never maps it
// executable, and never runs any code from it.
//
// Scope: DOS header, COFF header, Optional header (PE32 / PE32+), section
// table, import table (DLL + function names), export table (function
// names). That's enough static context for an AI-assisted review without
// doing anything an ordinary "file properties" or `objdump`-style tool
// wouldn't also do.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace heng {

struct PeSection {
    std::string name;
    uint32_t virtualAddress = 0;
    uint32_t virtualSize = 0;
    uint32_t rawSize = 0;
    uint32_t rawPointer = 0;
    uint32_t characteristics = 0;
};

struct PeImport {
    std::string dll;
    std::vector<std::string> functions;
};

struct PeInfo {
    bool valid = false;
    std::string error;

    bool is64 = false;
    uint16_t machine = 0;
    std::string machineName;
    uint32_t timestamp = 0;
    uint16_t characteristics = 0;
    bool isDll = false;
    uint16_t subsystem = 0;
    std::string subsystemName;
    uint64_t entryPointRva = 0;
    uint64_t imageBase = 0;
    uint32_t numberOfSections = 0;
    std::string pdbPath; // from CodeView debug directory, if present

    std::vector<PeSection> sections;
    std::vector<PeImport> imports;
    std::vector<std::string> exports;
};

namespace detail {
    inline uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
    inline uint32_t rd32(const uint8_t* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24)); }
    inline uint64_t rd64(const uint8_t* p) {
        uint64_t lo = rd32(p), hi = rd32(p + 4);
        return lo | (hi << 32);
    }

    inline const char* machineName(uint16_t m) {
        switch (m) {
            case 0x014c: return "x86 (32-bit)";
            case 0x8664: return "x64 (64-bit)";
            case 0x01c0: return "ARM";
            case 0xaa64: return "ARM64";
            case 0x0200: return "Itanium";
            default: return "unknown";
        }
    }
    inline const char* subsystemName(uint16_t s) {
        switch (s) {
            case 1: return "Native";
            case 2: return "Windows GUI";
            case 3: return "Windows Console";
            case 5: return "OS/2 CUI";
            case 7: return "POSIX CUI";
            case 9: return "Windows CE GUI";
            case 10: return "EFI Application";
            default: return "unknown";
        }
    }

    // RVA -> file offset using the section table (0 if not mapped / in headers).
    inline uint32_t rvaToOffset(const std::vector<PeSection>& secs, uint32_t rva) {
        for (auto& s : secs) {
            if (rva >= s.virtualAddress && rva < s.virtualAddress + (s.rawSize ? s.rawSize : s.virtualSize)) {
                return s.rawPointer + (rva - s.virtualAddress);
            }
        }
        return 0;
    }
}

// Parses a PE file image already loaded into memory (as read from disk --
// NOT a live, mapped-for-execution image). Returns a PeInfo with
// valid=false and an error message if the buffer isn't a recognizable PE.
inline PeInfo parsePe(const uint8_t* data, size_t size) {
    using namespace detail;
    PeInfo info;
    if (size < 0x40 || data[0] != 'M' || data[1] != 'Z') {
        info.error = "not an MZ/PE file";
        return info;
    }
    uint32_t peOff = rd32(data + 0x3C);
    if (peOff + 24 > size || !(data[peOff] == 'P' && data[peOff+1] == 'E' && data[peOff+2] == 0 && data[peOff+3] == 0)) {
        info.error = "no PE signature found";
        return info;
    }
    const uint8_t* coff = data + peOff + 4;
    info.machine = rd16(coff + 0);
    info.machineName = machineName(info.machine);
    info.numberOfSections = rd16(coff + 2);
    info.timestamp = rd32(coff + 4);
    uint16_t sizeOfOptionalHeader = rd16(coff + 16);
    info.characteristics = rd16(coff + 18);
    info.isDll = (info.characteristics & 0x2000) != 0;

    const uint8_t* opt = coff + 20;
    if (peOff + 24 + sizeOfOptionalHeader > size || sizeOfOptionalHeader < 2) {
        info.error = "optional header truncated";
        return info;
    }
    uint16_t magic = rd16(opt);
    info.is64 = (magic == 0x20b);
    size_t numDirsOffset;
    if (info.is64) {
        info.entryPointRva = rd32(opt + 16);
        info.imageBase = rd64(opt + 24);
        info.subsystem = rd16(opt + 68);
        numDirsOffset = 108;
    } else {
        info.entryPointRva = rd32(opt + 16);
        info.imageBase = rd32(opt + 28);
        info.subsystem = rd16(opt + 68);
        numDirsOffset = 92;
    }
    info.subsystemName = subsystemName(info.subsystem);
    uint32_t numDirs = (numDirsOffset + 4 <= sizeOfOptionalHeader) ? rd32(opt + numDirsOffset) : 0;
    const uint8_t* dirs = opt + numDirsOffset + 4;

    auto dirEntry = [&](int idx, uint32_t& rva, uint32_t& sz) {
        rva = 0; sz = 0;
        if (static_cast<uint32_t>(idx) >= numDirs) return;
        const uint8_t* p = dirs + idx * 8;
        if (p + 8 > data + size) return;
        rva = rd32(p);
        sz = rd32(p + 4);
    };

    // Section table
    const uint8_t* secTable = opt + sizeOfOptionalHeader;
    for (uint32_t i = 0; i < info.numberOfSections; ++i) {
        const uint8_t* s = secTable + i * 40;
        if (s + 40 > data + size) break;
        PeSection sec;
        char name[9] = {0};
        std::memcpy(name, s, 8);
        sec.name = name;
        sec.virtualSize   = rd32(s + 8);
        sec.virtualAddress= rd32(s + 12);
        sec.rawSize       = rd32(s + 16);
        sec.rawPointer    = rd32(s + 20);
        sec.characteristics = rd32(s + 36);
        info.sections.push_back(sec);
    }

    // Import table (directory index 1)
    uint32_t impRva, impSz;
    dirEntry(1, impRva, impSz);
    if (impRva) {
        uint32_t off = rvaToOffset(info.sections, impRva);
        for (int guard = 0; off && off + 20 <= size && guard < 256; ++guard) {
            const uint8_t* d = data + off;
            uint32_t origFirstThunk = rd32(d + 0);
            uint32_t nameRva = rd32(d + 12);
            uint32_t firstThunk = rd32(d + 16);
            if (nameRva == 0 && firstThunk == 0 && origFirstThunk == 0) break;
            PeImport imp;
            uint32_t nameOff = rvaToOffset(info.sections, nameRva);
            if (nameOff && nameOff < size) {
                const char* np = reinterpret_cast<const char*>(data + nameOff);
                size_t maxLen = size - nameOff;
                size_t len = strnlen(np, maxLen);
                imp.dll.assign(np, len);
            }
            uint32_t thunkRva = origFirstThunk ? origFirstThunk : firstThunk;
            uint32_t thunkOff = rvaToOffset(info.sections, thunkRva);
            size_t entrySize = info.is64 ? 8 : 4;
            int fguard = 0;
            while (thunkOff && thunkOff + entrySize <= size && fguard < 2000) {
                uint64_t entry = info.is64 ? rd64(data + thunkOff) : rd32(data + thunkOff);
                if (entry == 0) break;
                uint64_t ordinalFlag = info.is64 ? (1ULL << 63) : (1ULL << 31);
                if (!(entry & ordinalFlag)) {
                    uint32_t hintNameRva = static_cast<uint32_t>(entry);
                    uint32_t hintNameOff = rvaToOffset(info.sections, hintNameRva);
                    if (hintNameOff && hintNameOff + 2 < size) {
                        const char* fn = reinterpret_cast<const char*>(data + hintNameOff + 2);
                        size_t maxLen = size - (hintNameOff + 2);
                        size_t len = strnlen(fn, maxLen);
                        imp.functions.emplace_back(fn, len);
                    }
                } else {
                    imp.functions.push_back("ordinal#" + std::to_string(entry & 0xFFFF));
                }
                thunkOff += static_cast<uint32_t>(entrySize);
                ++fguard;
            }
            if (!imp.dll.empty()) info.imports.push_back(imp);
            off += 20;
        }
    }

    // Export table (directory index 0)
    uint32_t expRva, expSz;
    dirEntry(0, expRva, expSz);
    if (expRva) {
        uint32_t off = rvaToOffset(info.sections, expRva);
        if (off && off + 40 <= size) {
            const uint8_t* d = data + off;
            uint32_t numNames = rd32(d + 24);
            uint32_t namesRva = rd32(d + 32);
            uint32_t namesOff = rvaToOffset(info.sections, namesRva);
            for (uint32_t i = 0; i < numNames && namesOff; ++i) {
                uint32_t entryOff = namesOff + i * 4;
                if (entryOff + 4 > size) break;
                uint32_t nameRva = rd32(data + entryOff);
                uint32_t nameOff = rvaToOffset(info.sections, nameRva);
                if (nameOff && nameOff < size) {
                    const char* np = reinterpret_cast<const char*>(data + nameOff);
                    size_t maxLen = size - nameOff;
                    size_t len = strnlen(np, maxLen);
                    info.exports.emplace_back(np, len);
                }
                if (info.exports.size() >= 1000) break;
            }
        }
    }

    info.valid = true;
    return info;
}

} // namespace heng
