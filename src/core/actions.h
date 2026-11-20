// actions.h -- Happy Enginiring
// The "edit plan" protocol between the AI and the app. The AI answers in
// prose and, when it wants to change something, appends ONE fenced block:
//
//   ```happy-plan
//   {"summary":"...","actions":[ {"type":"replace_text", ...}, ... ],
//    "need_files":["data/lang/en.ini"]}
//   ```
//
// This header parses that block into typed, validated actions. Nothing here
// touches the disk or a process: the plan is only DATA the user reviews
// (and ticks) in the Plan window before anything is applied. Every action
// is checked for safe relative paths, and memory writes may only name a
// label from the user's own watch list -- never a raw address from the AI.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include "json.h"
#include "textutil.h"

namespace heng {

enum class ActionType { ReplaceText, ReplaceFile, ReplaceImage, PatchBytes, MemWrite, Unknown };

struct PlanAction {
    ActionType type = ActionType::Unknown;
    std::string typeName;      // raw "type" string from the AI
    std::string file;          // relative path inside the target folder ('/' separators)
    std::string find, replace; // replace_text
    std::string encoding = "utf8"; // utf8 | utf16le | ascii
    bool all = true;           // replace every occurrence (false = first only)
    std::string attachment;    // replace_file / replace_image: attachment file name
    std::string fit = "cover";  // replace_image: cover | contain | stretch
    uint64_t offset = 0;       // patch_bytes
    std::vector<uint8_t> expect, write; // patch_bytes
    std::string label;         // mem_write: watch-list label
    std::string value;         // mem_write: new value as text
    std::string note;          // the AI's explanation of why
    std::string error;         // non-empty => invalid, cannot be applied
};

struct Plan {
    bool found = false;                 // a happy-plan block was present
    std::string summary;
    std::vector<PlanAction> actions;
    std::vector<std::string> needFiles; // files the AI asks to see (relative paths)
    std::string error;                  // block present but unusable
};

// ---- small parsers ----------------------------------------------------------
inline std::string trimCopy(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// "123", "0x1A2B" -> value. Rejects empty text, trailing junk and overflow.
inline bool parseUInt64(const std::string& text, uint64_t& out) {
    std::string s = trimCopy(text);
    if (s.empty()) return false;
    int base = 10;
    size_t i = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; i = 2; }
    uint64_t v = 0;
    for (; i < s.size(); ++i) {
        char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (v > (UINT64_MAX - static_cast<uint64_t>(d)) / static_cast<uint64_t>(base)) return false;
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
    }
    out = v;
    return true;
}

// "74 05", "0x74,0x05" or "7405" -> bytes. False on bad/odd digits.
inline bool parseHexBytes(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    std::string tok;
    std::vector<std::string> toks;
    for (size_t i = 0; i <= text.size(); ++i) {
        char c = i < text.size() ? text[i] : ' ';
        if (c == ' ' || c == ',' || c == ';' || c == '\t' || c == '\r' || c == '\n') {
            if (!tok.empty()) { toks.push_back(tok); tok.clear(); }
        } else {
            tok.push_back(c);
        }
    }
    for (size_t t = 0; t < toks.size(); ++t) {
        std::string s = toks[t];
        if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
        if (s.empty() || (s.size() % 2) != 0) return false;
        for (size_t i = 0; i < s.size(); i += 2) {
            int v = 0;
            for (int k = 0; k < 2; ++k) {
                char c = s[i + k];
                int d;
                if (c >= '0' && c <= '9') d = c - '0';
                else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return false;
                v = (v << 4) | d;
            }
            out.push_back(static_cast<uint8_t>(v));
        }
    }
    return !out.empty();
}

inline std::string toHexBytes(const std::vector<uint8_t>& b, size_t maxBytes = 16) {
    static const char* hex = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < b.size() && i < maxBytes; ++i) {
        if (i) s.push_back(' ');
        s.push_back(hex[b[i] >> 4]);
        s.push_back(hex[b[i] & 0xF]);
    }
    if (b.size() > maxBytes) s += " ...";
    return s;
}

// True for the DOS device names Windows treats specially in any folder
// (CON, PRN, AUX, NUL, COM1-9, LPT1-9), with or without an extension:
// opening "NUL" or "CON.txt" does not open a file, it opens a device.
inline bool isReservedWindowsName(const std::string& segment) {
    std::string stem = segment.substr(0, segment.find('.'));
    while (!stem.empty() && stem[stem.size() - 1] == ' ') stem.erase(stem.size() - 1);
    for (size_t i = 0; i < stem.size(); ++i)
        if (stem[i] >= 'a' && stem[i] <= 'z') stem[i] = static_cast<char>(stem[i] - 'a' + 'A');
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return true;
    if (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
        stem[3] >= '1' && stem[3] <= '9') return true;
    return false;
}

// Accepts only a relative path that stays inside the target folder:
// no drive letters, no leading slash, no "..", no ':' (NTFS streams), none
// of the characters Windows forbids in file names, no device names (NUL, CON,
// COM1...) and no segment ending in '.' or ' ' (Windows silently drops those,
// so they would name a different file than the one the user reviewed).
// Output uses '/' separators.
inline bool normalizeRelPath(const std::string& in, std::string& out) {
    out.clear();
    if (in.empty() || in.size() > 400) return false;
    std::string s = in;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\') s[i] = '/';
        else if (c == '\0' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*') return false;
    }
    if (s[0] == '/') return false;
    std::string result;
    size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find('/', i);
        if (j == std::string::npos) j = s.size();
        std::string seg = s.substr(i, j - i);
        if (seg == "..") return false;
        if (!seg.empty() && seg != ".") {
            char last = seg[seg.size() - 1];
            if (last == '.' || last == ' ') return false;
            if (isReservedWindowsName(seg)) return false;
            if (!result.empty()) result.push_back('/');
            result += seg;
        }
        i = j + 1;
    }
    if (result.empty()) return false;
    out = result;
    return true;
}

inline bool jsonScalarToString(const JsonValue* v, std::string& out) {
    if (!v) return false;
    if (v->type == JsonType::String) { out = v->str; return true; }
    if (v->type == JsonType::Number) { out = v->dump(); return true; }
    if (v->type == JsonType::Bool) { out = v->b ? "true" : "false"; return true; }
    return false;
}

// ---- plan parsing -------------------------------------------------------
inline PlanAction parseAction(const JsonValue& o) {
    PlanAction a;
    a.typeName = o.getString("type");
    a.note = o.getString("note");
    std::string t = lowerCopy(a.typeName);

    auto needFile = [&]() -> bool {
        if (!normalizeRelPath(o.getString("file"), a.file)) {
            a.error = "missing or disallowed file path (only relative paths inside the target folder)";
            return false;
        }
        return true;
    };

    if (t == "replace_text") {
        a.type = ActionType::ReplaceText;
        if (!needFile()) return a;
        a.find = o.getString("find");
        if (a.find.empty()) { a.error = "missing search text ('find')"; return a; }
        if (!o.find("replace")) { a.error = "missing 'replace'"; return a; }
        a.replace = o.getString("replace");
        std::string enc = lowerCopy(o.getString("encoding", "utf8"));
        if (enc != "utf8" && enc != "utf16le" && enc != "ascii") { a.error = "unknown encoding: " + enc; return a; }
        a.encoding = enc;
        const JsonValue* all = o.find("all");
        if (all && all->type == JsonType::Bool) a.all = all->b;
    } else if (t == "replace_file") {
        a.type = ActionType::ReplaceFile;
        if (!needFile()) return a;
        a.attachment = o.getString("with_attachment");
        if (a.attachment.empty()) a.attachment = o.getString("attachment");
        if (a.attachment.empty()) { a.error = "missing 'with_attachment'"; return a; }
        if (a.attachment.find('/') != std::string::npos || a.attachment.find('\\') != std::string::npos) {
            a.error = "attachment name must not contain a path";
            return a;
        }
    } else if (t == "replace_image") {
        a.type = ActionType::ReplaceImage;
        if (!needFile()) return a;
        a.attachment = o.getString("with_attachment");
        if (a.attachment.empty()) a.attachment = o.getString("attachment");
        if (a.attachment.empty()) { a.error = "missing 'with_attachment'"; return a; }
        if (a.attachment.find('/') != std::string::npos || a.attachment.find('\\') != std::string::npos) {
            a.error = "attachment name must not contain a path";
            return a;
        }
        a.fit = lowerCopy(o.getString("fit", "cover"));
        if (a.fit != "cover" && a.fit != "contain" && a.fit != "stretch") { a.error = "unknown fit: " + a.fit; return a; }
    } else if (t == "patch_bytes") {
        a.type = ActionType::PatchBytes;
        if (!needFile()) return a;
        std::string off;
        if (!jsonScalarToString(o.find("offset"), off) || !parseUInt64(off, a.offset)) {
            a.error = "missing or invalid offset";
            return a;
        }
        if (!parseHexBytes(o.getString("write"), a.write)) { a.error = "'write' is missing or not valid hex"; return a; }
        std::string exp = o.getString("expect");
        if (!exp.empty()) {
            if (!parseHexBytes(exp, a.expect)) { a.error = "'expect' is not valid hex"; return a; }
            if (a.expect.size() != a.write.size()) { a.error = "'expect' and 'write' must have the same length"; return a; }
        }
    } else if (t == "mem_write") {
        a.type = ActionType::MemWrite;
        a.label = o.getString("label");
        if (a.label.empty()) { a.error = "missing 'label' (a label from the memory watch list)"; return a; }
        if (!jsonScalarToString(o.find("value"), a.value) || a.value.empty()) { a.error = "missing 'value'"; return a; }
    } else {
        a.type = ActionType::Unknown;
        a.error = "unknown action type: " + a.typeName;
    }
    return a;
}

inline Plan extractPlan(const std::string& text) {
    Plan p;
    const std::string marker = "```happy-plan";
    size_t m = text.rfind(marker);
    if (m == std::string::npos) return p;
    p.found = true;
    size_t lineEnd = text.find('\n', m);
    if (lineEnd == std::string::npos) { p.error = "the happy-plan block is empty"; return p; }
    size_t close = text.find("```", lineEnd);
    if (close == std::string::npos) {
        p.error = "the happy-plan block is not closed (the reply was probably cut off by the token limit)";
        return p;
    }
    std::string body = text.substr(lineEnd + 1, close - lineEnd - 1);
    bool ok = false;
    std::string err;
    JsonValue root = parseJson(body, &ok, &err);
    if (!ok || !root.isObject()) { p.error = "invalid JSON in the AI plan: " + err; return p; }

    p.summary = root.getString("summary");
    const JsonValue* need = root.find("need_files");
    if (need && need->isArray()) {
        for (size_t i = 0; i < need->arr->size(); ++i) {
            const JsonValue& f = (*need->arr)[i];
            std::string rel;
            if (f.isString() && normalizeRelPath(f.str, rel)) p.needFiles.push_back(rel);
        }
    }
    const JsonValue* acts = root.find("actions");
    if (acts && acts->isArray()) {
        for (size_t i = 0; i < acts->arr->size(); ++i) {
            const JsonValue& item = (*acts->arr)[i];
            if (item.isObject()) p.actions.push_back(parseAction(item));
        }
    } else if (p.needFiles.empty()) {
        p.error = "the plan has no 'actions' list";
    }
    return p;
}

// The AI's reply without the machine-readable plan block (for display).
inline std::string stripPlanBlock(const std::string& text) {
    const std::string marker = "```happy-plan";
    size_t m = text.rfind(marker);
    if (m == std::string::npos) return text;
    size_t lineEnd = text.find('\n', m);
    size_t close = lineEnd == std::string::npos ? std::string::npos : text.find("```", lineEnd);
    std::string before = text.substr(0, m);
    std::string after = close == std::string::npos ? std::string() : text.substr(close + 3);
    return trimCopy(before + after);
}

// ---- human-readable descriptions (shown in the Plan window) ---------------
inline std::string shortText(const std::string& s, size_t maxBytes = 40) {
    std::string t = cutUtf8(s, maxBytes);
    for (size_t i = 0; i < t.size(); ++i) if (t[i] == '\r' || t[i] == '\n' || t[i] == '\t') t[i] = ' ';
    if (t.size() < s.size()) t += "...";
    return t;
}

inline const char* actionTypeLabel(ActionType t) {
    switch (t) {
        case ActionType::ReplaceText: return "Text";
        case ActionType::ReplaceFile: return "File";
        case ActionType::ReplaceImage: return "Image";
        case ActionType::PatchBytes:  return "Bytes";
        case ActionType::MemWrite:    return "Memory";
        default:                      return "?";
    }
}

inline std::string describeAction(const PlanAction& a) {
    switch (a.type) {
        case ActionType::ReplaceText:
            return "Replace in " + a.file + ": \"" + shortText(a.find) + "\" -> \"" + shortText(a.replace) + "\"" +
                   (a.all ? "" : " (first match only)");
        case ActionType::ReplaceFile:
            return "Replace " + a.file + " with attachment " + a.attachment;
        case ActionType::ReplaceImage:
            return "Re-texture " + a.file + " with " + a.attachment + " (auto-resized, fit " + a.fit + ")";
        case ActionType::PatchBytes: {
            char off[32];
            std::snprintf(off, sizeof(off), "0x%llX", static_cast<unsigned long long>(a.offset));
            return "Patch " + a.file + " at " + off + ": " + (a.expect.empty() ? std::string("?") : toHexBytes(a.expect)) +
                   " -> " + toHexBytes(a.write);
        }
        case ActionType::MemWrite:
            return "Write to memory: " + a.label + " = " + a.value;
        default:
            return "Unknown action: " + a.typeName;
    }
}

} // namespace heng
