// provider.h -- Happy Enginiring
// Provider/model selection plus request-body building and response parsing
// for the three supported AI backends (OpenAI, OpenRouter, Anthropic). This
// module only builds and reads JSON text; the HTTPS transport lives in
// src/win/winhttp_client.h (Windows-only), which keeps this header portable
// and unit-testable.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include "json.h"

namespace heng {

enum class Provider { OpenAI, OpenRouter, Anthropic, Custom };
enum class Tier { Free, Pro };

inline const char* providerName(Provider p) {
    switch (p) {
        case Provider::OpenAI: return "OpenAI";
        case Provider::OpenRouter: return "OpenRouter";
        case Provider::Anthropic: return "Anthropic";
        case Provider::Custom: return "Custom";
    }
    return "?";
}

inline Provider providerFromName(const std::string& name) {
    if (name == "OpenRouter") return Provider::OpenRouter;
    if (name == "Anthropic") return Provider::Anthropic;
    if (name == "Custom") return Provider::Custom;
    return Provider::OpenAI;
}

inline const char* tierName(Tier t) { return t == Tier::Pro ? "Pro" : "Free"; }
inline Tier tierFromName(const std::string& n) { return n == "Pro" ? Tier::Pro : Tier::Free; }

// Suggested models per provider/tier. The model field in the UI is free
// text, so this list only seeds the drop-down: provider catalogs change
// often and the app does not call home to refresh them. "openrouter/free"
// is OpenRouter's own router that picks an available free model.
inline std::vector<std::string> suggestedModels(Provider p, Tier t) {
    switch (p) {
        case Provider::OpenAI:
            return t == Tier::Pro
                ? std::vector<std::string>{"gpt-5.1", "gpt-5", "gpt-4.1", "o3"}
                : std::vector<std::string>{"gpt-5-mini", "gpt-5-nano", "gpt-4.1-mini"};
        case Provider::Anthropic:
            return t == Tier::Pro
                ? std::vector<std::string>{"claude-opus-5-5", "claude-sonnet-5-5"}
                : std::vector<std::string>{"claude-haiku-5-5"};
        case Provider::OpenRouter:
            return t == Tier::Pro
                ? std::vector<std::string>{"anthropic/claude-sonnet-5-5", "openai/gpt-5.1"}
                : std::vector<std::string>{"openrouter/free"};
        case Provider::Custom:
            return {};
    }
    return {};
}

struct ApiRequest {
    std::string host;        // e.g. "api.openai.com"
    std::string path;        // e.g. "/v1/chat/completions"
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;        // JSON text
    bool tls = true;         // https (true) or plain http (local servers)
    int port = 0;            // 0 = default for the scheme
};

// An image attached to the question (sent to vision-capable models).
struct ImagePart {
    std::string label;       // attachment name, shown to the model before the image
    std::string mime;        // "image/png", "image/jpeg", "image/gif", "image/webp"
    std::string base64;      // base64 of the raw file bytes
};

namespace provider_detail {

inline JsonValue textPart(const std::string& text) {
    JsonValue p = JsonValue::makeObject();
    p.set("type", JsonValue("text"));
    p.set("text", JsonValue(text));
    return p;
}

// The "content" of the single user message: a plain string when there are
// no images, otherwise a list of parts in each provider's own schema.
inline JsonValue userContent(Provider provider, const std::string& userPrompt,
                             const std::vector<ImagePart>& images) {
    if (images.empty()) return JsonValue(userPrompt);
    JsonValue parts = JsonValue::makeArray();
    if (provider == Provider::Anthropic) {
        for (size_t i = 0; i < images.size(); ++i) {
            const ImagePart& im = images[i];
            if (!im.label.empty()) parts.push(textPart("Image attachment: " + im.label));
            JsonValue src = JsonValue::makeObject();
            src.set("type", JsonValue("base64"));
            src.set("media_type", JsonValue(im.mime));
            src.set("data", JsonValue(im.base64));
            JsonValue p = JsonValue::makeObject();
            p.set("type", JsonValue("image"));
            p.set("source", src);
            parts.push(p);
        }
        parts.push(textPart(userPrompt));
    } else {
        parts.push(textPart(userPrompt));
        for (size_t i = 0; i < images.size(); ++i) {
            const ImagePart& im = images[i];
            if (!im.label.empty()) parts.push(textPart("Image attachment: " + im.label));
            JsonValue url = JsonValue::makeObject();
            url.set("url", JsonValue("data:" + im.mime + ";base64," + im.base64));
            JsonValue p = JsonValue::makeObject();
            p.set("type", JsonValue("image_url"));
            p.set("image_url", url);
            parts.push(p);
        }
    }
    return parts;
}

inline JsonValue message(const char* role, const JsonValue& content) {
    JsonValue m = JsonValue::makeObject();
    m.set("role", JsonValue(role));
    m.set("content", content);
    return m;
}

} // namespace provider_detail

// Builds the HTTPS request for one provider. `system` is the assistant's
// personality plus the fixed plan protocol; `userPrompt` is the person's
// instructions followed by the gathered context. Keeping them separate lets
// each provider place them correctly (Anthropic uses a top-level `system`
// field; OpenAI/OpenRouter use a `system` role message).
inline ApiRequest buildRequestWithImages(Provider provider, const std::string& model,
                                         const std::string& apiKey,
                                         const std::string& system,
                                         const std::string& userPrompt,
                                         const std::vector<ImagePart>& images,
                                         int maxTokens = 4096) {
    using namespace provider_detail;
    ApiRequest req;
    JsonValue body = JsonValue::makeObject();
    body.set("model", JsonValue(model));
    JsonValue content = userContent(provider, userPrompt, images);
    switch (provider) {
        case Provider::Custom:
        case Provider::OpenAI: {
            req.host = "api.openai.com";
            req.path = "/v1/chat/completions";
            req.headers = {
                {"Authorization", "Bearer " + apiKey},
                {"Content-Type", "application/json"},
            };
            JsonValue msgs = JsonValue::makeArray();
            msgs.push(message("system", JsonValue(system)));
            msgs.push(message("user", content));
            body.set("messages", msgs);
            // newer OpenAI models reject max_tokens; max_completion_tokens
            // is accepted by both old and new chat models
            body.set("max_completion_tokens", JsonValue(maxTokens));
            break;
        }
        case Provider::OpenRouter: {
            req.host = "openrouter.ai";
            req.path = "/api/v1/chat/completions";
            req.headers = {
                {"Authorization", "Bearer " + apiKey},
                {"Content-Type", "application/json"},
                {"HTTP-Referer", "https://github.com/dim-gray/happy-engineering"},
                {"X-Title", "Happy Engineering"},
            };
            JsonValue msgs = JsonValue::makeArray();
            msgs.push(message("system", JsonValue(system)));
            msgs.push(message("user", content));
            body.set("messages", msgs);
            body.set("max_tokens", JsonValue(maxTokens));
            break;
        }
        case Provider::Anthropic: {
            req.host = "api.anthropic.com";
            req.path = "/v1/messages";
            req.headers = {
                {"x-api-key", apiKey},
                {"anthropic-version", "2023-06-01"},
                {"Content-Type", "application/json"},
            };
            body.set("system", JsonValue(system));
            JsonValue msgs = JsonValue::makeArray();
            msgs.push(message("user", content));
            body.set("messages", msgs);
            body.set("max_tokens", JsonValue(maxTokens));
            break;
        }
    }
    req.body = body.dump();
    return req;
}

inline ApiRequest buildRequest(Provider provider, const std::string& model,
                               const std::string& apiKey,
                               const std::string& system,
                               const std::string& userPrompt,
                               int maxTokens = 4096) {
    return buildRequestWithImages(provider, model, apiKey, system, userPrompt,
                                  std::vector<ImagePart>(), maxTokens);
}

struct ApiResult {
    bool ok = false;
    bool truncated = false;  // the model hit its output limit mid-answer
    std::string text;        // assistant reply, if ok
    std::string error;       // human-readable error, if !ok
};

// Parses the JSON response body from the given provider. httpStatus lets us
// surface HTTP-level failures (bad key, rate limit, ...) distinctly from
// JSON parse failures.
inline ApiResult parseResponse(Provider provider, int httpStatus, const std::string& responseBody) {
    ApiResult res;
    bool jsonOk = false;
    JsonValue v = parseJson(responseBody, &jsonOk);

    if (httpStatus != 200) {
        std::string msg;
        if (jsonOk) {
            const JsonValue* err = v.find("error");
            if (err) msg = err->getString("message", "");
        }
        if (msg.empty()) msg = responseBody.substr(0, 400);
        res.error = "HTTP " + std::to_string(httpStatus) + ": " + msg;
        return res;
    }
    if (!jsonOk) { res.error = "invalid response (not JSON)"; return res; }

    switch (provider) {
        case Provider::OpenAI:
        case Provider::Custom:
        case Provider::OpenRouter: {
            const JsonValue* choices = v.find("choices");
            if (choices && choices->isArray() && !choices->arr->empty()) {
                const JsonValue& first = (*choices->arr)[0];
                const JsonValue* msg = first.find("message");
                if (msg) {
                    const JsonValue* content = msg->find("content");
                    if (content && content->isString()) {
                        res.ok = true;
                        res.text = content->str;
                        res.truncated = (first.getString("finish_reason") == "length");
                        return res;
                    }
                    std::string refusal = msg->getString("refusal");
                    if (!refusal.empty()) { res.error = "the model refused: " + refusal; return res; }
                }
            }
            const JsonValue* err = v.find("error");
            res.error = err ? err->getString("message", "unexpected response") : "unexpected response from the provider";
            return res;
        }
        case Provider::Anthropic: {
            const JsonValue* content = v.find("content");
            if (content && content->isArray()) {
                std::string text;
                for (size_t i = 0; i < content->arr->size(); ++i) {
                    const JsonValue& block = (*content->arr)[i];
                    if (block.getString("type") == "text") text += block.getString("text");
                }
                if (!text.empty()) {
                    res.ok = true;
                    res.text = text;
                    res.truncated = (v.getString("stop_reason") == "max_tokens");
                    return res;
                }
            }
            const JsonValue* err = v.find("error");
            res.error = err ? err->getString("message", "unexpected response") : "unexpected response from the provider";
            return res;
        }
    }
    res.error = "unknown provider";
    return res;
}

} // namespace heng
