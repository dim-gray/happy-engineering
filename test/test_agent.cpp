#include <cstdio>
#include <map>
#include "core/agent.h"
#include "core/profiles.h"
using namespace heng;
static int fails = 0, total = 0;
#define CHECK(c) do { ++total; if (!(c)) { ++fails; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct FakeHost : IToolHost {
    std::map<std::string, std::string> files;
    bool list(const std::string& dir, bool rec, size_t, std::vector<HostEntry>& out, std::string&) override {
        for (auto& kv : files) {
            if (!dir.empty() && kv.first.compare(0, dir.size() + 1, dir + "/") != 0) continue;
            std::string rest = dir.empty() ? kv.first : kv.first.substr(dir.size() + 1);
            if (!rec && rest.find('/') != std::string::npos) continue;
            HostEntry e; e.path = kv.first; e.size = kv.second.size(); out.push_back(e);
        }
        return true;
    }
    bool read(const std::string& p, uint64_t off, uint64_t len, std::vector<uint8_t>& out, uint64_t& total, std::string& err) override {
        auto it = files.find(p);
        if (it == files.end()) { err = "no such file"; return false; }
        total = it->second.size();
        if (off > total) off = total;
        uint64_t n = std::min<uint64_t>(len, total - off);
        out.assign(it->second.begin() + off, it->second.begin() + off + n);
        return true;
    }
    std::vector<std::string> attachmentNames() override { return {"logo.png"}; }
};

int main() {
    // tool block parsing
    std::string reply = "Let me look.\n```happy-tool\n{\"tool\":\"list_files\",\"path\":\"data\"}\n```\n```happy-tool\n{bad}\n```\n";
    auto calls = extractToolCalls(reply);
    CHECK(calls.size() == 2); CHECK(calls[0].name == "list_files"); CHECK(!calls[1].error.empty());
    CHECK(stripToolBlocks(reply) == "Let me look.");

    FakeHost h;
    h.files["data/lang/en.ini"] = "[menu]\nstart=Start Game\nquit=Quit\n";
    h.files["data/readme.txt"] = "hello world\nStart here\n";
    h.files["bin/game.dat"] = std::string("\x01\x02\x03\x00\x04", 5) + "Gold: 100";

    auto run = [&](const std::string& json) {
        auto c = extractToolCalls("```happy-tool\n" + json + "\n```");
        return c.empty() ? std::string("<none>") : runTool(c[0], h);
    };
    std::string r = run("{\"tool\":\"list_files\",\"recursive\":true,\"pattern\":\"*.ini\"}");
    CHECK(r.find("data/lang/en.ini") != std::string::npos); CHECK(r.find("readme") == std::string::npos);
    r = run("{\"tool\":\"read_text\",\"path\":\"data/lang/en.ini\"}");
    CHECK(r.find("start=Start Game") != std::string::npos);
    r = run("{\"tool\":\"search_text\",\"query\":\"start\"}");
    CHECK(r.find("data/lang/en.ini:2:") != std::string::npos); CHECK(r.find("data/readme.txt:2:") != std::string::npos);
    r = run("{\"tool\":\"hex_dump\",\"path\":\"bin/game.dat\",\"length\":16}");
    CHECK(r.find("00000000  01 02 03 00 04 47 6F 6C 64") != std::string::npos);
    r = run("{\"tool\":\"read_text\",\"path\":\"bin/game.dat\"}");
    CHECK(r.find("does not look like text") != std::string::npos);
    r = run("{\"tool\":\"read_text\",\"path\":\"../etc/passwd\"}");
    CHECK(r.find("ERROR") == 0);
    r = run("{\"tool\":\"nope\"}");
    CHECK(r.find("unknown tool") != std::string::npos);
    r = run("{\"tool\":\"list_attachments\"}");
    CHECK(r.find("logo.png") != std::string::npos);
    r = run("{\"tool\":\"file_info\",\"path\":\"data/readme.txt\"}");
    CHECK(r.find("text") != std::string::npos);

    // replace_image action parses
    Plan p = extractPlan("```happy-plan\n{\"summary\":\"s\",\"actions\":[{\"type\":\"replace_image\",\"file\":\"a.zip!/t/x.png\",\"with_attachment\":\"logo.png\",\"fit\":\"contain\"}]}\n```");
    CHECK(p.found && p.actions.size() == 1 && p.actions[0].error.empty());
    CHECK(p.actions[0].type == ActionType::ReplaceImage && p.actions[0].fit == "contain" && p.actions[0].file == "a.zip!/t/x.png");

    // chat request
    ApiConfig cfg; cfg.provider = Provider::Custom; cfg.model = "llama3"; cfg.baseUrl = "http://localhost:11434/v1";
    std::vector<ChatMessage> hist(2); hist[0].role = "user"; hist[0].text = "hi"; hist[1].role = "assistant"; hist[1].text = "yo";
    ApiRequest rq = buildChatRequest(cfg, "SYS", hist);
    CHECK(rq.host == "localhost" && rq.port == 11434 && !rq.tls && rq.path == "/v1/chat/completions");
    bool ok; JsonValue b = parseJson(rq.body, &ok);
    CHECK(ok && b.find("messages")->arr->size() == 3);
    cfg.provider = Provider::Anthropic; cfg.baseUrl.clear();
    rq = buildChatRequest(cfg, "SYS", hist);
    b = parseJson(rq.body, &ok);
    CHECK(rq.host == "api.anthropic.com" && b.getString("system") == "SYS" && b.find("messages")->arr->size() == 2);

    // profiles roundtrip
    AppConfig c; ApiProfile p1; p1.name = "Work Claude"; p1.apiKey = "sk-secret"; p1.provider = Provider::Anthropic;
    ApiProfile p2; p2.name = "Local \"LLM\"\nline"; p2.provider = Provider::Custom; p2.baseUrl = "http://localhost:1234/v1"; p2.model = "qwen";
    c.profiles = {p1, p2}; c.activeProfile = 1; c.systemPrompt = "be\nnice"; c.uiScale = 1.25f; c.animations = false;
    SecretFn seal = [](const std::string& s) { return detail::obfuscate(s); };
    SecretFn unseal = [](const std::string& s) { return detail::deobfuscate(s); };
    std::string txt = serializeConfig(c, seal);
    CHECK(txt.find("sk-secret") == std::string::npos);
    AppConfig d = parseConfig(txt, unseal);
    CHECK(d.profiles.size() == 2 && d.profiles[0].apiKey == "sk-secret" && d.profiles[1].name == p2.name);
    CHECK(d.activeProfile == 1 && d.systemPrompt == "be\nnice" && !d.animations && d.profiles[1].baseUrl == p2.baseUrl);

    std::printf("%d checks, %d failed\n", total, fails);
    return fails ? 1 : 0;
}
