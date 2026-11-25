// page_tools.cpp -- external decompilers / disassemblers and compilers.
#include "app.h"
#include "theme.h"
#include "app/fsutil.h"
#include "core/toolkit.h"
#include "core/sha256.h"
#include "core/install.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace he {
using namespace ui;

static ImVec4 C4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
static std::string tailOf(const std::string& s, size_t n) { return s.size() <= n ? s : "..." + s.substr(s.size() - n); }
static std::string headOf(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n) + "\n...[shown part only - use Copy for everything]"; }

namespace {
// Real network access for installs: official hosts only, https only, streamed to disk with a running SHA-256.
class PlatformDownloader : public IDownloader {
public:
    bool getString(const std::string& url, std::string& out, std::string& err, std::atomic<bool>* cancel) override {
        if (!allowed(url, err)) return false;
        out.clear();
        platform::FetchResult r = platform::httpFetch(url, {{"Accept", "application/vnd.github+json, application/json"}}, 60, cancel,
            [&](const char* p, size_t n) { if (out.size() + n > (16u << 20)) return false; out.append(p, n); return true; }, nullptr);
        if (!r.ok) err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
        return r.ok;
    }
    bool getFile(const std::string& url, const std::string& dest, std::string& sha, std::string& err, std::atomic<bool>* cancel,
                 const std::function<void(uint64_t, uint64_t)>& progress) override {
        if (!allowed(url, err)) return false;
        std::ofstream f(fs::u8path(dest), std::ios::binary | std::ios::trunc);
        if (!f) { err = "cannot write " + dest; return false; }
        heng::Sha256 h; bool wrote = true;
        platform::FetchResult r = platform::httpFetch(url, {}, 120, cancel,
            [&](const char* p, size_t n) { h.update(p, n); f.write(p, (std::streamsize)n); wrote = (bool)f; return wrote; }, progress);
        f.close();
        if (!r.ok) { err = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error; std::error_code ec; fs::remove(fs::u8path(dest), ec); return false; }
        if (!wrote) { err = "disk write failed (disk full?)"; return false; }
        sha = h.hex(); return true;
    }
private:
    static bool allowed(const std::string& url, std::string& err) {
        std::string host, path; bool tls; int port;
        if (!heng::splitUrl(url, host, path, tls, port) || !heng::isAllowedDownloadHost(host)) { err = "blocked: not an official download source"; return false; }
        return true;
    }
};
} // namespace

void App::initToolbox() {
    ToolboxEnv env;
#ifdef _WIN32
    env.windows = true;
#endif
    env.is64 = platform::is64BitSystem();
    const char* pe = std::getenv("PATH");
    env.pathDirs = heng::splitPathList(pe ? pe : "", env.windows);
    std::string home = platform::homeDir();
    if (env.windows) {
        const char* extra[] = {"C:/Program Files (x86)/Dev-Cpp/MinGW64/bin", "C:/Program Files/Dev-Cpp/MinGW64/bin", "C:/Dev-Cpp/MinGW64/bin",
            "C:/msys64/ucrt64/bin", "C:/msys64/mingw64/bin", "C:/msys64/mingw32/bin", "C:/msys64/usr/bin", "C:/MinGW/bin", "C:/mingw64/bin", "C:/mingw32/bin",
            "C:/Program Files/LLVM/bin", "C:/Program Files/NASM", "C:/Program Files (x86)/NASM", "C:/Program Files/radare2/bin", "C:/tcc"};
        for (const char* e : extra) env.pathDirs.push_back(e);
        env.pathDirs.push_back(home + "/.dotnet/tools");
        const char* rootsW[] = {"C:/", "D:/", "C:/tools", "C:/Program Files", "C:/Program Files (x86)"};
        for (const char* r : rootsW) env.roots.push_back(r);
    } else {
        const char* extra[] = {"/usr/local/bin", "/opt/bin", "/snap/bin"};
        for (const char* e : extra) env.pathDirs.push_back(e);
        env.pathDirs.push_back(home + "/.local/bin"); env.pathDirs.push_back(home + "/.dotnet/tools");
        env.roots.push_back("/opt"); env.roots.push_back("/usr/local");
    }
    env.roots.push_back(platform::exeDir()); env.roots.push_back(platform::exeDir() + "/tools");
    env.roots.push_back(home); env.roots.push_back(home + "/Downloads"); env.roots.push_back(home + "/tools");
    env.roots.push_back(platform::documentsDir());
    env.workDir = platform::configDir() + "/work";
    env.toolsDir = platform::configDir() + "/tools";
    if (!downloader_) downloader_.reset(new PlatformDownloader());
    toolbox_.setDownloader(downloader_.get());
    toolbox_.setAutoApprove(cfg_.autoApproveInstalls);
    toolbox_.configure(env, cfg_.toolPaths);
    toolbox_.rescanAsync();
    ws_.setToolbox(&toolbox_);
    toolboxReady_ = true;
}

void App::drawInstallApproval() {
    float s = ImGui::GetFontSize() / 17.f;
    InstallRequest rq;
    bool pending = toolbox_.pendingApproval(rq);
    if (pending && !ai_.busy()) { toolbox_.answerApproval(false); pending = false; }   // the AI run was stopped
    if (pending && !wantApprovalPopup_) { wantApprovalPopup_ = true; ImGui::OpenPopup("##approveInstall"); }
    if (!pending && wantApprovalPopup_) { wantApprovalPopup_ = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(520 * s, 0), ImVec2(720 * s, 600 * s));
    if (ImGui::BeginPopupModal("##approveInstall", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ui::sectionLabel("Happy wants to download a tool", ui::col::orange);
        if (!pending) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        ImGui::PushTextWrapPos(520 * s);
        ImGui::PushFont(ui::fonts.bold); ImGui::TextUnformatted(rq.title.c_str()); ImGui::PopFont();
        ImGui::TextDisabled("File: %s", rq.fileName.c_str());
        if (rq.size) ImGui::TextDisabled("Size: %.0f MB", rq.size / 1048576.0);
        std::string host, path; bool tls; int port; heng::splitUrl(rq.url, host, path, tls, port);
        ImGui::TextDisabled("From: %s (official source, https)", host.c_str());
        ImGui::TextDisabled("Into: %s", rq.destDir.c_str());
        ImGui::TextColored(C4(rq.hasChecksum ? ui::col::green : ui::col::yellow), rq.hasChecksum ? "Checksum: published and will be verified" : "Checksum: none published by the project");
        for (const std::string& a : rq.alsoInstalls) ImGui::TextColored(C4(ui::col::cyan), "Also needs: %s", a.c_str());
        if (!rq.note.empty()) ImGui::TextColored(C4(ui::col::muted), "%s", rq.note.c_str());
        ImGui::Dummy(ImVec2(0, 4 * s));
        ImGui::TextDisabled("The program is only unpacked into Happy's own tools folder; Happy never runs the installer as administrator.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6 * s));
        if (ui::button("Download", ImVec2(150 * s, 0), ui::col::green, true, Icon::Check)) { toolbox_.answerApproval(true); wantApprovalPopup_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine(); if (ui::button("Deny", ImVec2(120 * s, 0), ui::col::red)) { toolbox_.answerApproval(false); wantApprovalPopup_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

std::string App::buildsBase() const {
    return cfg_.outputDir.empty() ? platform::documentsDir() + "/HappyEngineering/Builds" : cfg_.outputDir + "/Builds";
}

void App::useCodeInTools(const std::string& code, const std::string& lang) {
    srcBuf_ = code;
    std::string l = lang;
    for (char& c : l) c = (char)std::tolower((unsigned char)c);
    if (l == "cpp" || l == "c++" || l == "cc" || l == "cxx") srcLang_ = "cpp";
    else if (l == "asm" || l == "nasm" || l == "assembly") srcLang_ = "asm";
    else srcLang_ = "c";
    page_ = Page::Tools; toolsTab_ = 1;
    ui::toast(Toast::Success, "Code sent to the Recompile tab");
}

std::string App::latestAiCode(std::string* lang) const {
    std::vector<LogItem> log = ai_.log();
    for (size_t i = log.size(); i-- > 0;) {
        if (log[i].kind != LogKind::Assistant) continue;
        std::vector<std::string> langs; std::vector<std::string> blocks = heng::extractCodeBlocks(log[i].text, &langs);
        if (blocks.empty()) continue;
        size_t best = 0; bool found = false;
        for (size_t k = 0; k < blocks.size(); ++k) {
            std::string l = langs[k]; for (char& c : l) c = (char)std::tolower((unsigned char)c);
            if (l == "c" || l == "cpp" || l == "c++" || l == "cc" || l == "asm" || l == "nasm") { best = k; found = true; break; }
        }
        if (!found) for (size_t k = 1; k < blocks.size(); ++k) if (blocks[k].size() > blocks[best].size()) best = k;
        if (lang) *lang = langs[best];
        return blocks[best];
    }
    return std::string();
}

std::string App::chatAsText() const {
    std::string out;
    for (const LogItem& it : ai_.log()) {
        if (it.kind == LogKind::ToolCall) out += "[tool] " + it.title + " " + it.text + "\n\n";
        else if (it.kind == LogKind::ToolResult) out += "[result] " + it.title + "\n" + it.text + "\n\n";
        else out += it.title + ":\n" + it.text + "\n\n";
    }
    return out;
}

// ------------------------------------------------------------------ installed tools
void App::toolsListPanel(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##toollist", ImVec2(w, h), "Installed tools", ui::col::cyan, true);
    std::vector<ToolStatus> st = toolbox_.statuses();
    bool scanning = toolbox_.scanning();
    if (ui::button(scanning ? "Scanning..." : "Rescan", ImVec2(0, 0), ui::col::cyan, false, Icon::Refresh) && !scanning) toolbox_.rescanAsync();
    ImGui::SameLine(); ImGui::AlignTextToFramePadding();
    int found = 0; for (auto& x : st) if (x.found) ++found;
    ImGui::TextDisabled("%d of %d found", found, (int)st.size());
    if (scanning) ui::shimmerBar(ImGui::GetContentRegionAvail().x, 4 * s);
    // install progress / result
    {
        InstallState is = toolbox_.installState();
        if (is.active) {
            ImGui::TextColored(C4(ui::col::orange), "%s", is.stage.c_str());
            float frac = is.total ? (float)((double)is.done / (double)is.total) : 0.f;
            ImGui::ProgressBar(is.total ? frac : -1.f * (float)ImGui::GetTime(), ImVec2(ImGui::GetContentRegionAvail().x, 8 * s), "");
            if (is.total) ImGui::TextDisabled("%.0f / %.0f MB", is.done / 1048576.0, is.total / 1048576.0);
            if (ui::button("Cancel install", ImVec2(0, 0), ui::col::red, false, Icon::Stop)) toolbox_.cancelInstall();
        } else {
            InstallOutcome lo = toolbox_.lastInstall();
            if (!lo.message.empty()) {
                ImGui::PushTextWrapPos(0); ImGui::TextColored(C4(lo.ok ? ui::col::green : ui::col::red), "%s", lo.message.c_str()); ImGui::PopTextWrapPos();
            }
        }
    }
    ImGui::Dummy(ImVec2(0, 4 * s));
    if (!platform::is64BitSystem()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(C4(ui::col::yellow), "This is a 32-bit system: Ghidra and RetDec need 64-bit. radare2, Rizin and objdump work here.");
        ImGui::PopTextWrapPos(); ImGui::Dummy(ImVec2(0, 4 * s));
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    heng::ToolKind groups[] = {heng::ToolKind::Decompiler, heng::ToolKind::Disassembler, heng::ToolKind::Compiler, heng::ToolKind::Assembler, heng::ToolKind::Runtime};
    const char* gname[] = {"Decompilers", "Disassemblers", "Compilers", "Assemblers", "Runtimes"};
    for (int g = 0; g < 5; ++g) {
        bool any = false;
        for (const heng::ToolDef& d : heng::toolCatalog()) if (d.kind == groups[g]) any = true;
        if (!any) continue;
        ui::sectionLabel(gname[g], g < 2 ? ui::col::pink : ui::col::orange);
        for (const heng::ToolDef& d : heng::toolCatalog()) {
            if (d.kind != groups[g]) continue;
            ToolStatus ts; for (auto& x : st) if (x.id == d.id) ts = x;
            ImGui::PushID(d.id.c_str());
            ImVec2 p0 = ImGui::GetCursorScreenPos(); float cw = ImGui::GetContentRegionAvail().x;
            ImGui::BeginGroup();
            ImGui::Dummy(ImVec2(0, 3 * s));
            ImGui::Indent(10 * s);
            ui::statusDot(ts.found ? ui::col::green : ui::col::muted, false); ImGui::SameLine(0, 6);
            ImGui::PushFont(ui::fonts.bold); ImGui::TextUnformatted(d.name.c_str()); ImGui::PopFont();
            ImGui::SameLine(); ui::badge(ts.found ? "installed" : "not found", ts.found ? ui::col::green : ui::col::yellow);
            if (d.needs64 && !platform::is64BitSystem()) { ImGui::SameLine(); ui::badge("64-bit only", ui::col::red); }
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + cw - 24 * s);
            ImGui::PushFont(ui::fonts.small);
            ImGui::TextColored(C4(ui::col::muted), "%s", d.desc.c_str());
            if (ts.found) { ImGui::TextColored(C4(ui::col::cyan), "%s", ts.path.c_str()); if (!ts.version.empty()) ImGui::TextColored(C4(ui::col::muted), "%s", ts.version.c_str()); }
            else if (!d.notes.empty()) ImGui::TextColored(C4(ui::col::yellow), "%s", d.notes.c_str());
            ImGui::PopFont(); ImGui::PopTextWrapPos();
            if (!ts.found) {
                std::string why; bool can = toolbox_.installable(d.id, &why);
                ImGui::BeginDisabled(!can || toolbox_.installing());
                if (ui::button("Install automatically", ImVec2(0, 0), ui::col::green, true, Icon::Plus)) toolbox_.startInstall(d.id);
                ImGui::EndDisabled();
                if (!can && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("%s", why.c_str()); }
            }
            if (ui::button("Path...", ImVec2(0, 0), ui::col::purple)) { toolPickId_ = d.id; browser_.open(d.relPaths.empty() ? "Pick the " + d.name + " program" : "Pick the " + d.name + " folder", d.relPaths.empty() ? FileBrowser::Mode::OpenFile : FileBrowser::Mode::OpenFolder, platform::homeDir(), 8); }
            ImGui::SameLine(); if (ui::button("Download", ImVec2(0, 0), ui::col::cyan)) platform::openInFileManager(d.url);
            ImGui::SameLine(); if (ui::button("Install help", ImVec2(0, 0), ui::col::pink)) {
#ifdef _WIN32
                ImGui::SetClipboardText((d.name + ": " + d.installWin + "\n" + d.url).c_str());
#else
                ImGui::SetClipboardText((d.name + ": " + d.installLinux + "\n" + d.url).c_str());
#endif
                ui::toast(Toast::Success, "Install steps copied");
            }
            if (cfg_.toolPaths.count(d.id)) { ImGui::SameLine(); if (ui::button("Reset path", ImVec2(0, 0), ui::col::muted)) { cfg_.toolPaths.erase(d.id); markDirty(); initToolbox(); } }
            ImGui::Unindent(10 * s);
            ImGui::Dummy(ImVec2(0, 3 * s));
            ImGui::EndGroup();
            ImVec2 p1 = ImVec2(p0.x + cw, ImGui::GetItemRectMax().y);
            dl->AddRect(p0, p1, ui::col::withAlpha(ts.found ? ui::col::green : ui::col::purple, ts.found ? 0.45f : 0.3f), 10 * s);
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, 4 * s));
        }
    }
    ui::endPanel();
}

// ------------------------------------------------------------------ decompile
void App::toolsDecompilePanel(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##decomp", ImVec2(w, h), "Decompile / disassemble a file", ui::col::pink, true);
    bool busy = toolbox_.busy();
    ui::sectionLabel("Input file", ui::col::cyan);
    float bw = 110 * s;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputTextWithHint("##dcin", "path to an .exe / .dll / .so / ELF ... (or drop / browse)", &dcInput_);
    ImGui::SameLine(); if (ui::button("Browse...", ImVec2(bw, 0), ui::col::purple, false, Icon::Folder)) browser_.open("Pick a file to decompile", FileBrowser::Mode::OpenFile, dcInput_.empty() ? ws_.root() : S(P(dcInput_).parent_path()), 6);
    bool hasFocus = !ws_.focusFile().empty() && !ws_.root().empty();
    ImGui::BeginDisabled(!hasFocus);
    if (ui::button("Use my target file", ImVec2(0, 0), ui::col::yellow)) dcInput_ = ws_.root() + "/" + ws_.focusFile();
    ImGui::EndDisabled();
    if (!hasFocus && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Choose a single file as target on the Workspace page");

    ImGui::Dummy(ImVec2(0, 2 * s));
    std::vector<ToolStatus> st = toolbox_.statuses();
    std::string comboLabel = dcTool_.empty() ? "Auto (best available)" : (heng::findToolDef(dcTool_) ? heng::findToolDef(dcTool_)->name : dcTool_);
    ImGui::SetNextItemWidth(230 * s);
    if (ImGui::BeginCombo("##dctool", comboLabel.c_str())) {
        if (ImGui::Selectable("Auto (best available)", dcTool_.empty())) dcTool_.clear();
        for (const heng::ToolDef& d : heng::toolCatalog()) {
            if (!d.decompiles()) continue;
            bool f = false; for (auto& x : st) if (x.id == d.id) f = x.found;
            ImGui::BeginDisabled(!f);
            std::string lab = d.name + (f ? "" : "  (not installed)");
            if (ImGui::Selectable(lab.c_str(), dcTool_ == d.id)) dcTool_ = d.id;
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputTextWithHint("##dcfn", "only functions containing this name (optional, faster)", &dcFunc_);

    std::string why; std::string pick = toolbox_.pickDecompiler(dcTool_, why);
    ImGui::BeginDisabled(busy || dcInput_.empty() || pick.empty());
    if (ui::button("Run", ImVec2(130 * s, 0), ui::col::pink, true, Icon::Play)) { toolbox_.startDecompile(dcTool_, dcInput_, dcFunc_); dcShown_.clear(); dcToken_ = "running"; }
    ImGui::EndDisabled();
    if (busy) { ImGui::SameLine(); if (ui::button("Cancel", ImVec2(110 * s, 0), ui::col::red, false, Icon::Stop)) toolbox_.cancelJob(); }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding();
    if (pick.empty()) ImGui::TextColored(C4(ui::col::yellow), "%s", why.c_str());
    else ImGui::TextColored(C4(ui::col::muted), "will use: %s", heng::findToolDef(pick)->name.c_str());
    if (busy) ui::shimmerBar(ImGui::GetContentRegionAvail().x, 5 * s);

    // result text
    JobResult jr = toolbox_.lastResult();
    if (busy) dcShown_ = tailOf(toolbox_.liveLog(), 30000);
    else if (jr.done) {
        std::string tok = jr.outFile + "|" + jr.error + "|" + std::to_string(jr.text.size());
        if (tok != dcToken_) { dcToken_ = tok; dcShown_ = jr.ok ? headOf(jr.text, 150000) : "ERROR: " + jr.error + "\n\n" + tailOf(toolbox_.liveLog(), 8000); }
    }
    ImGui::Dummy(ImVec2(0, 2 * s));
    ui::sectionLabel(jr.ok && !busy ? "Result" : "Output", ui::col::purple);
    float reserve = 44 * s;
    ImGui::PushFont(ui::fonts.mono);
    ImGui::InputTextMultiline("##dcout", &dcShown_, ImVec2(-FLT_MIN, std::max(120.f * s, ImGui::GetContentRegionAvail().y - reserve)), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();
    bool haveRes = jr.ok && !busy;
    ImGui::BeginDisabled(!haveRes);
    if (ui::button("Copy all", ImVec2(0, 0), ui::col::cyan, false, Icon::Copy)) { ImGui::SetClipboardText(jr.text.c_str()); ui::toast(Toast::Success, "Copied " + std::to_string(jr.text.size()) + " characters"); }
    ImGui::SameLine();
    if (ui::button("Attach for AI", ImVec2(0, 0), ui::col::yellow, false, Icon::File)) { std::string e; if (ws_.addAttachment(jr.outFile, e)) ui::toast(Toast::Success, "Attached " + heng::baseNameOf(jr.outFile)); else ui::toast(Toast::Error, e); }
    ImGui::SameLine();
    if (ui::button("Ask AI to explain", ImVec2(0, 0), ui::col::pink, true, Icon::Send)) {
        std::string why2; if (!aiReady(why2)) ui::toast(Toast::Warn, why2);
        else { page_ = Page::Workspace; sendToAi("I decompiled " + heng::baseNameOf(dcInput_) + " with " + jr.tool + ". The output is saved at _happy/decomp/" + heng::baseNameOf(jr.outFile) + " - read it with read_text / search_text and explain what the interesting parts do (game logic, values, checks I could change)."); }
    }
    ImGui::SameLine();
    if (ui::button("Open folder", ImVec2(0, 0), ui::col::purple, false, Icon::Folder)) platform::openInFileManager(toolbox_.decompDir());
    ImGui::EndDisabled();
    ui::endPanel();
}

// ------------------------------------------------------------------ recompile
void App::toolsBuildPanel(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##build", ImVec2(w, h), "Recompile source", ui::col::orange, true);
    bool busy = toolbox_.busy();
    float avail = ImGui::GetContentRegionAvail().y;
    ImGui::PushFont(ui::fonts.mono);
    ImGui::InputTextMultiline("##src", &srcBuf_, ImVec2(-FLT_MIN, std::max(130.f * s, avail * 0.36f)), ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopFont();
    if (srcBuf_.empty() && !ImGui::IsItemActive()) { ImVec2 pm = ImGui::GetItemRectMin(); ImGui::GetWindowDrawList()->AddText(ImVec2(pm.x + 10 * s, pm.y + 8 * s), ui::col::withAlpha(ui::col::muted, 0.8f), "Paste C / C++ / NASM source here, load a file, or send code from the AI chat."); }
    if (ui::button("Load file...", ImVec2(0, 0), ui::col::purple, false, Icon::Folder)) browser_.open("Load a source file", FileBrowser::Mode::OpenFile, platform::homeDir(), 7);
    ImGui::SameLine(); if (ui::button("Paste", ImVec2(0, 0), ui::col::cyan, false, Icon::Copy)) { const char* c = ImGui::GetClipboardText(); if (c) srcBuf_ = c; }
    ImGui::SameLine(); if (ui::button("Latest AI code", ImVec2(0, 0), ui::col::pink)) { std::string l; std::string c = latestAiCode(&l); if (c.empty()) ui::toast(Toast::Warn, "No code block in the AI's replies yet"); else useCodeInTools(c, l); }
    ImGui::SameLine(); if (ui::button("Copy", ImVec2(0, 0), ui::col::cyan)) { ImGui::SetClipboardText(srcBuf_.c_str()); ui::toast(Toast::Success, "Source copied"); }
    ImGui::SameLine(); if (ui::button("Clear", ImVec2(0, 0), ui::col::muted)) srcBuf_.clear();

    ImGui::Dummy(ImVec2(0, 2 * s));
    const char* langs[] = {"C", "C++", "Asm"}; int li = srcLang_ == "cpp" ? 1 : srcLang_ == "asm" ? 2 : 0;
    if (ui::segmented("##lang", langs, 3, &li, 170 * s)) srcLang_ = li == 1 ? "cpp" : li == 2 ? "asm" : "c";
    ImGui::SameLine();
    const char* archs[] = {"Auto", "32-bit", "64-bit"};
    ui::segmented("##arch", archs, 3, &cpBits_, 230 * s);
    ImGui::SetNextItemWidth(230 * s);
    std::vector<ToolStatus> st = toolbox_.statuses();
    std::string comboLabel = cpTool_.empty() ? "Compiler: auto" : (heng::findToolDef(cpTool_) ? heng::findToolDef(cpTool_)->name : cpTool_);
    if (ImGui::BeginCombo("##cptool", comboLabel.c_str())) {
        if (ImGui::Selectable("Compiler: auto", cpTool_.empty())) cpTool_.clear();
        for (const heng::ToolDef& d : heng::toolCatalog()) {
            if (!d.builds() || !heng::compilerSupports(d, srcLang_)) continue;
            bool f = false; for (auto& x : st) if (x.id == d.id) f = x.found;
            ImGui::BeginDisabled(!f);
            if (ImGui::Selectable((d.name + (f ? "" : "  (not installed)")).c_str(), cpTool_ == d.id)) cpTool_ = d.id;
            ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine(); ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x); ImGui::InputTextWithHint("##flags", "extra flags, e.g. -O2 -Wall -lm", &srcFlags_);
    ImGui::SetNextItemWidth(-FLT_MIN); ImGui::InputTextWithHint("##outname", "output name (.exe added for Windows builds)", &srcName_);

    int bits = cpBits_ == 1 ? 32 : cpBits_ == 2 ? 64 : 0;
    std::string why; std::string pick = toolbox_.pickCompiler(cpTool_, srcLang_, bits, why);
    ImGui::BeginDisabled(busy || srcBuf_.empty() || pick.empty());
    if (ui::button("Build", ImVec2(130 * s, 0), ui::col::orange, true, Icon::Bolt)) {
        std::string dir = toolbox_.env().workDir + "/src"; std::error_code ec; fs::create_directories(P(dir), ec);
        std::string nm; for (char c : srcName_) nm.push_back((std::isalnum((unsigned char)c) || c == '_' || c == '-') ? c : '_'); if (nm.empty()) nm = "patched";
        std::string src = dir + "/" + nm + "." + srcLang_;
        std::vector<uint8_t> b(srcBuf_.begin(), srcBuf_.end()); std::string e;
        if (!writeWholeFile(src, b, e)) ui::toast(Toast::Error, e);
        else { toolbox_.startCompile(cpTool_, std::vector<std::string>(1, src), nm, srcFlags_, bits); lastLogSeen_.clear(); buildNote_.clear(); }
    }
    ImGui::EndDisabled();
    if (busy) { ImGui::SameLine(); if (ui::button("Cancel", ImVec2(110 * s, 0), ui::col::red, false, Icon::Stop)) toolbox_.cancelJob(); }
    ImGui::SameLine(); ImGui::AlignTextToFramePadding();
    if (pick.empty()) ImGui::TextColored(C4(ui::col::yellow), "%s", why.c_str());
    else ImGui::TextColored(C4(ui::col::muted), "will use: %s", heng::findToolDef(pick)->name.c_str());
    if (busy) ui::shimmerBar(ImGui::GetContentRegionAvail().x, 5 * s);

    JobResult jr = toolbox_.lastResult();
    std::string log = toolbox_.liveLog();
    ImGui::PushFont(ui::fonts.mono);
    std::string shown = tailOf(log, 40000);
    ImGui::InputTextMultiline("##buildlog", &shown, ImVec2(-FLT_MIN, std::max(70.f * s, ImGui::GetContentRegionAvail().y - 46 * s)), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();
    bool built = jr.done && jr.ok && !busy && !jr.outFile.empty() && jr.title.compare(0, 5, "Build") == 0;
    bool failed = jr.done && !jr.ok && !busy && jr.title.compare(0, 5, "Build") == 0;
    ImGui::BeginDisabled(!built);
    if (ui::button("Save build", ImVec2(0, 0), ui::col::green, true, Icon::Check)) {
        std::error_code ec; fs::create_directories(P(buildsBase()), ec);
        std::string dst = buildsBase() + "/" + heng::baseNameOf(jr.outFile);
        fs::copy_file(P(jr.outFile), P(dst), fs::copy_options::overwrite_existing, ec);
        if (ec) ui::toast(Toast::Error, "Copy failed: " + ec.message()); else { buildNote_ = "Saved: " + dst; ui::toast(Toast::Success, "Saved to " + dst); }
    }
    ImGui::SameLine(); if (ui::button("Builds folder", ImVec2(0, 0), ui::col::purple, false, Icon::Folder)) { std::error_code ec; fs::create_directories(P(buildsBase()), ec); platform::openInFileManager(buildsBase()); }
    ImGui::EndDisabled();
    ImGui::SameLine(); if (ui::button("Copy log", ImVec2(0, 0), ui::col::cyan, false, Icon::Copy)) { ImGui::SetClipboardText(log.c_str()); ui::toast(Toast::Success, "Log copied"); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!failed);
    if (ui::button("Ask AI to fix", ImVec2(0, 0), ui::col::pink, true, Icon::Send)) {
        std::string why2; if (!aiReady(why2)) ui::toast(Toast::Warn, why2);
        else { page_ = Page::Workspace; sendToAi("This source fails to build. Please fix it and reply with the complete corrected source in one ```" + srcLang_ + " code block.\n\nSource:\n```" + srcLang_ + "\n" + srcBuf_ + "\n```\n\nCompiler output:\n```\n" + tailOf(log, 6000) + "\n```"); }
    }
    ImGui::EndDisabled();
    if (!buildNote_.empty()) { ImGui::SameLine(); ImGui::TextColored(C4(ui::col::green), "%s", buildNote_.c_str()); }
    ui::endPanel();
}

void App::pageTools(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f; float gap = ImGui::GetStyle().ItemSpacing.x;
    float leftW = std::max(370 * s, w * 0.40f);
    toolsListPanel(leftW, h);
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##toolsright", ImVec2(0, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    const char* tabs[] = {"Decompile", "Recompile"};
    ui::segmented("##toolstab", tabs, 2, &toolsTab_, 320 * s);
    ImGui::Dummy(ImVec2(0, 2 * s));
    float ph = h - ImGui::GetCursorPosY();
    if (toolsTab_ == 0) toolsDecompilePanel(0, ph); else toolsBuildPanel(0, ph);
    ImGui::EndChild();
}

} // namespace he
