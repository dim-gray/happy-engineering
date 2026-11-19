// patcher.h -- Happy Enginiring
// Pure byte-buffer editing used when a plan is applied: text replacement
// (UTF-8 / UTF-16LE / ASCII), verified byte patches, and sanity checks for
// "replace this game file with that attachment". It only ever works on
// in-memory copies of the files; the Windows layer decides what gets
// written, and always into the Output folder, never over the original.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include "actions.h"
#include "attachments.h"
#include "textutil.h"

namespace heng {

struct PatchOutcome {
    bool ok = false;
    size_t count = 0;        // occurrences / bytes changed
    std::string message;     // success note or error (Romanian, shown in the UI)
};

inline bool encodeForEncoding(const std::string& utf8, const std::string& enc,
                              std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    if (enc == "utf16le") {
        if (!utf8ToUtf16le(utf8, out)) { err = "textul nu este UTF-8 valid"; return false; }
        return true;
    }
    if (enc == "ascii" && !isAsciiOnly(utf8)) {
        err = "textul contine caractere non-ASCII, dar encoding=ascii";
        return false;
    }
    out.assign(utf8.begin(), utf8.end());
    return true;
}

// Non-overlapping occurrences of `needle`, optionally only at multiples of `align`.
inline std::vector<size_t> findAllBytes(const std::vector<uint8_t>& data,
                                        const std::vector<uint8_t>& needle,
                                        size_t align = 1, size_t maxHits = 100000) {
    std::vector<size_t> pos;
    if (needle.empty() || data.size() < needle.size()) return pos;
    size_t last = data.size() - needle.size();
    size_t i = 0;
    while (i <= last) {
        const void* p = std::memchr(data.data() + i, needle[0], last - i + 1);
        if (!p) break;
        size_t at = static_cast<size_t>(static_cast<const uint8_t*>(p) - data.data());
        if ((align <= 1 || at % align == 0) &&
            std::memcmp(data.data() + at, needle.data(), needle.size()) == 0) {
            pos.push_back(at);
            if (pos.size() >= maxHits) break;
            i = at + needle.size();
        } else {
            i = at + 1;
        }
    }
    return pos;
}

// Applies a replace_text action to `data` (modified in place).
// Text files may change length; in binary files the replacement may not be
// longer than the original (a shorter one is padded with 0x00), because a
// longer one would shift every offset after it and corrupt the file.
inline PatchOutcome applyReplaceText(std::vector<uint8_t>& data, const PlanAction& a,
                                     const std::string& fileName) {
    PatchOutcome r;
    std::vector<uint8_t> f, rep;
    std::string err;
    if (!encodeForEncoding(a.find, a.encoding, f, err)) { r.message = "'find': " + err; return r; }
    if (!encodeForEncoding(a.replace, a.encoding, rep, err)) { r.message = "'replace': " + err; return r; }
    if (f.empty()) { r.message = "textul de cautat este gol"; return r; }

    std::vector<size_t> hits = findAllBytes(data, f, a.encoding == "utf16le" ? 2 : 1);
    if (hits.empty()) { r.message = "textul nu a fost gasit in fisier"; return r; }
    size_t total = hits.size();
    if (!a.all) hits.resize(1);

    bool resizable = isTextExtension(fileName) || looksLikeText(data.data(), data.size());
    std::string note;
    if (resizable) {
        std::vector<uint8_t> out;
        out.reserve(data.size() + hits.size() * (rep.size() > f.size() ? rep.size() - f.size() : 0));
        size_t cursor = 0;
        for (size_t h = 0; h < hits.size(); ++h) {
            out.insert(out.end(), data.begin() + cursor, data.begin() + hits[h]);
            out.insert(out.end(), rep.begin(), rep.end());
            cursor = hits[h] + f.size();
        }
        out.insert(out.end(), data.begin() + cursor, data.end());
        data.swap(out);
    } else {
        if (rep.size() > f.size()) {
            r.message = "inlocuirea (" + std::to_string(rep.size()) + " octeti) este mai lunga decat originalul (" +
                        std::to_string(f.size()) + "); intr-un fisier binar lungimea nu se poate schimba";
            return r;
        }
        if (rep.size() < f.size()) note = " (completat cu 0x00 pana la lungimea originala)";
        rep.resize(f.size(), 0);
        for (size_t h = 0; h < hits.size(); ++h)
            std::memcpy(&data[hits[h]], rep.data(), rep.size());
    }
    r.ok = true;
    r.count = hits.size();
    r.message = std::to_string(hits.size()) + " aparitii inlocuite";
    if (!a.all && total > 1) r.message = "prima aparitie inlocuita (din " + std::to_string(total) + ")";
    r.message += note;
    return r;
}

inline PatchOutcome applyPatchBytes(std::vector<uint8_t>& data, const PlanAction& a) {
    PatchOutcome r;
    if (a.write.empty()) { r.message = "nu exista octeti de scris"; return r; }
    if (a.offset > data.size() || a.write.size() > data.size() - static_cast<size_t>(a.offset)) {
        r.message = "zona de patch este in afara fisierului (dimensiune " + std::to_string(data.size()) + " octeti)";
        return r;
    }
    size_t off = static_cast<size_t>(a.offset);
    if (!a.expect.empty()) {
        if (std::memcmp(&data[off], a.expect.data(), a.expect.size()) != 0) {
            std::vector<uint8_t> actual(data.begin() + off, data.begin() + off + a.expect.size());
            r.message = "octetii de la acel offset nu corespund (gasit: " + toHexBytes(actual) +
                        ", asteptat: " + toHexBytes(a.expect) + ")";
            return r;
        }
    }
    std::memcpy(&data[off], a.write.data(), a.write.size());
    r.ok = true;
    r.count = a.write.size();
    r.message = std::to_string(a.write.size()) + " octeti scrisi";
    return r;
}

struct ReplaceCheck {
    bool ok = false;           // false = must not be applied
    std::string message;       // error when !ok, otherwise optional warnings
};

// Sanity checks before swapping a game file for an attachment: same file
// extension (the game will not recognise a .jpg renamed over a .png), and a
// warning when image size / DDS format / audio parameters differ.
inline ReplaceCheck checkReplaceFile(const std::string& fileName, const std::vector<uint8_t>& oldBytes,
                                     const std::string& attachName, const std::vector<uint8_t>& newBytes) {
    ReplaceCheck c;
    std::string e1 = extensionOf(fileName), e2 = extensionOf(attachName);
    if (e1 != e2) {
        c.message = "extensia atasamentului (." + e2 + ") difera de cea a fisierului tinta (." + e1 +
                    "); jocul nu il va recunoaste - converteste-l mai intai in acelasi format";
        return c;
    }
    if (newBytes.empty()) { c.message = "atasamentul este gol"; return c; }
    MediaInfo a = inspectMedia(oldBytes, fileName);
    MediaInfo b = inspectMedia(newBytes, attachName);
    std::string warn;
    if (a.known && !b.known) {
        warn = "atasamentul nu pare un fisier " + a.format + " valid";
    } else if (a.known && b.known) {
        if (a.format != b.format) warn += "format diferit (" + a.format + " vs " + b.format + "). ";
        if (a.width && (a.width != b.width || a.height != b.height))
            warn += "dimensiune diferita (" + std::to_string(a.width) + "x" + std::to_string(a.height) + " vs " +
                    std::to_string(b.width) + "x" + std::to_string(b.height) + "). ";
        if (a.sampleRate && (a.sampleRate != b.sampleRate || a.channels != b.channels || a.bits != b.bits))
            warn += "parametri audio diferiti (" + describeMedia(a) + " vs " + describeMedia(b) + "). ";
    }
    c.ok = true;
    c.message = warn;
    return c;
}

} // namespace heng
