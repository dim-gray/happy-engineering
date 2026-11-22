// Demo content for screenshots.
#include "ui/app.h"
#include "ui/theme.h"
#include "app/fsutil.h"
#include "miniz.h"
namespace he {
void App::debugLoadDemo() {
    fs::path g = "/tmp/claude-0/demo_game"; fs::remove_all(g); fs::create_directories(g / "data" / "lang"); fs::create_directories(g / "textures");
    std::string e;
    auto w = [&](const std::string& rel, const std::string& s) { writeWholeFile((g / rel).generic_string(), std::vector<uint8_t>(s.begin(), s.end()), e); };
    w("data/lang/en.ini", "[menu]\nstart=Start Game\nquit=Quit\nsword_cost=120\n"); w("readme.txt", "Demo game"); w("game.exe", std::string(2048, 'x'));
    std::string z = (g / "data.pk3").generic_string(); mz_zip_add_mem_to_archive_file_in_place(z.c_str(), "textures/logo.png", "x", 1, nullptr, 0, MZ_DEFAULT_LEVEL);
    cfg_.profiles.clear();
    heng::ApiProfile a; a.name = "Claude Pro"; a.provider = heng::Provider::Anthropic; a.tier = heng::Tier::Pro; a.model = "claude-sonnet-5-5"; a.apiKey = "sk-ant-demo"; cfg_.profiles.push_back(a);
    heng::ApiProfile b; b.name = "OpenRouter free"; b.provider = heng::Provider::OpenRouter; b.model = "openrouter/free"; cfg_.profiles.push_back(b);
    heng::ApiProfile c; c.name = "Local llama"; c.provider = heng::Provider::Custom; c.model = "llama3"; c.baseUrl = "http://localhost:11434/v1"; cfg_.profiles.push_back(c);
    cfg_.activeProfile = 0; selProfile_ = 0;
    setTargetPath(g.generic_string());
    std::string d1 = "bin/img.png"; ws_.addAttachment("/tmp/claude-0/demo_game/readme.txt", e);
    ai_.addLog(LogKind::User, "You", "Make the sword cost 50 gold and translate the menu to Romanian.");
    ai_.addLog(LogKind::Assistant, "Happy", "Sure - let me look at how the game stores its strings and prices.");
    ai_.addLog(LogKind::ToolCall, "list_files", "{\"tool\":\"list_files\",\"path\":\"data\",\"recursive\":true}");
    ai_.addLog(LogKind::ToolResult, "list_files", "data/lang/en.ini  (62 B)\ndata.pk3  (1 KB)\ndata.pk3!/textures/logo.png (1 B)");
    ai_.addLog(LogKind::ToolCall, "read_text", "{\"tool\":\"read_text\",\"path\":\"data/lang/en.ini\"}");
    ai_.addLog(LogKind::ToolResult, "read_text", "[menu]\nstart=Start Game\nquit=Quit\nsword_cost=120");
    ai_.addLog(LogKind::Assistant, "Happy", "Found it. `sword_cost` is in **data/lang/en.ini**. Here is what I'd change:\n```c\nint sword_cost(void) {\n    return 50;\n}\n```\n```happy-plan\n{\"summary\":\"...\"}\n```");
    heng::Plan p = heng::extractPlan("```happy-plan\n{\"summary\":\"Sword costs 50 gold, menu in Romanian\",\"actions\":["
        "{\"type\":\"replace_text\",\"file\":\"data/lang/en.ini\",\"find\":\"sword_cost=120\",\"replace\":\"sword_cost=50\",\"note\":\"price tweak\"},"
        "{\"type\":\"replace_text\",\"file\":\"data/lang/en.ini\",\"find\":\"Start Game\",\"replace\":\"Porneste jocul\"},"
        "{\"type\":\"replace_image\",\"file\":\"data.pk3!/textures/logo.png\",\"with_attachment\":\"readme.txt\",\"fit\":\"cover\"},"
        "{\"type\":\"patch_bytes\",\"file\":\"game.exe\",\"offset\":\"0x10\",\"expect\":\"78 78\",\"write\":\"90 90\"}]}\n```");
    applyIncomingPlan(p); page_ = Page::Workspace;
    heng::WatchEntry we; we.label = "gold"; we.address = 0x1A2B3C40; we.type = heng::ValueType::Int32; we.lastText = "1250"; watch_.push_back(we);
    we.label = "hp"; we.address = 0x1A2B3D00; we.lastText = "100"; we.frozen = true; watch_.push_back(we);
    outputsAt_ = -100;
}
}
