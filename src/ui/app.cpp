#include "app.h"
#include "theme.h"
#include "app/fsutil.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

namespace he {
using namespace ui;

App::App() {}
App::~App() { shutdown(); }

void App::init() {
    cfgPath_ = platform::configDir() + "/config.cfg";
    std::vector<uint8_t> b; std::string err;
    if (readWholeFile(cfgPath_, b, err)) {
        cfg_ = heng::parseConfig(std::string(b.begin(), b.end()), [](const std::string& s) { return platform::unsealSecret(s); });
    }
    if (cfg_.profiles.empty()) {
        heng::ApiProfile p; p.name = "Claude"; p.provider = heng::Provider::Anthropic; p.model = "claude-sonnet-5-5"; p.tier = heng::Tier::Pro;
        cfg_.profiles.push_back(p);
    }
    selProfile_ = cfg_.activeProfile;
    ui::setAnimations(cfg_.animations);
    if (!cfg_.lastTargetPath.empty()) {
        std::error_code ec;
        if (fs::exists(P(cfg_.lastTargetPath), ec)) setTargetPath(cfg_.lastTargetPath);
    }
}

void App::shutdown() {
    static bool done = false; if (done) return; done = true;
    ai_.cancel();
    if (test_.th.joinable()) test_.th.join();
    if (scanTh_.joinable()) scanTh_.join();
    if (!cfgPath_.empty()) saveConfig();
}

void App::saveConfig() {
    if (cfgPath_.empty()) return;
    std::string txt = heng::serializeConfig(cfg_, [](const std::string& s) { return platform::sealSecret(s); });
    std::vector<uint8_t> b(txt.begin(), txt.end()); std::string err;
    if (writeWholeFile(cfgPath_, b, err)) platform::setSecureFileMode(cfgPath_);
    else ui::toast(Toast::Error, "Could not save settings: " + err);
    cfgDirty_ = false;
}

bool App::consumeScaleChange(float& s) {
    float want = effectiveScale();
    if (std::fabs(want - appliedScale_) < 0.01f) return false;
    appliedScale_ = want; s = want; return true;
}

// ----------------------------------------------------------------- frame
static const char* pageTitle(Page p) {
    switch (p) { case Page::Workspace: return "Workspace"; case Page::Plan: return "Plan"; case Page::Memory: return "Memory"; case Page::Output: return "Output"; case Page::About: return "About"; }
    return "";
}
static const char* pageSub(Page p) {
    switch (p) {
        case Page::Workspace: return "Pick a target, tell Happy what you want changed.";
        case Page::Plan: return "Review every change before anything is written.";
        case Page::Memory: return "Find values in a running game and name them for the AI.";
        case Page::Output: return "Modified files live here. Install them with a backup.";
        case Page::About: return "";
    }
    return "";
}

void App::frame(ImVec2 display) {
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ui::drawBackground(display, 1.f);
    settingsAnim_ = ui::anim(ImGui::GetID("settingsOpenAnim"), settingsOpen_ ? 1.f : 0.f, 12.f);

    // incoming plan from the AI thread
    heng::Plan np; if (ai_.takePlan(np)) applyIncomingPlan(np);
    if (cfgDirty_ && ImGui::GetTime() - cfgDirtyAt_ > 1.0) saveConfig();
    tickMemory();

    float s = ImGui::GetFontSize() / 17.f;
    float sideW = 250 * s, pad = 16 * s;
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);

    ImGui::SetCursorPos(ImVec2(pad, pad));
    drawSidebar(sideW, display.y - pad * 2);
    ImGui::SetCursorPos(ImVec2(pad * 2 + sideW, pad));
    float mainW = display.x - sideW - pad * 3;
    ImGui::BeginChild("##main", ImVec2(mainW, display.y - pad * 2), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawTopBar(mainW);
    float a = ui::pageEnter((int)page_);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(0.02f, a));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (1.f - a) * 22.f * s);
    float contentH = ImGui::GetContentRegionAvail().y - (1.f - a) * 22.f * s;
    switch (page_) {
        case Page::Workspace: pageWorkspace(mainW, contentH); break;
        case Page::Plan: pagePlan(mainW, contentH); break;
        case Page::Memory: pageMemory(mainW, contentH); break;
        case Page::Output: pageOutput(mainW, contentH); break;
        case Page::About: pageAbout(mainW, contentH); break;
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    if (settingsAnim_ > 0.01f) ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), display, IM_COL32(6, 0, 20, (int)(165 * settingsAnim_)));
    ImGui::End();

    drawSettingsWindow(display);
    std::string chosen; int purpose = 0;
    if (browser_.draw(chosen, purpose)) {
        if (purpose == 1) { targetKind_ = 0; setTargetPath(chosen); }
        else if (purpose == 2) { targetKind_ = 1; setTargetPath(chosen); }
        else if (purpose == 3) { std::string e; if (ws_.addAttachment(chosen, e)) ui::toast(Toast::Success, "Attached " + heng::baseNameOf(chosen)); else ui::toast(Toast::Error, e); }
        else if (purpose == 4) { cfg_.outputDir = chosen; markDirty(); }
        else if (purpose == 5) { targetKind_ = 2; setTargetPath(chosen); }
    }
    ui::drawToasts(display);
    lastPage_ = page_;
}

// ----------------------------------------------------------------- sidebar
void App::drawSidebar(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##side", ImVec2(w, h), nullptr, ui::col::pink, false);
    // logo
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(); float tile = 58 * s;
    ImVec2 a = p, b(p.x + tile, p.y + tile);
    for (int g = 3; g >= 1; --g) dl->AddRect(ImVec2(a.x - g * 2, a.y - g * 2), ImVec2(b.x + g * 2, b.y + g * 2), ui::col::withAlpha(ui::col::yellow, 0.06f * (4 - g)), 16 * s + g * 2, 0, 2.f);
    dl->AddRectFilled(a, b, IM_COL32(243, 229, 171, 255), 14 * s);
    dl->AddRectFilled(ImVec2(a.x + 2, a.y + 2), ImVec2(b.x - 2, a.y + tile * 0.45f), IM_COL32(255, 250, 220, 90), 12 * s, ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, ui::col::withAlpha(ui::col::yellow, 0.9f), 14 * s, 0, 2.f);
    ImVec2 ts = ImVec2(0, 0);
    ImGui::PushFont(ui::fonts.title); ts = ImGui::CalcTextSize(":)"); dl->AddText(ui::fonts.title, ui::fonts.title->FontSize, ImVec2(a.x + (tile - ts.x) * .5f, a.y + (tile - ts.y) * .5f - 1), IM_COL32(58, 38, 24, 255), ":)"); ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x + tile + 12 * s, p.y + 4 * s));
    ImGui::BeginGroup();
    ui::gradientText("Happy", ui::col::pink, ui::col::orange, ui::fonts.title);
    ImGui::SetCursorScreenPos(ImVec2(p.x + tile + 12 * s, p.y + 4 * s + ui::fonts.title->FontSize + 2));
    ImGui::PushFont(ui::fonts.medium); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::cyan), "Engineering"); ImGui::PopFont();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + tile + 18 * s));
    float nw = w - ImGui::GetStyle().WindowPadding.x * 2;
    struct Nav { const char* n; Icon ic; Page pg; } items[] = {
        {"Workspace", Icon::Workspace, Page::Workspace}, {"Plan", Icon::Plan, Page::Plan},
        {"Memory", Icon::Memory, Page::Memory}, {"Output", Icon::Folder, Page::Output}};
    for (auto& it : items) {
        ImVec2 q = ImGui::GetCursorScreenPos();
        if (ui::navItem(it.n, it.ic, page_ == it.pg, nw)) page_ = it.pg;
        if (it.pg == Page::Plan && !plan_.actions.empty()) {
            std::string c = std::to_string(plan_.actions.size());
            ImVec2 tsz = ImGui::CalcTextSize(c.c_str()); float r = std::max(tsz.x, tsz.y) * .75f;
            ImVec2 ctr(q.x + nw - r - 10 * s, q.y + ImGui::GetFontSize() * 1.25f);
            float pulse = 0.5f + 0.5f * std::sin((float)ImGui::GetTime() * 3.f);
            dl->AddCircleFilled(ctr, r + 2 + pulse * 2, ui::col::withAlpha(ui::col::cyan, 0.15f));
            dl->AddCircleFilled(ctr, r, ui::col::cyan);
            dl->AddText(ImVec2(ctr.x - tsz.x * .5f, ctr.y - tsz.y * .5f), IM_COL32(10, 3, 28, 255), c.c_str());
        }
    }
    // bottom block
    float bottomH = 150 * s;
    ImGui::SetCursorScreenPos(ImVec2(p.x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - bottomH - ImGui::GetStyle().WindowPadding.y));
    heng::ApiProfile* ap = cfg_.active();
    ImVec2 c0 = ImGui::GetCursorScreenPos(); ImVec2 c1(c0.x + nw, c0.y + 72 * s);
    dl->AddRectFilled(c0, c1, IM_COL32(10, 3, 28, 200), 12 * s);
    dl->AddRect(c0, c1, ui::col::withAlpha(ui::col::purple, 0.6f), 12 * s);
    bool ready = ap && (!ap->apiKey.empty() || ap->provider == heng::Provider::Custom);
    ImGui::SetCursorScreenPos(ImVec2(c0.x + 10 * s, c0.y + 8 * s));
    ui::statusDot(ready ? ui::col::green : ui::col::yellow, ai_.busy());
    ImGui::SameLine(0, 4);
    ImGui::PushFont(ui::fonts.bold); ImGui::TextUnformatted(ap ? ap->name.c_str() : "No profile"); ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(c0.x + 12 * s, c0.y + 34 * s));
    ImGui::PushFont(ui::fonts.small);
    std::string sub = ap ? std::string(heng::providerName(ap->provider)) + " · " + heng::tierName(ap->tier) : "";
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::muted), "%s", sub.c_str());
    ImGui::SetCursorScreenPos(ImVec2(c0.x + 12 * s, c0.y + 50 * s));
    std::string model = ap ? ap->model : "";
    if (ImGui::CalcTextSize(model.c_str()).x > nw - 24 * s) model = model.substr(0, 20) + "...";
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::cyan), "%s", model.c_str());
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(c0.x, c1.y + 10 * s));
    if (ui::button("Settings", ImVec2(nw, 0), ui::col::pink, false, Icon::Settings)) { settingsOpen_ = true; settingsOpenedAt_ = ImGui::GetTime(); }
    if (ui::navItem("About", Icon::About, page_ == Page::About, nw)) page_ = Page::About;
    ui::endPanel();
}

void App::drawTopBar(float w) {
    float s = ImGui::GetFontSize() / 17.f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ui::gradientText(pageTitle(page_), ui::col::text, ui::col::pink, ui::fonts.title);
    if (pageSub(page_)[0]) { ImGui::PushFont(ui::fonts.small); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::muted), "%s", pageSub(page_)); ImGui::PopFont(); }
    // right side: AI status
    float busy = ui::anim(ImGui::GetID("busyAnim"), ai_.busy() ? 1.f : 0.f, 8.f);
    if (busy > 0.01f) {
        const char* label = ai_.state() == SessionState::RunningTools ? "running tools" : "thinking";
        char buf[64]; std::snprintf(buf, sizeof buf, "%s  ·  step %d", label, ai_.step());
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImVec2 q(p.x + w - ts.x - 40 * s, p.y + 4 * s);
        ImGui::GetWindowDrawList()->AddText(q, ui::col::withAlpha(ui::col::cyan, busy), buf);
        ImGui::SetCursorScreenPos(ImVec2(q.x + ts.x + 6, q.y - 2)); ui::statusDot(ui::col::cyan, true);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 66 * s));
}

// ----------------------------------------------------------------- settings window
void App::newProfile(heng::Provider pv) {
    heng::ApiProfile p; p.provider = pv;
    switch (pv) {
        case heng::Provider::OpenAI: p.name = "OpenAI"; p.model = "gpt-5-mini"; break;
        case heng::Provider::OpenRouter: p.name = "OpenRouter"; p.model = "openrouter/free"; break;
        case heng::Provider::Anthropic: p.name = "Claude"; p.model = "claude-haiku-5-5"; break;
        case heng::Provider::Custom: p.name = "Local / custom"; p.model = "llama3"; p.baseUrl = "http://localhost:11434/v1"; break;
    }
    std::string base = p.name; int n = 2;
    auto taken = [&](const std::string& nm) { for (auto& x : cfg_.profiles) if (x.name == nm) return true; return false; };
    while (taken(p.name)) p.name = base + " " + std::to_string(n++);
    cfg_.profiles.push_back(p); selProfile_ = (int)cfg_.profiles.size() - 1; markDirty();
}

void App::testConnection() {
    heng::ApiProfile* p = (selProfile_ >= 0 && selProfile_ < (int)cfg_.profiles.size()) ? &cfg_.profiles[selProfile_] : nullptr;
    if (!p || test_.state.load() == 1) return;
    if (test_.th.joinable()) test_.th.join();
    test_.state = 1;
    heng::ApiProfile copy = *p;
    test_.th = std::thread([this, copy]() {
        std::vector<heng::ChatMessage> h(1); h[0].role = "user"; h[0].text = "Reply with exactly: OK";
        heng::ApiRequest rq = heng::buildChatRequest(copy.toConfig(), "You are a connectivity test. Be extremely brief.", h, 64);
        platform::HttpResult r = platform::httpPost(rq, 60, nullptr);
        std::string msg; int st = 3;
        if (!r.sent) msg = r.error;
        else {
            heng::ApiResult ar = heng::parseResponse(copy.provider, r.status, r.body);
            if (ar.ok) { st = 2; msg = "Connected. Model replied: " + (ar.text.size() > 40 ? ar.text.substr(0, 40) : ar.text); }
            else msg = ar.error;
        }
        { std::lock_guard<std::mutex> lk(test_.mu); test_.msg = msg; }
        test_.state = st;
    });
}

void App::drawSettingsWindow(ImVec2 display) {
    float fs = ImGui::GetFontSize(), s = fs / 17.f;
    float open = settingsAnim_;
    if (open < 0.01f && !settingsOpen_) return;
    ImVec2 sz(std::min(display.x - 40 * s, 980 * s), std::min(display.y - 40 * s, 640 * s));
    ImGui::SetNextWindowSize(sz, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(display.x * .5f, display.y * .5f + (1.f - open) * 30 * s), ImGuiCond_Always, ImVec2(.5f, .5f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(0.01f, open));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.02f, 0.17f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(ui::col::pink));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowFocus();
    if (ImGui::Begin("Settings##win", nullptr, fl)) {
        ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 wp = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
        for (int g = 3; g >= 1; --g) dl->AddRect(ImVec2(wp.x - g * 2, wp.y - g * 2), ImVec2(wp.x + wsz.x + g * 2, wp.y + wsz.y + g * 2), ui::col::withAlpha(ui::col::pink, 0.06f * (4 - g) * open), 14 * s + g * 2, 0, 2.f);
        // header
        ui::drawIcon(dl, Icon::Settings, ImVec2(wp.x + 34 * s, wp.y + 32 * s), 26 * s, ui::col::pink);
        ImGui::SetCursorPos(ImVec2(58 * s, 14 * s)); ui::gradientText("Settings", ui::col::text, ui::col::pink, ui::fonts.title);
        ImGui::SetCursorPos(ImVec2(wsz.x - 44 * s, 16 * s));
        if (ui::iconButton("##closeSettings", Icon::Plus, ui::col::pink, 30 * s, "Close (Esc)")) settingsOpen_ = false;
        // draw the X by rotating the plus: overdraw
        { ImVec2 c(wp.x + wsz.x - 44 * s + 15 * s, wp.y + 16 * s + 15 * s); dl->AddRectFilled(ImVec2(c.x - 8 * s, c.y - 8 * s), ImVec2(c.x + 8 * s, c.y + 8 * s), IM_COL32(28, 10, 66, 255), 4.f);
          dl->AddLine(ImVec2(c.x - 6 * s, c.y - 6 * s), ImVec2(c.x + 6 * s, c.y + 6 * s), ui::col::text, 2.f); dl->AddLine(ImVec2(c.x - 6 * s, c.y + 6 * s), ImVec2(c.x + 6 * s, c.y - 6 * s), ui::col::text, 2.f); }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) settingsOpen_ = false;
        // tabs (left rail)
        float railW = 190 * s;
        ImGui::SetCursorPos(ImVec2(16 * s, 62 * s));
        ImGui::BeginChild("##rail", ImVec2(railW, wsz.y - 80 * s), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        const char* tabs[] = {"API profiles", "AI personality", "Behavior", "Appearance", "Storage & safety"};
        Icon ics[] = {Icon::Key, Icon::Bolt, Icon::Plan, Icon::Eye, Icon::Folder};
        for (int i = 0; i < 5; ++i) if (ui::navItem(tabs[i], ics[i], settingsTab_ == i, railW - 6)) settingsTab_ = i;
        ImGui::EndChild();
        ImGui::SetCursorPos(ImVec2(16 * s + railW + 14 * s, 62 * s));
        ImGui::BeginChild("##settingsBody", ImVec2(wsz.x - railW - 46 * s, wsz.y - 80 * s), ImGuiChildFlags_None);
        switch (settingsTab_) {
            case 0: settingsProfiles(); break; case 1: settingsPersonality(); break; case 2: settingsBehavior(); break;
            case 3: settingsAppearance(); break; default: settingsStorage(); break;
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
}

static void keyHint(heng::Provider p) {
    const char* url = p == heng::Provider::OpenAI ? "platform.openai.com/api-keys" : p == heng::Provider::OpenRouter ? "openrouter.ai/keys" : p == heng::Provider::Anthropic ? "console.anthropic.com/settings/keys" : "";
    if (url[0]) { ImGui::PushFont(ui::fonts.small); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::muted), "Get a key: %s", url); ImGui::PopFont(); }
}

void App::settingsProfiles() {
    float fs = ImGui::GetFontSize(), s = fs / 17.f;
    float listW = 250 * s;
    ImGui::BeginChild("##plist", ImVec2(listW, 0), ImGuiChildFlags_None);
    ui::sectionLabel("Saved profiles", ui::col::cyan);
    for (int i = 0; i < (int)cfg_.profiles.size(); ++i) {
        heng::ApiProfile& p = cfg_.profiles[i];
        ImGui::PushID(i);
        ImVec2 q = ImGui::GetCursorScreenPos(); float rowH = fs * 2.7f;
        bool sel = selProfile_ == i;
        if (ImGui::Selectable("##row", sel, 0, ImVec2(listW - 12, rowH))) selProfile_ = i;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool active = cfg_.activeProfile == i;
        dl->AddText(ui::fonts.bold, ui::fonts.bold->FontSize, ImVec2(q.x + 10, q.y + 5 * s), ui::col::text, p.name.c_str());
        std::string sub = std::string(heng::providerName(p.provider)) + " · " + p.model;
        dl->PushClipRect(ImVec2(q.x, q.y), ImVec2(q.x + listW - 60 * s, q.y + rowH), true);
        dl->AddText(ui::fonts.small, ui::fonts.small->FontSize, ImVec2(q.x + 10, q.y + rowH - fs * 1.2f), ui::col::muted, sub.c_str());
        dl->PopClipRect();
        bool hasKey = !p.apiKey.empty();
        dl->AddCircleFilled(ImVec2(q.x + listW - 28 * s, q.y + rowH * .5f), 5 * s, hasKey ? ui::col::green : ui::col::yellow);
        if (active) { dl->AddRect(ImVec2(q.x, q.y), ImVec2(q.x + listW - 12, q.y + rowH), ui::col::withAlpha(ui::col::cyan, 0.8f), 8.f, 0, 1.5f);
                      dl->AddText(ui::fonts.small, ui::fonts.small->FontSize, ImVec2(q.x + listW - 74 * s, q.y + 4 * s), ui::col::cyan, "ACTIVE"); }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6));
    if (ui::button("New profile", ImVec2(listW - 12, 0), ui::col::pink, true, Icon::Plus)) ImGui::OpenPopup("##newprofile");
    if (ImGui::BeginPopup("##newprofile")) {
        const heng::Provider provs[] = {heng::Provider::Anthropic, heng::Provider::OpenAI, heng::Provider::OpenRouter, heng::Provider::Custom};
        const char* names[] = {"Anthropic (Claude)", "OpenAI", "OpenRouter", "Custom / local (OpenAI-compatible)"};
        for (int i = 0; i < 4; ++i) if (ImGui::Selectable(names[i])) newProfile(provs[i]);
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##pform", ImVec2(0, 0), ImGuiChildFlags_None);
    if (selProfile_ < 0 || selProfile_ >= (int)cfg_.profiles.size()) { ImGui::TextDisabled("No profile selected."); ImGui::EndChild(); return; }
    heng::ApiProfile& p = cfg_.profiles[selProfile_];
    ui::sectionLabel("Profile", ui::col::pink);
    ImGui::TextDisabled("Name"); if (ui::inputText("##pname", &p.name)) markDirty();
    ImGui::TextDisabled("Provider");
    {
        const char* items[] = {"Anthropic", "OpenAI", "OpenRouter", "Custom"};
        int cur = p.provider == heng::Provider::Anthropic ? 0 : p.provider == heng::Provider::OpenAI ? 1 : p.provider == heng::Provider::OpenRouter ? 2 : 3;
        if (ui::segmented("##prov", items, 4, &cur, ImGui::GetContentRegionAvail().x)) {
            p.provider = cur == 0 ? heng::Provider::Anthropic : cur == 1 ? heng::Provider::OpenAI : cur == 2 ? heng::Provider::OpenRouter : heng::Provider::Custom;
            auto sug = heng::suggestedModels(p.provider, p.tier); if (!sug.empty()) p.model = sug[0];
            markDirty();
        }
    }
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Tier (only changes which models are suggested)");
    { const char* items[] = {"Free", "Pro"}; int cur = p.tier == heng::Tier::Pro; if (ui::segmented("##tier", items, 2, &cur, 200 * s)) { p.tier = cur ? heng::Tier::Pro : heng::Tier::Free; auto sug = heng::suggestedModels(p.provider, p.tier); if (!sug.empty()) p.model = sug[0]; markDirty(); } }
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Model (type any model name)");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150 * s);
    if (ImGui::InputText("##model", &p.model)) markDirty();
    ImGui::SameLine();
    if (ui::button("Suggestions", ImVec2(140 * s, 0), ui::col::purple)) ImGui::OpenPopup("##sug");
    if (ImGui::BeginPopup("##sug")) {
        auto sug = heng::suggestedModels(p.provider, p.tier);
        auto sug2 = heng::suggestedModels(p.provider, p.tier == heng::Tier::Pro ? heng::Tier::Free : heng::Tier::Pro);
        sug.insert(sug.end(), sug2.begin(), sug2.end());
        if (sug.empty()) ImGui::TextDisabled("No suggestions for custom endpoints.");
        for (auto& m : sug) if (ImGui::Selectable(m.c_str())) { p.model = m; markDirty(); }
        ImGui::EndPopup();
    }
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("API key"); keyHint(p.provider);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50 * s);
    if (ImGui::InputText("##key", &p.apiKey, showKey_ ? 0 : ImGuiInputTextFlags_Password)) markDirty();
    ImGui::SameLine(); if (ui::iconButton("##showkey", Icon::Eye, showKey_ ? ui::col::cyan : ui::col::muted, 38 * s, showKey_ ? "Hide key" : "Show key")) showKey_ = !showKey_;
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled(p.provider == heng::Provider::Custom ? "Base URL (required)" : "Base URL (optional: proxy / compatible server)");
    if (ui::inputText("##base", &p.baseUrl, p.provider == heng::Provider::Custom ? "http://localhost:11434/v1" : "leave empty for the official API")) markDirty();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Max reply tokens"); ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderInt("##mt", &p.maxTokens, 1024, 32000)) markDirty();
    ImGui::Dummy(ImVec2(0, 8));
    bool isActive = cfg_.activeProfile == selProfile_;
    ImGui::BeginDisabled(isActive);
    if (ui::button(isActive ? "In use" : "Use this profile", ImVec2(0, 0), ui::col::cyan, !isActive, Icon::Check)) { cfg_.activeProfile = selProfile_; markDirty(); ui::toast(Toast::Success, "Now using \"" + p.name + "\""); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    int ts = test_.state.load();
    if (ui::button(ts == 1 ? "Testing..." : "Test connection", ImVec2(0, 0), ui::col::yellow, false, Icon::Bolt)) testConnection();
    ImGui::SameLine();
    if (ui::button("Duplicate", ImVec2(0, 0), ui::col::purple, false, Icon::Copy)) { heng::ApiProfile c = p; c.name += " copy"; cfg_.profiles.push_back(c); selProfile_ = (int)cfg_.profiles.size() - 1; markDirty(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(cfg_.profiles.size() <= 1);
    if (ui::button("Delete", ImVec2(0, 0), ui::col::red, false, Icon::Trash)) {
        cfg_.profiles.erase(cfg_.profiles.begin() + selProfile_);
        if (cfg_.activeProfile >= (int)cfg_.profiles.size()) cfg_.activeProfile = 0;
        if (cfg_.activeProfile > selProfile_) --cfg_.activeProfile;
        selProfile_ = std::min(selProfile_, (int)cfg_.profiles.size() - 1); markDirty();
    }
    ImGui::EndDisabled();
    if (ts == 1) ui::shimmerBar(0, 6 * s);
    else if (ts >= 2) { std::string m; { std::lock_guard<std::mutex> lk(test_.mu); m = test_.msg; } ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ts == 2 ? ui::col::green : ui::col::red)); ImGui::TextWrapped("%s", m.c_str()); ImGui::PopStyleColor(); }
    ImGui::EndChild();
}

void App::settingsPersonality() {
    if (!personalityLoaded_) { personalityBuf_ = cfg_.systemPrompt.empty() ? std::string(heng::defaultPersonality()) : cfg_.systemPrompt; personalityLoaded_ = true; }
    ui::sectionLabel("AI personality (system prompt)", ui::col::pink);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("This is how Happy behaves and talks. Edit it freely. The tool + plan protocol is added automatically and cannot be removed, so the app keeps working whatever you write.");
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 4));
    float h = ImGui::GetContentRegionAvail().y - ImGui::GetFontSize() * 3.2f;
    if (ImGui::InputTextMultiline("##personality", &personalityBuf_, ImVec2(-FLT_MIN, h))) {
        cfg_.systemPrompt = (personalityBuf_ == heng::defaultPersonality()) ? std::string() : personalityBuf_; markDirty();
    }
    if (ui::button("Reset to default", ImVec2(0, 0), ui::col::yellow, false, Icon::Refresh)) { personalityBuf_ = heng::defaultPersonality(); cfg_.systemPrompt.clear(); markDirty(); }
    ImGui::SameLine(); ui::badge(cfg_.systemPrompt.empty() ? "default" : "custom", cfg_.systemPrompt.empty() ? ui::col::cyan : ui::col::pink);
}

void App::settingsBehavior() {
    ui::sectionLabel("Agent & safety", ui::col::pink);
    ImGui::TextDisabled("Max tool steps per request"); ImGui::SetNextItemWidth(300 * ImGui::GetFontSize() / 17.f);
    if (ImGui::SliderInt("##steps", &cfg_.agentMaxSteps, 1, 40)) markDirty();
    ui::helpMarker("How many times the AI may call read-only tools (list/read/search files) before it has to answer.");
    ImGui::Dummy(ImVec2(0, 6));
    if (ImGui::Checkbox("Back up originals when installing into the game (.hebak)", &cfg_.backupOnInstall)) markDirty();
    if (ImGui::Checkbox("Always ask before applying", &cfg_.confirmBeforeApply)) markDirty();
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Nothing is written to your game without you pressing Apply, and Apply only writes to the Output folder. Installing into the game is a separate step.");
    ImGui::PopTextWrapPos();
}

void App::settingsAppearance() {
    float s = ImGui::GetFontSize() / 17.f;
    ui::sectionLabel("Look & feel", ui::col::pink);
    if (ImGui::Checkbox("Animations and smooth transitions", &cfg_.animations)) { ui::setAnimations(cfg_.animations); markDirty(); }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextDisabled("Interface scale"); ImGui::SetNextItemWidth(320 * s);
    float v = cfg_.uiScale;
    if (ImGui::SliderFloat("##scale", &v, 0.75f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp)) { cfg_.uiScale = v; markDirty(); }
    ImGui::SameLine(); if (ui::button("Reset", ImVec2(0, 0), ui::col::purple)) { cfg_.uiScale = 1.f; markDirty(); }
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::PushTextWrapPos(0); ImGui::TextDisabled("Scale is also adjusted automatically for high-DPI screens."); ImGui::PopTextWrapPos();
}

void App::settingsStorage() {
    ui::sectionLabel("Where things are stored", ui::col::pink);
    ImGui::TextDisabled("Config file"); ImGui::TextWrapped("%s", cfgPath_.c_str());
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextDisabled("API key protection"); ImGui::TextWrapped("%s", platform::secretProtectionName());
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextDisabled("Output folder (modified files are written here)");
    std::string shown = cfg_.outputDir.empty() ? outputBase() + "  (default)" : cfg_.outputDir;
    ImGui::TextWrapped("%s", shown.c_str());
    if (ui::button("Choose...", ImVec2(0, 0), ui::col::cyan, false, Icon::Folder)) browser_.open("Choose the output folder", FileBrowser::Mode::OpenFolder, outputBase(), 4);
    ImGui::SameLine(); if (ui::button("Use default", ImVec2(0, 0), ui::col::purple)) { cfg_.outputDir.clear(); markDirty(); }
    ImGui::SameLine(); if (ui::button("Open", ImVec2(0, 0), ui::col::yellow)) platform::openInFileManager(outputBase());
    ImGui::Dummy(ImVec2(0, 12));
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("What is sent to the AI provider: the info about your target (file list, small text samples, strings), your attachments, the memory watch list and your messages. Choose a target you are fine sending to an external provider.");
    ImGui::PopTextWrapPos();
}

} // namespace he
