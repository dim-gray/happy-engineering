// settings.h -- Happy Enginiring
// Loads/saves the app's local settings (chosen provider/model/tier and one
// API key per provider) to a small text file.
//
// IMPORTANT, read this before trusting it with a key: the "obfuscation"
// below is a reversible byte scramble (XOR + hex), NOT encryption. Its only
// purpose is to stop a key from sitting in the file as plain, grep-able
// text if someone peeks at it by accident. Anyone with access to this
// settings file and this source code can recover the key. On Windows a
// real secret store (DPAPI / Credential Manager) would protect it properly;
// that integration is not included here. Treat the settings file like you
// would a saved browser password file.
#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include "provider.h"

namespace heng {

namespace detail {
    // Fixed local scramble key. Deliberately NOT a secret -- it only avoids
    // storing keys as raw plaintext; see the file-level comment above.
    inline const char* obfKey() { return "HappyEnginiring-v1"; }

    inline std::string xorScramble(const std::string& in) {
        std::string out(in.size(), '\0');
        const char* k = obfKey();
        size_t klen = std::strlen(k);
        for (size_t i = 0; i < in.size(); ++i) out[i] = in[i] ^ k[i % klen];
        return out;
    }
    inline std::string toHex(const std::string& in) {
        static const char* digits = "0123456789abcdef";
        std::string out; out.reserve(in.size() * 2);
        for (unsigned char c : in) { out.push_back(digits[c >> 4]); out.push_back(digits[c & 0xF]); }
        return out;
    }
    inline std::string fromHex(const std::string& in) {
        std::string out; out.reserve(in.size() / 2);
        auto val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        for (size_t i = 0; i + 1 < in.size(); i += 2) out.push_back(static_cast<char>((val(in[i]) << 4) | val(in[i+1])));
        return out;
    }
    inline std::string obfuscate(const std::string& plain) { return toHex(xorScramble(plain)); }
    inline std::string deobfuscate(const std::string& hex) { return xorScramble(fromHex(hex)); }

    // The settings file is one "key=value" per line, so a value that itself
    // contains a real newline (the user-edited, possibly multi-line system
    // prompt) has to be escaped into a single line and reversed on read.
    inline std::string escapeNewlines(const std::string& in) {
        std::string out; out.reserve(in.size());
        for (char c : in) {
            if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') continue; // normalize CRLF -> \n only
            else out.push_back(c);
        }
        return out;
    }
    inline std::string unescapeNewlines(const std::string& in) {
        std::string out; out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '\\' && i + 1 < in.size()) {
                char nx = in[i + 1];
                if (nx == 'n') { out.push_back('\n'); ++i; continue; }
                if (nx == '\\') { out.push_back('\\'); ++i; continue; }
            }
            out.push_back(in[i]);
        }
        return out;
    }
}

struct Settings {
    Provider provider = Provider::Anthropic;
    Tier tier = Tier::Free;
    std::string model = "claude-haiku-5-5";

    std::string apiKeyOpenAI;
    std::string apiKeyOpenRouter;
    std::string apiKeyAnthropic;

    std::string lastTargetPath; // last file/html path used (not a secret)
    std::string systemPrompt;   // user-edited "personality"; empty = use the built-in default

    std::string& apiKeyFor(Provider p) {
        switch (p) {
            case Provider::OpenAI: return apiKeyOpenAI;
            case Provider::OpenRouter: return apiKeyOpenRouter;
            case Provider::Anthropic: return apiKeyAnthropic;
            case Provider::Custom: return apiKeyOpenAI;
        }
        return apiKeyAnthropic;
    }
};

// Simple "key=value" text format, one per line. Values for api_key_* lines
// are obfuscated (see file header); everything else is stored plainly.
inline std::string serializeSettings(const Settings& s) {
    std::ostringstream o;
    o << "# Happy Enginiring settings\n";
    o << "provider=" << providerName(s.provider) << "\n";
    o << "tier=" << tierName(s.tier) << "\n";
    o << "model=" << s.model << "\n";
    o << "last_target=" << s.lastTargetPath << "\n";
    o << "system_prompt=" << detail::escapeNewlines(s.systemPrompt) << "\n";
    o << "api_key_openai=" << detail::obfuscate(s.apiKeyOpenAI) << "\n";
    o << "api_key_openrouter=" << detail::obfuscate(s.apiKeyOpenRouter) << "\n";
    o << "api_key_anthropic=" << detail::obfuscate(s.apiKeyAnthropic) << "\n";
    return o.str();
}

inline Settings parseSettings(const std::string& text) {
    Settings s;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // strip possible trailing \r (CRLF files)
        if (!val.empty() && val.back() == '\r') val.pop_back();
        if (key == "provider") s.provider = providerFromName(val);
        else if (key == "tier") s.tier = tierFromName(val);
        else if (key == "model") s.model = val;
        else if (key == "last_target") s.lastTargetPath = val;
        else if (key == "system_prompt") s.systemPrompt = detail::unescapeNewlines(val);
        else if (key == "api_key_openai") s.apiKeyOpenAI = detail::deobfuscate(val);
        else if (key == "api_key_openrouter") s.apiKeyOpenRouter = detail::deobfuscate(val);
        else if (key == "api_key_anthropic") s.apiKeyAnthropic = detail::deobfuscate(val);
    }
    return s;
}

inline bool saveSettingsToFile(const Settings& s, const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    std::string text = serializeSettings(s);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(f);
}

// Returns a default-constructed Settings if the file doesn't exist or can't
// be read -- that's a normal first run, not an error worth surfacing.
inline Settings loadSettingsFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return Settings();
    std::ostringstream buf;
    buf << f.rdbuf();
    return parseSettings(buf.str());
}

} // namespace heng
