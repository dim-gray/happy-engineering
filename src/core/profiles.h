// profiles.h -- Happy Engineering
// Saved, named API profiles + the app configuration file. Secrets (API keys)
// are passed through a caller-supplied "seal" function (DPAPI on Windows,
// obfuscation on Linux) so this header stays portable and testable.
#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <functional>
#include "chat.h"
#include "settings.h"   // detail::escapeNewlines / toHex helpers

namespace heng {

struct ApiProfile {
    std::string name = "My profile";
    Provider provider = Provider::Anthropic;
    Tier tier = Tier::Free;
    std::string model = "claude-haiku-5-5";
    std::string apiKey;
    std::string baseUrl;      // optional custom endpoint
    int maxTokens = 8192;

    ApiConfig toConfig() const { ApiConfig c; c.provider = provider; c.model = model; c.apiKey = apiKey; c.baseUrl = baseUrl; return c; }
};

struct AppConfig {
    std::vector<ApiProfile> profiles;
    int activeProfile = 0;
    std::string systemPrompt;        // empty = default personality
    std::string lastTargetPath;
    std::string outputDir;           // empty = default
    float uiScale = 1.0f;
    bool animations = true;
    bool backupOnInstall = true;
    int agentMaxSteps = 12;
    bool confirmBeforeApply = true;

    ApiProfile* active() {
        if (profiles.empty()) return nullptr;
        if (activeProfile < 0 || activeProfile >= (int)profiles.size()) activeProfile = 0;
        return &profiles[activeProfile];
    }
};

typedef std::function<std::string(const std::string&)> SecretFn;

inline std::string serializeConfig(const AppConfig& c, const SecretFn& seal) {
    std::ostringstream o;
    o << "# Happy Engineering config\nversion=2\n";
    o << "active=" << c.activeProfile << "\n";
    o << "system_prompt=" << detail::escapeNewlines(c.systemPrompt) << "\n";
    o << "last_target=" << c.lastTargetPath << "\n";
    o << "output_dir=" << c.outputDir << "\n";
    o << "ui_scale=" << c.uiScale << "\n";
    o << "animations=" << (c.animations ? 1 : 0) << "\n";
    o << "backup_on_install=" << (c.backupOnInstall ? 1 : 0) << "\n";
    o << "confirm_apply=" << (c.confirmBeforeApply ? 1 : 0) << "\n";
    o << "agent_max_steps=" << c.agentMaxSteps << "\n";
    for (size_t i = 0; i < c.profiles.size(); ++i) {
        const ApiProfile& p = c.profiles[i];
        o << "profile=" << detail::toHex(p.name) << "\n";   // starts a new profile
        o << "p.provider=" << providerName(p.provider) << "\n";
        o << "p.tier=" << tierName(p.tier) << "\n";
        o << "p.model=" << p.model << "\n";
        o << "p.base_url=" << p.baseUrl << "\n";
        o << "p.max_tokens=" << p.maxTokens << "\n";
        o << "p.key=" << detail::toHex(seal(p.apiKey)) << "\n";
    }
    return o.str();
}

inline AppConfig parseConfig(const std::string& text, const SecretFn& unseal) {
    AppConfig c;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "active") c.activeProfile = std::atoi(v.c_str());
        else if (k == "system_prompt") c.systemPrompt = detail::unescapeNewlines(v);
        else if (k == "last_target") c.lastTargetPath = v;
        else if (k == "output_dir") c.outputDir = v;
        else if (k == "ui_scale") { c.uiScale = (float)std::atof(v.c_str()); if (c.uiScale < 0.6f || c.uiScale > 3.f) c.uiScale = 1.f; }
        else if (k == "animations") c.animations = v != "0";
        else if (k == "backup_on_install") c.backupOnInstall = v != "0";
        else if (k == "confirm_apply") c.confirmBeforeApply = v != "0";
        else if (k == "agent_max_steps") { c.agentMaxSteps = std::atoi(v.c_str()); if (c.agentMaxSteps < 1 || c.agentMaxSteps > 40) c.agentMaxSteps = 12; }
        else if (k == "profile") { ApiProfile p; p.name = detail::fromHex(v); c.profiles.push_back(p); }
        else if (!c.profiles.empty()) {
            ApiProfile& p = c.profiles.back();
            if (k == "p.provider") p.provider = providerFromName(v);
            else if (k == "p.tier") p.tier = tierFromName(v);
            else if (k == "p.model") p.model = v;
            else if (k == "p.base_url") p.baseUrl = v;
            else if (k == "p.max_tokens") { p.maxTokens = std::atoi(v.c_str()); if (p.maxTokens < 256) p.maxTokens = 8192; }
            else if (k == "p.key") p.apiKey = unseal(detail::fromHex(v));
        }
    }
    if (c.activeProfile < 0 || c.activeProfile >= (int)c.profiles.size()) c.activeProfile = 0;
    return c;
}

} // namespace heng
