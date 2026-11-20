// chat.h -- Happy Engineering
// Multi-turn chat requests (agent loop), custom OpenAI-compatible endpoints.
#pragma once
#include <string>
#include <vector>
#include "provider.h"

namespace heng {

struct ChatMessage {
    std::string role;                 // "user" or "assistant"
    std::string text;
    std::vector<ImagePart> images;    // only honoured on user messages
};

struct ApiConfig {
    Provider provider = Provider::Anthropic;
    std::string model;
    std::string apiKey;
    std::string baseUrl;   // optional override, e.g. "http://localhost:11434/v1" or "https://my.proxy/v1"
};

struct ParsedUrl { bool ok = false; bool tls = true; int port = 0; std::string host, prefix; };

// "https://host:8443/v1/" -> {tls, 8443, host, "/v1"}. A missing scheme means https.
inline ParsedUrl parseBaseUrl(const std::string& url) {
    ParsedUrl u;
    std::string s = url;
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return u;
    s = s.substr(a, b - a + 1);
    if (s.compare(0, 7, "http://") == 0) { u.tls = false; s = s.substr(7); }
    else if (s.compare(0, 8, "https://") == 0) { s = s.substr(8); }
    size_t slash = s.find('/');
    std::string hostPort = slash == std::string::npos ? s : s.substr(0, slash);
    u.prefix = slash == std::string::npos ? std::string() : s.substr(slash);
    while (!u.prefix.empty() && u.prefix.back() == '/') u.prefix.pop_back();
    size_t colon = hostPort.rfind(':');
    if (colon != std::string::npos && hostPort.find(']') == std::string::npos) {
        u.port = std::atoi(hostPort.substr(colon + 1).c_str());
        hostPort = hostPort.substr(0, colon);
    }
    u.host = hostPort;
    u.ok = !u.host.empty();
    return u;
}

inline ApiRequest buildChatRequest(const ApiConfig& cfg, const std::string& system,
                                   const std::vector<ChatMessage>& history, int maxTokens = 8192) {
    using namespace provider_detail;
    ApiRequest req;
    JsonValue body = JsonValue::makeObject();
    body.set("model", JsonValue(cfg.model));
    JsonValue msgs = JsonValue::makeArray();
    bool anthropic = cfg.provider == Provider::Anthropic;
    if (!anthropic) msgs.push(message("system", JsonValue(system)));
    for (size_t i = 0; i < history.size(); ++i) {
        const ChatMessage& m = history[i];
        JsonValue content = (m.role == "user") ? userContent(cfg.provider, m.text, m.images) : JsonValue(m.text);
        msgs.push(message(m.role == "user" ? "user" : "assistant", content));
    }
    body.set("messages", msgs);
    ParsedUrl u = parseBaseUrl(cfg.baseUrl);
    switch (cfg.provider) {
        case Provider::OpenAI:
        case Provider::Custom:
            req.host = "api.openai.com"; req.path = "/v1/chat/completions";
            if (u.ok) { req.host = u.host; req.tls = u.tls; req.port = u.port; req.path = (u.prefix.empty() ? "/v1" : u.prefix) + "/chat/completions"; }
            req.headers = {{"Content-Type", "application/json"}};
            if (!cfg.apiKey.empty()) req.headers.push_back({"Authorization", "Bearer " + cfg.apiKey});
            body.set("max_completion_tokens", JsonValue(maxTokens));
            break;
        case Provider::OpenRouter:
            req.host = "openrouter.ai"; req.path = "/api/v1/chat/completions";
            if (u.ok) { req.host = u.host; req.tls = u.tls; req.port = u.port; req.path = (u.prefix.empty() ? "/api/v1" : u.prefix) + "/chat/completions"; }
            req.headers = {{"Authorization", "Bearer " + cfg.apiKey}, {"Content-Type", "application/json"},
                           {"HTTP-Referer", "https://github.com/dim-gray/happy-engineering"}, {"X-Title", "Happy Engineering"}};
            body.set("max_tokens", JsonValue(maxTokens));
            break;
        case Provider::Anthropic:
            req.host = "api.anthropic.com"; req.path = "/v1/messages";
            if (u.ok) { req.host = u.host; req.tls = u.tls; req.port = u.port; req.path = (u.prefix.empty() ? "/v1" : u.prefix) + "/messages"; }
            req.headers = {{"x-api-key", cfg.apiKey}, {"anthropic-version", "2023-06-01"}, {"Content-Type", "application/json"}};
            body.set("system", JsonValue(system));
            body.set("max_tokens", JsonValue(maxTokens));
            break;
    }
    req.body = body.dump();
    return req;
}

} // namespace heng
