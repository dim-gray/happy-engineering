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
#include <algorithm>
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
        if (!utf8ToUtf16le(utf8, out)) { err = "text is not valid UTF-8"; return false; }
        return true;
    }
    if (enc == "ascii" && !isAsciiOnly(utf8)) {
        err = "the text contains non-ASCII characters but encoding=ascii";
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

// ---- tolerant matching for text files -----------------------------------------
inline std::string toCrlf(const std::string& s) { std::string o; for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) o += '\r'; o += s[i]; } return o; }
inline std::string toLf(const std::string& s) { std::string o; for (size_t i = 0; i < s.size(); ++i) { if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue; o += s[i]; } return o; }
inline bool isWs(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
inline std::string trimWs(const std::string& s) { size_t a = 0, b = s.size(); while (a < b && isWs((uint8_t)s[a])) ++a; while (b > a && isWs((uint8_t)s[b - 1])) --b; return s.substr(a, b - a); }
inline bool hasCrlf(const std::vector<uint8_t>& d) { for (size_t i = 0; i + 1 < d.size() && i < 4000000; ++i) if (d[i] == '\r' && d[i + 1] == '\n') return true; return false; }

// Finds `needle` allowing any run of whitespace wherever the needle has whitespace. Returns (start,len) spans.
inline std::vector<std::pair<size_t, size_t> > findLoose(const std::vector<uint8_t>& data, const std::string& needle, size_t maxHits = 5000) {
    std::vector<std::string> tok; std::string cur;
    for (char c : needle) { if (isWs((uint8_t)c)) { if (!cur.empty()) tok.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) tok.push_back(cur);
    std::vector<std::pair<size_t, size_t> > out;
    if (tok.size() < 2) return out;
    std::vector<uint8_t> t0(tok[0].begin(), tok[0].end());
    size_t i = 0;
    while (i < data.size() && out.size() < maxHits) {
        std::vector<uint8_t>::const_iterator f0 = std::search(data.begin() + i, data.end(), t0.begin(), t0.end());
        if (f0 == data.end()) break;
        size_t start = (size_t)(f0 - data.begin()), pos = start + t0.size(); bool ok = true;
        for (size_t k = 1; k < tok.size() && ok; ++k) {
            size_t ws = pos; while (pos < data.size() && isWs(data[pos])) ++pos;
            if (pos == ws || pos + tok[k].size() > data.size() || std::memcmp(&data[pos], tok[k].data(), tok[k].size()) != 0) ok = false;
            else pos += tok[k].size();
        }
        if (ok) { out.push_back(std::make_pair(start, pos - start)); i = pos; } else i = start + 1;
    }
    return out;
}

// Why a find text was not found: points at the nearest line that does exist.
inline std::string explainMissing(const std::vector<uint8_t>& data, const std::string& find) {
    std::string best; size_t cur = 0;
    for (size_t i = 0; i <= find.size(); ++i) {
        if (i == find.size() || find[i] == '\n') { std::string line = trimWs(find.substr(cur, i - cur)); if (line.size() > best.size()) best = line; cur = i + 1; }
    }
    if (best.size() >= 6) {
        std::vector<uint8_t> b(best.begin(), best.end());
        std::vector<size_t> at = findAllBytes(data, b, 1, 1);
        if (!at.empty()) {
            size_t line = 1; for (size_t k = 0; k < at[0]; ++k) if (data[k] == '\n') ++line;
            return "text was not found in the file. The line \"" + (best.size() > 60 ? best.substr(0, 60) + "..." : best) + "\" does exist (line " + std::to_string(line) +
                   ") but the text around it is different - copy the exact text from the file (read_text) or use a shorter single-line snippet.";
        }
    }
    return "text was not found in the file - no line of it exists there. The file may have changed, or the text was guessed; read the file again and copy the exact text.";
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
    if (f.empty()) { r.message = "the text to find is empty"; return r; }

    std::vector<size_t> hits = findAllBytes(data, f, a.encoding == "utf16le" ? 2 : 1);
    std::vector<size_t> lens(hits.size(), f.size());
    std::string tolerant;      // note when a forgiving match was needed
    bool resizable = isTextExtension(fileName) || looksLikeText(data.data(), data.size());
    if (hits.empty() && resizable && a.encoding != "utf16le") {
        // 1) the AI usually writes "\n" while Windows files use "\r\n" (or the reverse)
        bool crlf = hasCrlf(data);
        std::string find2 = crlf ? toCrlf(a.find) : toLf(a.find), rep2 = crlf ? toCrlf(a.replace) : toLf(a.replace);
        if (find2 != a.find) {
            std::vector<uint8_t> f2(find2.begin(), find2.end());
            hits = findAllBytes(data, f2);
            if (!hits.empty()) { f.swap(f2); rep.assign(rep2.begin(), rep2.end()); lens.assign(hits.size(), f.size()); tolerant = " (matched after fixing the line endings)"; }
        }
        // 2) whitespace / indentation differences
        if (hits.empty()) {
            std::vector<std::pair<size_t, size_t> > sp = findLoose(data, a.find);
            if (!sp.empty()) {
                std::string r2 = trimWs(crlf ? toCrlf(a.replace) : toLf(a.replace));
                rep.assign(r2.begin(), r2.end());
                hits.clear(); lens.clear();
                for (size_t k = 0; k < sp.size(); ++k) { hits.push_back(sp[k].first); lens.push_back(sp[k].second); }
                tolerant = " (matched ignoring spaces/indentation differences)";
            }
        }
    }
    if (hits.empty()) { r.message = explainMissing(data, a.find); return r; }
    size_t total = hits.size();
    if (!a.all) { hits.resize(1); lens.resize(1); }

    std::string note;
    if (resizable) {
        std::vector<uint8_t> out;
        out.reserve(data.size() + hits.size() * (rep.size() > f.size() ? rep.size() - f.size() : 0));
        size_t cursor = 0;
        for (size_t h = 0; h < hits.size(); ++h) {
            out.insert(out.end(), data.begin() + cursor, data.begin() + hits[h]);
            out.insert(out.end(), rep.begin(), rep.end());
            cursor = hits[h] + lens[h];
        }
        out.insert(out.end(), data.begin() + cursor, data.end());
        data.swap(out);
    } else {
        if (rep.size() > f.size()) {
            r.message = "the replacement (" + std::to_string(rep.size()) + " bytes) is longer than the original (" +
                        std::to_string(f.size()) + "); the length cannot change in a binary file";
            return r;
        }
        if (rep.size() < f.size()) note = " (padded with 0x00 to the original length)";
        rep.resize(f.size(), 0);
        for (size_t h = 0; h < hits.size(); ++h)
            std::memcpy(&data[hits[h]], rep.data(), rep.size());
    }
    r.ok = true;
    r.count = hits.size();
    r.message = std::to_string(hits.size()) + (hits.size() == 1 ? " occurrence replaced" : " occurrences replaced");
    if (!a.all && total > 1) r.message = "first occurrence replaced (of " + std::to_string(total) + ")";
    r.message += note + tolerant;
    return r;
}

inline PatchOutcome applyPatchBytes(std::vector<uint8_t>& data, const PlanAction& a) {
    PatchOutcome r;
    if (a.write.empty()) { r.message = "there are no bytes to write"; return r; }
    if (a.offset > data.size() || a.write.size() > data.size() - static_cast<size_t>(a.offset)) {
        r.message = "patch range is outside the file (size " + std::to_string(data.size()) + " bytes)";
        return r;
    }
    size_t off = static_cast<size_t>(a.offset);
    if (!a.expect.empty()) {
        if (std::memcmp(&data[off], a.expect.data(), a.expect.size()) != 0) {
            std::vector<uint8_t> actual(data.begin() + off, data.begin() + off + a.expect.size());
            r.message = "bytes at that offset do not match (found: " + toHexBytes(actual) +
                        ", asteptat: " + toHexBytes(a.expect) + ")";
            return r;
        }
    }
    std::memcpy(&data[off], a.write.data(), a.write.size());
    r.ok = true;
    r.count = a.write.size();
    r.message = std::to_string(a.write.size()) + " bytes written";
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
        c.message = "attachment extension (." + e2 + ") differs from the target file's (." + e1 +
                    "); the game will not recognise it - convert it to the same format first";
        return c;
    }
    if (newBytes.empty()) { c.message = "the attachment is empty"; return c; }
    MediaInfo a = inspectMedia(oldBytes, fileName);
    MediaInfo b = inspectMedia(newBytes, attachName);
    std::string warn;
    if (a.known && !b.known) {
        warn = "the attachment does not look like a valid " + a.format + " file";
    } else if (a.known && b.known) {
        if (a.format != b.format) warn += "different format (" + a.format + " vs " + b.format + "). ";
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
