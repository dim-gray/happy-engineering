// json.h -- Happy Enginiring
// Minimal, dependency-free JSON reader/writer (C++11).
// Enough to build request bodies for OpenAI / OpenRouter / Anthropic chat
// APIs and to parse their responses. Not a general-purpose validator.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <cstdio>

namespace heng {

class JsonValue;
using JsonArray  = std::vector<JsonValue>;
using JsonObject = std::vector<std::pair<std::string, JsonValue>>; // keeps insertion order

enum class JsonType { Null, Bool, Number, String, Array, Object };

class JsonValue {
public:
    JsonType type = JsonType::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::shared_ptr<JsonArray> arr;
    std::shared_ptr<JsonObject> obj;

    JsonValue() : type(JsonType::Null) {}
    JsonValue(std::nullptr_t) : type(JsonType::Null) {}
    JsonValue(bool v) : type(JsonType::Bool), b(v) {}
    JsonValue(double v) : type(JsonType::Number), num(v) {}
    JsonValue(int v) : type(JsonType::Number), num(static_cast<double>(v)) {}
    JsonValue(const char* v) : type(JsonType::String), str(v) {}
    JsonValue(const std::string& v) : type(JsonType::String), str(v) {}

    static JsonValue makeArray()  { JsonValue v; v.type = JsonType::Array;  v.arr = std::make_shared<JsonArray>();  return v; }
    static JsonValue makeObject() { JsonValue v; v.type = JsonType::Object; v.obj = std::make_shared<JsonObject>(); return v; }

    bool isNull()   const { return type == JsonType::Null; }
    bool isObject() const { return type == JsonType::Object; }
    bool isArray()  const { return type == JsonType::Array; }
    bool isString() const { return type == JsonType::String; }

    // --- Object helpers -----------------------------------------------
    void set(const std::string& key, const JsonValue& v) {
        if (!obj) { type = JsonType::Object; obj = std::make_shared<JsonObject>(); }
        for (auto& kv : *obj) { if (kv.first == key) { kv.second = v; return; } }
        obj->emplace_back(key, v);
    }
    const JsonValue* find(const std::string& key) const {
        if (!obj) return nullptr;
        for (auto& kv : *obj) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string getString(const std::string& key, const std::string& def = "") const {
        const JsonValue* v = find(key);
        return (v && v->type == JsonType::String) ? v->str : def;
    }

    // --- Array helpers ---------------------------------------------------
    void push(const JsonValue& v) {
        if (!arr) { type = JsonType::Array; arr = std::make_shared<JsonArray>(); }
        arr->push_back(v);
    }

    // --- Serialization ----------------------------------------------------
    static void escapeInto(const std::string& s, std::string& out) {
        out.push_back('"');
        for (unsigned char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
            }
        }
        out.push_back('"');
    }

    void dump(std::string& out) const {
        switch (type) {
            case JsonType::Null: out += "null"; break;
            case JsonType::Bool: out += b ? "true" : "false"; break;
            case JsonType::Number: {
                if (num == static_cast<long long>(num)) {
                    out += std::to_string(static_cast<long long>(num));
                } else {
                    std::ostringstream ss; ss << num; out += ss.str();
                }
                break;
            }
            case JsonType::String: escapeInto(str, out); break;
            case JsonType::Array: {
                out.push_back('[');
                if (arr) for (size_t i = 0; i < arr->size(); ++i) {
                    if (i) out.push_back(',');
                    (*arr)[i].dump(out);
                }
                out.push_back(']');
                break;
            }
            case JsonType::Object: {
                out.push_back('{');
                if (obj) for (size_t i = 0; i < obj->size(); ++i) {
                    if (i) out.push_back(',');
                    escapeInto((*obj)[i].first, out);
                    out.push_back(':');
                    (*obj)[i].second.dump(out);
                }
                out.push_back('}');
                break;
            }
        }
    }
    std::string dump() const { std::string s; dump(s); return s; }
};

// --- Parser -----------------------------------------------------------
class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s(text), i(0), n(text.size()) {}

    JsonValue parse() {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        return v;
    }
    bool ok() const { return !failed; }
    const std::string& error() const { return errMsg; }

private:
    const std::string& s;
    size_t i, n;
    bool failed = false;
    std::string errMsg;

    void fail(const std::string& m) { if (!failed) { failed = true; errMsg = m; } }
    char peek() const { return i < n ? s[i] : '\0'; }
    void skipWs() { while (i < n && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }

    JsonValue parseValue() {
        if (failed) return JsonValue();
        skipWs();
        if (i >= n) { fail("unexpected end"); return JsonValue(); }
        char c = s[i];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return JsonValue(parseString());
        if (c == 't') { expectLiteral("true");  return JsonValue(true); }
        if (c == 'f') { expectLiteral("false"); return JsonValue(false); }
        if (c == 'n') { expectLiteral("null");  return JsonValue(nullptr); }
        if (c == '-' || std::isdigit((unsigned char)c)) return parseNumber();
        fail("unexpected character");
        return JsonValue();
    }

    void expectLiteral(const char* lit) {
        size_t len = std::strlen(lit);
        if (s.compare(i, len, lit) != 0) { fail(std::string("expected '") + lit + "'"); return; }
        i += len;
    }

    JsonValue parseNumber() {
        size_t start = i;
        if (peek() == '-') ++i;
        while (i < n && std::isdigit((unsigned char)s[i])) ++i;
        if (peek() == '.') { ++i; while (i < n && std::isdigit((unsigned char)s[i])) ++i; }
        if (peek() == 'e' || peek() == 'E') {
            ++i; if (peek()=='+'||peek()=='-') ++i;
            while (i < n && std::isdigit((unsigned char)s[i])) ++i;
        }
        if (i == start) { fail("bad number"); return JsonValue(0.0); }
        try { return JsonValue(std::stod(s.substr(start, i - start))); }
        catch (...) { fail("bad number"); return JsonValue(0.0); }
    }

    std::string parseString() {
        std::string out;
        if (peek() != '"') { fail("expected string"); return out; }
        ++i;
        while (i < n && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < n) {
                char e = s[i++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (i + 4 > n) { fail("bad unicode escape"); break; }
                        unsigned int cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                        }
                        // naive UTF-8 encode (no surrogate pairing; sufficient for our use)
                        if (cp < 0x80) out.push_back(static_cast<char>(cp));
                        else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: out.push_back(e);
                }
            } else {
                out.push_back(c);
            }
        }
        if (i < n && s[i] == '"') ++i; else fail("unterminated string");
        return out;
    }

    JsonValue parseArray() {
        JsonValue v = JsonValue::makeArray();
        ++i; skipWs();
        if (peek() == ']') { ++i; return v; }
        while (true) {
            v.push(parseValue());
            skipWs();
            if (peek() == ',') { ++i; skipWs(); continue; }
            if (peek() == ']') { ++i; break; }
            fail("expected ',' or ']' in array"); break;
        }
        return v;
    }

    JsonValue parseObject() {
        JsonValue v = JsonValue::makeObject();
        ++i; skipWs();
        if (peek() == '}') { ++i; return v; }
        while (true) {
            skipWs();
            if (peek() != '"') { fail("expected object key"); break; }
            std::string key = parseString();
            skipWs();
            if (peek() != ':') { fail("expected ':' in object"); break; }
            ++i;
            v.set(key, parseValue());
            skipWs();
            if (peek() == ',') { ++i; continue; }
            if (peek() == '}') { ++i; break; }
            fail("expected ',' or '}' in object"); break;
        }
        return v;
    }
};

inline JsonValue parseJson(const std::string& text, bool* okOut = nullptr, std::string* errOut = nullptr) {
    JsonParser p(text);
    JsonValue v = p.parse();
    if (okOut) *okOut = p.ok();
    if (errOut) *errOut = p.error();
    return v;
}

} // namespace heng
