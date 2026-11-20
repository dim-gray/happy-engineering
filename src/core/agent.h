// agent.h -- Happy Engineering
// The AI "agent" protocol. Instead of one big blind prompt, the model can
// explore the target with READ-ONLY tools (```happy-tool blocks) that the app
// runs locally and feeds back, over several turns. Changes still only happen
// through a reviewed ```happy-plan``` block -- tools can never write.
#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include "json.h"
#include "actions.h"
#include "textutil.h"
#include "strings_extract.h"
#include "attachments.h"
#include "pe.h"

namespace heng {

struct HostEntry { std::string path; uint64_t size = 0; bool isDir = false; };

// What the agent tools need from the outside world. Paths are relative to the
// target folder; "game.zip!/textures/a.png" addresses a file inside an archive.
class IToolHost {
public:
    virtual ~IToolHost() {}
    // List entries under `dir` ("" = root). Archives are listed with "!/" paths.
    virtual bool list(const std::string& dir, bool recursive, size_t maxEntries,
                      std::vector<HostEntry>& out, std::string& err) = 0;
    // Read up to `len` bytes at `offset`; `total` receives the full size.
    virtual bool read(const std::string& path, uint64_t offset, uint64_t len,
                      std::vector<uint8_t>& out, uint64_t& total, std::string& err) = 0;
    virtual std::vector<std::string> attachmentNames() = 0;
};

struct ToolCall { std::string name; JsonValue args; std::string error; };

inline const char* defaultPersonality() {
    return
        "You are Happy Engineering, a friendly but precise reverse-engineering and game-modding assistant. "
        "You help the user modify programs and games they are entitled to modify: single-player and offline "
        "games, their own software, translations, re-texturing, balance tweaks, UI text. You study the target "
        "with your read-only tools, explain what you found, and propose concrete changes as a plan the user "
        "reviews before anything happens. Be concise, structured and honest: say plainly when something is an "
        "assumption. Never invent file paths, offsets or addresses - look them up with the tools first. "
        "You do not help with license-key generation, DRM or anti-cheat bypass, malware, or cheating in online "
        "or multiplayer games. Answer in the language the user writes in.";
}

inline const char* agentProtocolPrompt() {
    return R"HEPROTO(PROTOCOL (fixed, always active)

== 1. Exploring with tools ==
To look at the target, reply with one or more blocks like this (max 6 per reply) and NOTHING after them:
```happy-tool
{"tool":"list_files","path":"","recursive":true,"pattern":"*.ini","max":200}
```
The app runs them and answers with the results; then you continue. Tools (all read-only):
- list_files   {path, recursive, pattern, max}  list files/folders. Archives (.zip .jar .apk .pk3 .pk4 .pak-as-zip) open like folders: path "data.zip!/textures".
- read_text    {path, offset, max_chars}        read a text file (utf8/utf16 auto-detected).
- search_text  {query, path, ignore_case, max_hits}  search text across files under path (and inside archives).
- hex_dump     {path, offset, length}           hex+ASCII view, length <= 512.
- strings      {path, min_len, max}             printable strings of a binary.
- file_info    {path}                           size, image dimensions/format, PE header summary.
- list_attachments {}                           files the user attached for you to use.
Paths are relative to the target folder with "/" separators, no "..". Prefer a few precise calls over reading everything.

== 2. Proposing changes ==
When you know exactly what to change, explain briefly, then end the reply with ONE block:
```happy-plan
{"summary":"short description","actions":[ ... ]}
```
Action types ("note" = why, optional):
1. {"type":"replace_text","file":"rel/path","find":"exact existing text","replace":"new text","encoding":"utf8","all":true}
   encoding: utf8|utf16le|ascii. In binary files the length cannot grow (shorter text is padded with 0x00).
2. {"type":"replace_image","file":"rel/path.png","with_attachment":"mypic.jpg","fit":"cover"}
   Re-textures an image: the app converts the attachment to the SAME pixel size and file format as the original (fit: cover|contain|stretch). Prefer this over replace_file for images. Works for png/jpg/bmp/tga, also inside archives.
3. {"type":"replace_file","file":"rel/path","with_attachment":"name.ext"}   raw swap, same extension required (audio, models, anything).
4. {"type":"patch_bytes","file":"rel/path","offset":"0x1A2B","expect":"74 05","write":"EB 05"}   always include "expect".
5. {"type":"mem_write","label":"gold","value":"99999"}   ONLY labels from the user's memory watch list.
Rules: only files you have seen via tools or the file list; "find" must match existing text exactly - verify with read_text/search_text first. If data is missing, say what is missing instead of guessing. Max 40 actions, one happy-plan block, and never a happy-tool block in the same reply as a happy-plan.)HEPROTO";
}

inline std::string effectivePersonality(const std::string& custom) {
    std::string t = trimCopy(custom);
    return t.empty() ? std::string(defaultPersonality()) : t;
}
inline std::string agentSystemPrompt(const std::string& customPersonality) {
    return effectivePersonality(customPersonality) + "\n\n" + agentProtocolPrompt();
}

// ---- parsing tool blocks --------------------------------------------------
inline std::vector<ToolCall> extractToolCalls(const std::string& text) {
    std::vector<ToolCall> calls;
    const std::string marker = "```happy-tool";
    size_t pos = 0;
    while (calls.size() < 6) {
        size_t m = text.find(marker, pos);
        if (m == std::string::npos) break;
        size_t le = text.find('\n', m);
        if (le == std::string::npos) break;
        size_t close = text.find("```", le);
        if (close == std::string::npos) break;
        std::string body = text.substr(le + 1, close - le - 1);
        pos = close + 3;
        ToolCall c;
        bool ok = false; std::string err;
        JsonValue v = parseJson(body, &ok, &err);
        if (!ok || !v.isObject()) { c.error = "invalid JSON in tool block: " + err; calls.push_back(c); continue; }
        c.name = v.getString("tool");
        c.args = v;
        if (c.name.empty()) c.error = "missing \"tool\"";
        calls.push_back(c);
    }
    return calls;
}

inline std::string stripToolBlocks(const std::string& text) {
    std::string out = text;
    const std::string marker = "```happy-tool";
    for (;;) {
        size_t m = out.find(marker);
        if (m == std::string::npos) break;
        size_t le = out.find('\n', m);
        size_t close = le == std::string::npos ? std::string::npos : out.find("```", le);
        if (close == std::string::npos) { out = out.substr(0, m); break; }
        out = out.substr(0, m) + out.substr(close + 3);
    }
    return trimCopy(out);
}

// ---- running tools ----------------------------------------------------------
namespace agent_detail {
inline bool wildMatch(const char* p, const char* s) {   // * and ? , case-insensitive
    for (; *p; ++p) {
        if (*p == '*') { while (p[1] == '*') ++p; for (const char* t = s;; ++t) { if (wildMatch(p + 1, t)) return true; if (!*t) return false; } }
        if (!*s) return false;
        char a = *p, b = *s; if (a >= 'A' && a <= 'Z') a += 32; if (b >= 'A' && b <= 'Z') b += 32;
        if (*p != '?' && a != b) return false;
        ++s;
    }
    return *s == 0;
}
inline int64_t geti(const JsonValue& a, const char* k, int64_t def) {
    const JsonValue* v = a.find(k);
    if (!v) return def;
    if (v->type == JsonType::Number) return (int64_t)v->num;
    if (v->isString()) { uint64_t u; if (parseUInt64(v->str, u)) return (int64_t)u; }
    return def;
}
inline bool getb(const JsonValue& a, const char* k, bool def) {
    const JsonValue* v = a.find(k);
    return (v && v->type == JsonType::Bool) ? v->b : def;
}
inline std::string cleanPath(const JsonValue& a, bool allowEmpty, bool& ok) {
    std::string p = a.getString("path"), out;
    ok = true;
    if (p.empty() || p == "." || p == "/") { if (allowEmpty) return std::string(); ok = false; return std::string(); }
    if (!normalizeRelPath(p, out)) ok = false;
    return out;
}
inline std::string hexDump(const std::vector<uint8_t>& d, uint64_t base) {
    std::ostringstream o; char buf[16];
    for (size_t i = 0; i < d.size(); i += 16) {
        std::snprintf(buf, sizeof buf, "%08llX  ", (unsigned long long)(base + i)); o << buf;
        for (size_t j = 0; j < 16; ++j) { if (i + j < d.size()) { std::snprintf(buf, sizeof buf, "%02X ", d[i + j]); o << buf; } else o << "   "; }
        o << " ";
        for (size_t j = 0; j < 16 && i + j < d.size(); ++j) { uint8_t c = d[i + j]; o << (char)((c >= 32 && c < 127) ? c : '.'); }
        o << "\n";
    }
    return o.str();
}
} // namespace agent_detail

inline std::string runTool(const ToolCall& call, IToolHost& host, size_t maxOut = 14000) {
    using namespace agent_detail;
    if (!call.error.empty()) return "ERROR: " + call.error;
    const JsonValue& a = call.args;
    std::ostringstream o;
    std::string err;
    bool pathOk = true;

    if (call.name == "list_files") {
        std::string dir = cleanPath(a, true, pathOk);
        if (!pathOk) return "ERROR: bad path";
        size_t max = (size_t)std::min<int64_t>(std::max<int64_t>(geti(a, "max", 200), 1), 1000);
        std::string pat = a.getString("pattern");
        std::vector<HostEntry> es;
        if (!host.list(dir, getb(a, "recursive", false), 20000, es, err)) return "ERROR: " + err;
        size_t shown = 0, matched = 0;
        for (size_t i = 0; i < es.size(); ++i) {
            if (!pat.empty() && !es[i].isDir && !wildMatch(pat.c_str(), baseNameOf(es[i].path).c_str())) continue;
            if (!pat.empty() && es[i].isDir) continue;
            ++matched;
            if (shown < max) { o << (es[i].isDir ? "[dir]  " : "") << es[i].path; if (!es[i].isDir) o << "  (" << humanSize(es[i].size) << ")"; o << "\n"; ++shown; }
        }
        if (matched > shown) o << "... " << (matched - shown) << " more (raise \"max\" or narrow path/pattern)\n";
        if (matched == 0) o << "(nothing found)\n";
    } else if (call.name == "read_text") {
        std::string p = cleanPath(a, false, pathOk);
        if (!pathOk) return "ERROR: bad path";
        int64_t off = std::max<int64_t>(geti(a, "offset", 0), 0);
        int64_t maxc = std::min<int64_t>(std::max<int64_t>(geti(a, "max_chars", 6000), 100), 12000);
        std::vector<uint8_t> d; uint64_t total = 0;
        if (!host.read(p, (uint64_t)off, (uint64_t)maxc * 2 + 4, d, total, err)) return "ERROR: " + err;
        if (!looksLikeText(d.data(), std::min<size_t>(d.size(), 4096))) return "ERROR: " + p + " does not look like text (" + humanSize(total) + "); use hex_dump, strings or file_info";
        std::string t = decodeTextBytes(d.data(), d.size());
        bool cut = t.size() > (size_t)maxc; if (cut) t = cutUtf8(t, (size_t)maxc);
        o << "--- " << p << " (bytes " << off << ".. of " << total << ") ---\n" << t << (cut || off + (int64_t)d.size() < (int64_t)total ? "\n...[more available: use offset]" : "");
    } else if (call.name == "search_text") {
        std::string q = a.getString("query");
        if (q.empty()) return "ERROR: missing query";
        std::string dir = cleanPath(a, true, pathOk);
        if (!pathOk) return "ERROR: bad path";
        bool ic = getb(a, "ignore_case", true);
        size_t maxHits = (size_t)std::min<int64_t>(std::max<int64_t>(geti(a, "max_hits", 40), 1), 200);
        std::vector<HostEntry> es;
        if (!host.list(dir, true, 20000, es, err)) return "ERROR: " + err;
        std::string needle = ic ? lowerCopy(q) : q;
        size_t hits = 0, scanned = 0;
        for (size_t i = 0; i < es.size() && hits < maxHits; ++i) {
            if (es[i].isDir || es[i].size == 0 || es[i].size > 4u * 1024 * 1024) continue;
            std::vector<uint8_t> d; uint64_t total;
            if (!host.read(es[i].path, 0, 4u * 1024 * 1024, d, total, err)) continue;
            if (!looksLikeText(d.data(), std::min<size_t>(d.size(), 4096))) continue;
            ++scanned;
            std::string t = decodeTextBytes(d.data(), d.size());
            std::string hay = ic ? lowerCopy(t) : t;
            size_t pos = 0, line = 1, lastPos = 0;
            while (hits < maxHits && (pos = hay.find(needle, pos)) != std::string::npos) {
                for (size_t k = lastPos; k < pos; ++k) if (hay[k] == '\n') ++line;
                lastPos = pos;
                size_t ls = t.rfind('\n', pos); ls = ls == std::string::npos ? 0 : ls + 1;
                size_t le = t.find('\n', pos); if (le == std::string::npos) le = t.size();
                o << es[i].path << ":" << line << ": " << cutUtf8(trimCopy(t.substr(ls, le - ls)), 200) << "\n";
                ++hits; pos = le;
            }
        }
        o << (hits ? "" : "(no matches) ") << "[scanned " << scanned << " text files]\n";
    } else if (call.name == "hex_dump") {
        std::string p = cleanPath(a, false, pathOk);
        if (!pathOk) return "ERROR: bad path";
        int64_t off = std::max<int64_t>(geti(a, "offset", 0), 0);
        int64_t len = std::min<int64_t>(std::max<int64_t>(geti(a, "length", 256), 1), 512);
        std::vector<uint8_t> d; uint64_t total;
        if (!host.read(p, (uint64_t)off, (uint64_t)len, d, total, err)) return "ERROR: " + err;
        o << "--- " << p << " (size " << total << ") ---\n" << hexDump(d, (uint64_t)off);
    } else if (call.name == "strings") {
        std::string p = cleanPath(a, false, pathOk);
        if (!pathOk) return "ERROR: bad path";
        size_t minLen = (size_t)std::min<int64_t>(std::max<int64_t>(geti(a, "min_len", 6), 3), 40);
        size_t max = (size_t)std::min<int64_t>(std::max<int64_t>(geti(a, "max", 150), 1), 400);
        std::vector<uint8_t> d; uint64_t total;
        if (!host.read(p, 0, 16u * 1024 * 1024, d, total, err)) return "ERROR: " + err;
        std::vector<std::string> s = extractAsciiStrings(d.data(), d.size(), minLen, max);
        std::vector<std::string> w = extractUtf16Strings(d.data(), d.size(), minLen, max);
        o << "--- " << p << ": " << s.size() << " ascii, " << w.size() << " utf16 strings (first " << max << " each) ---\n";
        for (size_t i = 0; i < s.size(); ++i) o << s[i] << "\n";
        if (!w.empty()) { o << "--- UTF-16 ---\n"; for (size_t i = 0; i < w.size(); ++i) o << w[i] << "\n"; }
    } else if (call.name == "file_info") {
        std::string p = cleanPath(a, false, pathOk);
        if (!pathOk) return "ERROR: bad path";
        std::vector<uint8_t> d; uint64_t total;
        if (!host.read(p, 0, 8u * 1024 * 1024, d, total, err)) return "ERROR: " + err;
        o << p << "\nsize: " << total << " bytes (" << humanSize(total) << ")\n";
        MediaInfo mi = inspectMedia(d, p, total);
        if (mi.known) o << describeMedia(mi) << "\n";
        PeInfo pe = parsePe(d.data(), d.size());
        if (pe.valid) {
            o << "PE: " << (pe.is64 ? "x64" : "x86") << ", " << pe.sections.size() << " sections, " << pe.imports.size() << " imported DLLs\n";
        } else if (!mi.known) {
            o << "looks like " << (looksLikeText(d.data(), std::min<size_t>(d.size(), 4096)) ? "text" : "binary data") << "\n";
        }
    } else if (call.name == "list_attachments") {
        std::vector<std::string> n = host.attachmentNames();
        if (n.empty()) o << "(no attachments)\n";
        for (size_t i = 0; i < n.size(); ++i) o << n[i] << "\n";
    } else {
        return "ERROR: unknown tool '" + call.name + "'";
    }
    std::string r = o.str();
    if (r.size() > maxOut) r = cutUtf8(r, maxOut) + "\n...[output truncated]";
    return r;
}

} // namespace heng
