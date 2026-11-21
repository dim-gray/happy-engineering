#include "app.h"
#include "theme.h"
#include "app/fsutil.h"
#include "core/context.h"
#include "core/base64.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace he {
using namespace ui;

// ----------------------------------------------------------------- target handling
void App::clearTarget() {
    target_ = heng::Target(); pathBuf_.clear(); targetSummary_.clear(); ws_.setTarget(target_); pickedPid_ = 0; pickedName_.clear();
}

void App::setTargetPath(const std::string& pathIn) {
    std::error_code ec; fs::path p = P(pathIn);
    heng::Target t;
    if (fs::is_directory(p, ec)) { t.type = heng::TargetType::Folder; }
    else if (fs::is_regular_file(p, ec)) {
        std::string ext = heng::extensionOf(pathIn);
        t.type = (ext == "html" || ext == "htm") ? heng::TargetType::Html : heng::TargetType::File;
    } else { ui::toast(Toast::Error, "That path does not exist."); return; }
    t.path = S(p);
    bool changed = target_.path != t.path || target_.type != t.type;
    target_ = t; pathBuf_ = t.path;
    targetKind_ = t.type == heng::TargetType::Folder ? 1 : t.type == heng::TargetType::Html ? 2 : 0;
    ws_.setTarget(t);
    if (changed && ai_.hasHistory()) { ai_.reset(); plan_ = heng::Plan(); selected_.clear(); hasReport_ = false; ui::toast(Toast::Info, "New target: conversation reset."); }
    // summary
    std::ostringstream o;
    if (t.type == heng::TargetType::Folder) {
        bool tr = false; auto files = ws_.scanFolder(20000, tr);
        uint64_t total = 0; for (auto& f : files) total += f.size;
        o << files.size() << (tr ? "+" : "") << " files · " << heng::humanSize(total);
    } else {
        std::vector<uint8_t> b; std::string e;
        if (readWholeFile(t.path, b, e, 64u * 1024 * 1024)) {
            o << heng::humanSize(b.size());
            heng::PeInfo pe = heng::parsePe(b.data(), b.size());
            if (pe.valid) o << " · PE " << (pe.is64 ? "x64" : "x86") << " · " << pe.imports.size() << " imports";
        }
    }
    targetSummary_ = o.str();
    cfg_.lastTargetPath = t.path; markDirty();
}

void App::setTargetProcess(const ProcPick& pp) {
    heng::Target t; t.type = heng::TargetType::Process; t.pid = pp.pid; t.processName = pp.name;
    target_ = t; targetKind_ = 3; pickedPid_ = pp.pid; pickedName_ = pp.name;
    std::string mainPath; std::vector<heng::ModuleSummary> mods;
    platform::processModules(pp.pid, mainPath, mods);
    heng::Target folder;
    if (!mainPath.empty()) {
        folder.type = heng::TargetType::Folder; folder.path = S(P(mainPath).parent_path());
        ws_.setTarget(folder);   // the game's folder becomes the editable file root
        pathBuf_ = folder.path;
    }
    targetSummary_ = std::string("PID ") + std::to_string(pp.pid) + (mainPath.empty() ? " (modules not readable)" : " · " + mainPath);
}

void App::onDrop(const std::vector<std::string>& paths) {
    for (auto& p : paths) {
        std::error_code ec;
        if (fs::is_directory(P(p), ec)) { page_ = Page::Workspace; setTargetPath(p); continue; }
        if (!target_.isSet()) { page_ = Page::Workspace; setTargetPath(p); continue; }
        std::string e;
        if (ws_.addAttachment(p, e)) ui::toast(Toast::Success, "Attached " + heng::baseNameOf(p)); else ui::toast(Toast::Error, e);
    }
}

bool App::aiReady(std::string& why) const {
    const heng::ApiProfile* ap = const_cast<heng::AppConfig&>(cfg_).active();
    if (!ap) { why = "Create an API profile in Settings first."; return false; }
    if (ap->provider != heng::Provider::Custom && ap->apiKey.empty()) { why = "Add your API key in Settings > API profiles."; return false; }
    if (ap->model.empty()) { why = "Choose a model in Settings."; return false; }
    if (!target_.isSet()) { why = "Pick a target first (file, folder, HTML or running process)."; return false; }
    return true;
}

// ----------------------------------------------------------------- context for the first message
std::string App::buildContext(std::vector<heng::ImagePart>& images) {
    std::ostringstream o;
    std::string err;
    switch (target_.type) {
        case heng::TargetType::File: {
            std::vector<uint8_t> b;
            if (readWholeFile(target_.path, b, err, 64u * 1024 * 1024)) o << heng::buildFileContext(heng::baseNameOf(target_.path), b) << "\n";
            else o << "(could not read the target file: " << err << ")\n";
            o << "Editable root folder: " << ws_.root() << "\nTarget file (relative): " << ws_.focusFile() << "\n";
            break; }
        case heng::TargetType::Html: {
            std::vector<uint8_t> b;
            if (readWholeFile(target_.path, b, err, 16u * 1024 * 1024)) o << heng::buildHtmlContext(heng::baseNameOf(target_.path), heng::decodeTextBytes(b.data(), b.size())) << "\n";
            o << "Target file (relative): " << ws_.focusFile() << "\n";
            break; }
        case heng::TargetType::Folder: {
            bool trunc = false; auto files = ws_.scanFolder(5000, trunc);
            std::vector<heng::TextSnippet> snips;
            for (auto& rel : heng::selectSnippetFiles(files)) {
                std::vector<uint8_t> b;
                if (!ws_.readTarget(rel, b, err)) continue;
                heng::TextSnippet sn; sn.rel = rel; std::string t = heng::decodeTextBytes(b.data(), b.size());
                sn.content = heng::cutUtf8(t, 8000); sn.truncated = sn.content.size() < t.size(); snips.push_back(sn);
            }
            o << heng::buildFolderContext(ws_.root(), files, trunc, snips) << "\n";
            o << "Tip: archives (.zip .jar .apk .pk3 ...) in the folder can be browsed with list_files path \"name.zip!\".\n";
            break; }
        case heng::TargetType::Process: {
            std::string mainPath; std::vector<heng::ModuleSummary> mods; platform::processModules(target_.pid, mainPath, mods);
            std::string pe;
            if (!mainPath.empty()) { std::vector<uint8_t> b; if (readWholeFile(mainPath, b, err, 64u * 1024 * 1024)) pe = heng::buildFileContext(heng::baseNameOf(mainPath), b); }
            o << heng::buildProcessContext(target_.pid, target_.processName, mainPath, mods, pe) << "\n";
            o << "Editable root folder: " << ws_.root() << "\n";
            break; }
        default: break;
    }
    // attachments
    std::vector<heng::AttachmentContextItem> items; int imgBytes = 0;
    for (auto& a : ws_.attachments()) {
        std::vector<uint8_t> b; if (!readWholeFile(a.path, b, err, 64u * 1024 * 1024)) continue;
        heng::Attachment at; at.name = a.name; at.path = a.path; at.size = a.size; at.kind = a.kind;
        heng::AttachmentContextItem it; it.description = heng::describeAttachment(at, b);
        if (a.kind == heng::AttachmentKind::Text) it.textContent = heng::cutUtf8(heng::decodeTextBytes(b.data(), b.size()), 12000);
        if (a.kind == heng::AttachmentKind::ImageVision && b.size() <= 4u * 1024 * 1024 && imgBytes + (int)b.size() <= 12 * 1024 * 1024 && images.size() < 6) {
            heng::ImagePart ip; ip.label = a.name; ip.mime = heng::imageMimeFor(a.name); ip.base64 = heng::base64Encode(b); images.push_back(ip); imgBytes += (int)b.size(); it.sentAsImage = true;
        }
        items.push_back(it);
    }
    o << heng::buildAttachmentsContext(items);
    std::vector<heng::WatchEntry> w = watch_; o << heng::buildWatchContext(w);
    return o.str();
}

void App::sendToAi(const std::string& text) {
    std::string why; if (!aiReady(why)) { ui::toast(Toast::Warn, why); return; }
    heng::ApiProfile* ap = cfg_.active();
    SessionSettings ss; ss.api = ap->toConfig(); ss.maxTokens = ap->maxTokens; ss.maxSteps = cfg_.agentMaxSteps; ss.personality = cfg_.systemPrompt;
    std::vector<heng::ImagePart> images; std::string ctx;
    if (!ai_.hasHistory()) ctx = buildContext(images);
    else {   // follow-up: refresh only attachments/watch list changes cheaply
        for (auto& a : ws_.attachments()) { (void)a; }
    }
    ai_.send(ss, &ws_, text, ctx, images);
    feedStick_ = true;
}

// ----------------------------------------------------------------- process picker
void App::drawProcessPicker(float h, bool forMemory) {
    float s = ImGui::GetFontSize() / 17.f;
    if (ImGui::GetTime() - procRefreshAt_ > 5.0 || procs_.empty()) { procs_ = platform::listProcesses(); procRefreshAt_ = ImGui::GetTime(); std::sort(procs_.begin(), procs_.end(), [](const platform::ProcessInfo& a, const platform::ProcessInfo& b) { return heng::lowerCopy(a.name) < heng::lowerCopy(b.name); }); }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 46 * s);
    ImGui::InputTextWithHint("##pf", "filter processes...", &procFilter_);
    ImGui::SameLine(); if (ui::iconButton("##rp", Icon::Refresh, ui::col::cyan, 36 * s, "Refresh list")) { procRefreshAt_ = -100; }
    ImGui::BeginChild("##proclist", ImVec2(0, h), ImGuiChildFlags_None);
    std::string f = heng::lowerCopy(procFilter_);
    uint32_t cur = forMemory ? memPid_ : pickedPid_;
    for (auto& p : procs_) {
        if (!f.empty() && heng::lowerCopy(p.name).find(f) == std::string::npos && std::to_string(p.pid).find(f) == std::string::npos) continue;
        char buf[256]; std::snprintf(buf, sizeof buf, "%s   [%u]", p.name.c_str(), p.pid);
        ImGui::PushID((int)p.pid);
        if (ImGui::Selectable(buf, cur == p.pid)) { ProcPick pp; pp.pid = p.pid; pp.name = p.name; if (forMemory) attachMemory(pp); else setTargetProcess(pp); }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// ----------------------------------------------------------------- panels
void App::drawTargetPanel(float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##target", ImVec2(0, h), "Target", ui::col::cyan, true);
    const char* kinds[] = {"File", "Folder", "HTML", "Process"};
    int k = targetKind_;
    if (ui::segmented("##kind", kinds, 4, &k, ImGui::GetContentRegionAvail().x)) targetKind_ = k;
    ImGui::Dummy(ImVec2(0, 2));
    if (targetKind_ == 3) {
        drawProcessPicker(std::max(120.f * s, ImGui::GetContentRegionAvail().y - 80 * s), false);
    } else {
        ImGui::TextDisabled(targetKind_ == 1 ? "Game / app folder" : targetKind_ == 2 ? "HTML file" : "Program file (EXE, DLL, any binary)");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120 * s);
        bool enter = ImGui::InputTextWithHint("##tp", "path or drop it onto the window", &pathBuf_, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ui::button("Browse", ImVec2(110 * s, 0), ui::col::cyan, false, Icon::Folder)) {
            std::string start = pathBuf_.empty() ? cfg_.lastTargetPath : pathBuf_;
            if (!start.empty()) { std::error_code ec; fs::path sp = P(start); if (!fs::is_directory(sp, ec)) start = S(sp.parent_path()); }
            if (targetKind_ == 1) browser_.open("Choose the game folder", FileBrowser::Mode::OpenFolder, start, 2);
            else browser_.open(targetKind_ == 2 ? "Choose an HTML file" : "Choose a program file", FileBrowser::Mode::OpenFile, start, targetKind_ == 2 ? 5 : 1);
        }
        if (enter && !pathBuf_.empty()) setTargetPath(pathBuf_);
    }
    ImGui::Dummy(ImVec2(0, 4));
    if (target_.isSet()) {
        ImGui::BeginGroup();
        const char* tn = target_.type == heng::TargetType::Folder ? "FOLDER" : target_.type == heng::TargetType::Html ? "HTML" : target_.type == heng::TargetType::Process ? "PROCESS" : "FILE";
        ui::badge(tn, ui::col::green); ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::muted), "%s", targetSummary_.c_str());
        ImGui::EndGroup();
        ImGui::PushFont(ui::fonts.small); ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::text), "%s", target_.type == heng::TargetType::Process ? target_.processName.c_str() : target_.path.c_str());
        ImGui::PopTextWrapPos(); ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 2));
        if (ui::button("Clear target", ImVec2(0, 0), ui::col::red, false, Icon::Trash)) { clearTarget(); }
    } else {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("No target yet. Drop a game folder or file onto this window, or use Browse.");
        ImGui::PopTextWrapPos();
    }
    ui::endPanel();
}

void App::drawAttachmentsPanel(float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##att", ImVec2(0, h), "Attachments", ui::col::yellow, true);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Images, audio, text, anything the AI should know about or use as a replacement. Images are shown to the AI.");
    ImGui::PopTextWrapPos();
    if (ui::button("Add files", ImVec2(0, 0), ui::col::yellow, false, Icon::Plus)) browser_.open("Attach a file", FileBrowser::Mode::OpenFile, cfg_.lastTargetPath.empty() ? platform::documentsDir() : cfg_.lastTargetPath, 3);
    int rm = -1;
    const auto& att = ws_.attachments();
    for (size_t i = 0; i < att.size(); ++i) {
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos(); float rowH = ImGui::GetFontSize() * 2.2f; float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH), IM_COL32(10, 3, 28, 150), 9.f);
        Icon ic = att[i].kind == heng::AttachmentKind::ImageVision || att[i].kind == heng::AttachmentKind::ImageOther ? Icon::Image : Icon::File;
        ui::drawIcon(dl, ic, ImVec2(p.x + rowH * .5f, p.y + rowH * .5f), rowH * .5f, ui::col::yellow);
        dl->AddText(ImVec2(p.x + rowH, p.y + rowH * .5f - ImGui::GetFontSize() * .62f), ui::col::text, att[i].name.c_str());
        std::string meta = std::string(heng::kindName(att[i].kind)) + " · " + heng::humanSize(att[i].size);
        dl->AddText(ui::fonts.small, ui::fonts.small->FontSize, ImVec2(p.x + rowH, p.y + rowH * .5f + 1), ui::col::muted, meta.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - rowH, p.y + 3));
        if (ui::iconButton("##rm", Icon::Trash, ui::col::red, rowH - 6, "Remove")) rm = (int)i;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH + 4));
        ImGui::PopID();
    }
    if (rm >= 0) ws_.removeAttachment((size_t)rm);
    if (att.empty()) { ImGui::Dummy(ImVec2(0, 4)); ImGui::TextDisabled("Nothing attached."); }
    ui::endPanel();
}

// ----------------------------------------------------------------- feed
static void renderRich(const std::string& text, float wrapW) {
    // split on ``` fences: code in mono on a darker plate
    size_t pos = 0; bool code = false;
    while (pos <= text.size()) {
        size_t f = text.find("```", pos);
        std::string seg = text.substr(pos, f == std::string::npos ? std::string::npos : f - pos);
        if (!seg.empty()) {
            if (code) {
                size_t nl = seg.find('\n'); std::string body = (nl == std::string::npos) ? seg : seg.substr(nl + 1);
                while (!body.empty() && (body.back() == '\n' || body.back() == ' ')) body.pop_back();
                ImVec2 p = ImGui::GetCursorScreenPos(); ImGui::PushFont(ui::fonts.mono);
                ImVec2 ts = ImGui::CalcTextSize(body.c_str(), nullptr, false, wrapW - 20);
                ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + wrapW, p.y + ts.y + 14), IM_COL32(6, 1, 20, 230), 8.f);
                ImGui::GetWindowDrawList()->AddRect(p, ImVec2(p.x + wrapW, p.y + ts.y + 14), ui::col::withAlpha(ui::col::purple, 0.5f), 8.f);
                ImGui::SetCursorScreenPos(ImVec2(p.x + 10, p.y + 7));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::cyan));
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrapW - 20); ImGui::TextUnformatted(body.c_str()); ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::PopFont();
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ts.y + 20));
            } else {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrapW); ImGui::TextUnformatted(seg.c_str()); ImGui::PopTextWrapPos();
            }
        }
        if (f == std::string::npos) break;
        pos = f + 3; code = !code;
    }
}

void App::drawFeed(float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##feed", ImVec2(0, h), "Conversation", ui::col::pink, true);
    std::vector<LogItem> log = ai_.log();
    if (log.empty()) {
        ImGui::Dummy(ImVec2(0, 20 * s));
        ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
        const char* big = ":)"; ImGui::PushFont(ui::fonts.logo); ImVec2 ts = ImGui::CalcTextSize(big);
        float bob = std::sin((float)ImGui::GetTime() * 1.6f) * 4.f;
        ui::glowText(big, ui::col::yellow, ImVec2(p.x + (w - ts.x) * .5f, p.y + bob), ui::fonts.logo);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, ts.y + 12 * s));
        const char* l1 = "Hi! Pick a target, then tell me what to change.";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - ImGui::CalcTextSize(l1).x) * .5f); ImGui::TextUnformatted(l1);
        const char* l2 = "I can explore files, archives and strings, then propose a plan you approve.";
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - ImGui::CalcTextSize(l2).x) * .5f); ImGui::TextDisabled("%s", l2);
    }
    float wrapW = ImGui::GetContentRegionAvail().x - 24 * s;
    for (size_t i = 0; i < log.size(); ++i) {
        const LogItem& it = log[i];
        ImGui::PushID((int)i);
        ImU32 accent = it.kind == LogKind::User ? ui::col::pink : it.kind == LogKind::Assistant ? ui::col::cyan : it.kind == LogKind::Error ? ui::col::red : it.kind == LogKind::Info ? ui::col::yellow : ui::col::purple;
        while (feedBorn_.size() < log.size()) feedBorn_.push_back(ui::time());
        if (feedBorn_.size() > log.size()) feedBorn_.resize(log.size());
        float born = std::min(1.f, (float)(ui::time() - feedBorn_[i]) / 0.3f); if (!ui::animationsOn()) born = 1.f;
        float slide = (1.f - born) * 14.f * s;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(0.05f, born));
        ImVec2 p0 = ImGui::GetCursorScreenPos(); p0.y += slide; ImGui::SetCursorScreenPos(p0);
        if (it.kind == LogKind::ToolCall || it.kind == LogKind::ToolResult) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            bool isCall = it.kind == LogKind::ToolCall;
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 18 * s, p0.y));
            ui::drawIcon(dl, isCall ? Icon::Bolt : Icon::Check, ImVec2(p0.x + 26 * s, p0.y + ImGui::GetFontSize() * .6f), ImGui::GetFontSize() * .9f, isCall ? ui::col::purple : ui::col::green);
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 40 * s, p0.y));
            ImGui::PushFont(ui::fonts.mono);
            std::string head = (isCall ? "tool: " : "result: ") + it.title;
            if (isCall) { std::string a = it.text; if (a.size() > 90) a = a.substr(0, 90) + "..."; head += "  " + a; }
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(isCall ? ui::col::purple : ui::col::muted));
            if (isCall) ImGui::TextUnformatted(head.c_str());
            else { if (ImGui::TreeNodeEx(head.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::text)); ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrapW - 40 * s); ImGui::TextUnformatted(it.text.c_str()); ImGui::PopTextWrapPos(); ImGui::PopStyleColor(); ImGui::TreePop(); } }
            ImGui::PopStyleColor(); ImGui::PopFont();
        } else {
            ImVec2 start = ImGui::GetCursorScreenPos();
            ImGui::PushFont(ui::fonts.bold); ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(accent));
            ImGui::SetCursorScreenPos(ImVec2(start.x + 14 * s, start.y)); ImGui::TextUnformatted(it.title.c_str()); ImGui::PopStyleColor(); ImGui::PopFont();
            ImGui::SetCursorScreenPos(ImVec2(start.x + 14 * s, ImGui::GetCursorScreenPos().y));
            ImGui::BeginGroup(); renderRich(it.text, wrapW - 14 * s); ImGui::EndGroup();
            ImVec2 end = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(start.x, start.y + 2), ImVec2(start.x + 3, end.y - 2), ui::col::withAlpha(accent, born), 2.f);
            dl->AddRectFilled(ImVec2(start.x - 3, start.y + 2), ImVec2(start.x + 6, end.y - 2), ui::col::withAlpha(accent, 0.15f * born), 4.f);
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0, 6 * s));
        ImGui::PopID();
    }
    if (ai_.busy()) { ImGui::Dummy(ImVec2(0, 4)); ui::shimmerBar(ImGui::GetContentRegionAvail().x * 0.6f, 5 * s); }
    // stick to bottom when new items arrive
    if (log.size() != feedSeen_) { feedSeen_ = log.size(); if (feedStick_) ImGui::SetScrollHereY(1.f); }
    else if (ai_.busy() && feedStick_) ImGui::SetScrollHereY(1.f);
    if (ImGui::GetScrollY() < ImGui::GetScrollMaxY() - 40) { if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0) feedStick_ = false; }
    else feedStick_ = true;
    ui::endPanel();
}

void App::drawAskPanel(float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##ask", ImVec2(0, h), "Ask Happy", ui::col::orange, false);
    struct Q { const char* label; const char* text; } qs[] = {
        {"Explore", "Explore this target and tell me how it is organised and what is moddable."},
        {"Translate text", "Find the user-visible text (menus, dialogs) and propose translating it."},
        {"Re-texture", "Replace the texture I attached into the matching game image, resized to fit."},
        {"Find where... ", "Find where the following is stored/defined: "}};
    for (int i = 0; i < 4; ++i) { if (i) ImGui::SameLine(); bool v = false; if (ui::chipToggle(qs[i].label, &v, ui::col::orange)) { prompt_ = qs[i].text; } }
    ImGui::Dummy(ImVec2(0, 2));
    float boxH = ImGui::GetContentRegionAvail().y - 46 * s;
    bool submit = false;
    ImGui::PushFont(ui::fonts.body);
    if (ImGui::InputTextMultiline("##prompt", &prompt_, ImVec2(-FLT_MIN, boxH), ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_EnterReturnsTrue)) submit = true;
    ImGui::PopFont();
    if (prompt_.empty() && !ImGui::IsItemActive()) { ImVec2 pmin = ImGui::GetItemRectMin(); ImGui::GetWindowDrawList()->AddText(ImVec2(pmin.x + 12 * s, pmin.y + 8 * s), ui::col::withAlpha(ui::col::muted, 0.8f), "What should I do?  e.g. \"make the sword cost 50 gold\"   (Enter = send)"); }
    std::string why; bool ready = aiReady(why);
    if (ai_.busy()) { if (ui::button("Stop", ImVec2(130 * s, 0), ui::col::red, true, Icon::Stop)) ai_.cancel(); }
    else {
        ImGui::BeginDisabled(!ready || prompt_.empty());
        if (ui::button("Send to Happy", ImVec2(170 * s, 0), ui::col::pink, true, Icon::Send) || (submit && ready && !prompt_.empty())) { std::string t = prompt_; prompt_.clear(); sendToAi(t); }
        ImGui::EndDisabled();
        if (!ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", why.c_str());
    }
    ImGui::SameLine();
    if (ai_.hasHistory() && !ai_.busy()) { if (ui::button("New chat", ImVec2(0, 0), ui::col::purple, false, Icon::Refresh)) { ai_.reset(); plan_ = heng::Plan(); selected_.clear(); hasReport_ = false; } ImGui::SameLine(); }
    if (!ready) { ImGui::AlignTextToFramePadding(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::yellow), "%s", why.c_str());
                  ImGui::SameLine(); if (why.find("Settings") != std::string::npos && ui::button("Open settings", ImVec2(0, 0), ui::col::yellow)) { settingsOpen_ = true; } }
    ui::endPanel();
}

void App::pageWorkspace(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f; float gap = ImGui::GetStyle().ItemSpacing.x;
    float leftW = std::max(330 * s, w * 0.36f);
    ImGui::BeginChild("##wl", ImVec2(leftW, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    float th = targetKind_ == 3 ? h * 0.62f : h * 0.46f;
    drawTargetPanel(th);
    drawAttachmentsPanel(h - th - ImGui::GetStyle().ItemSpacing.y);
    ImGui::EndChild();
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##wr", ImVec2(0, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    float askH = std::max(190 * s, h * 0.30f);
    drawFeed(h - askH - ImGui::GetStyle().ItemSpacing.y);
    drawAskPanel(askH);
    ImGui::EndChild();
}

} // namespace he
