#include "app.h"
#include "theme.h"
#include "app/fsutil.h"
#include <algorithm>
#include <cmath>

namespace he {
using namespace ui;

void App::attachMemory(const ProcPick& p) {
    if (scanning_) return;
    mem_.reset(); scanner_.reset(); memErr_.clear(); memPid_ = 0;
    std::string err; mem_ = platform::openProcessMemory(p.pid, err);
    if (!mem_) { memErr_ = err; ui::toast(Toast::Error, "Cannot attach: " + err); return; }
    memPid_ = p.pid; memName_ = p.name; scanner_.reset(new heng::Scanner(*mem_));
    ui::toast(Toast::Success, "Attached to " + p.name);
}

void App::startScan(bool first) {
    if (!mem_ || !scanner_ || scanning_) return;
    if (scanTh_.joinable()) scanTh_.join();
    heng::ValueType vt = (heng::ValueType)scanType_;
    std::vector<uint8_t> needle; std::string err;
    bool needValue = first || scanMode_ == 0;
    if (needValue && !heng::parseValue(vt, scanValue_, needle, err)) { ui::toast(Toast::Error, err); return; }
    scanning_ = true; scanMsg_.clear();
    int mode = scanMode_;
    scanTh_ = std::thread([this, first, vt, needle, mode]() {
        if (first) scanner_->firstScan(vt, needle);
        else scanner_->nextScan((heng::ScanMode)mode, mode == 0 ? &needle : nullptr);
        scanning_ = false;
    });
}

void App::tickMemory() {
    if (!mem_) return;
    double now = ImGui::GetTime();
    if (memPid_ && !platform::processAlive(memPid_) && !scanning_) { ui::toast(Toast::Warn, memName_ + " has exited."); mem_.reset(); scanner_.reset(); memPid_ = 0; for (auto& w : watch_) w.frozen = false; return; }
    if (now - lastWatchRead_ > 0.5) {
        lastWatchRead_ = now;
        for (auto& w : watch_) { std::vector<uint8_t> b; if (heng::readValue(*mem_, w.address, w.type, b)) w.lastText = heng::formatValue(w.type, b.data()); else w.lastText = "?"; }
    }
    if (now - lastFreeze_ > 0.08) { lastFreeze_ = now; for (auto& w : watch_) if (w.frozen && !w.freezeBytes.empty()) mem_->write(w.address, w.freezeBytes.data(), w.freezeBytes.size()); }
}

void App::pageMemory(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f; float gap = ImGui::GetStyle().ItemSpacing.x;
    float leftW = std::max(300 * s, w * 0.30f);
    ui::beginPanel("##memproc", ImVec2(leftW, h), "Process", ui::col::cyan, false);
    if (mem_) {
        ui::badge("ATTACHED", ui::col::green); ImGui::SameLine(); ImGui::Text("%s [%u]", memName_.c_str(), memPid_);
        if (ui::button("Detach", ImVec2(0, 0), ui::col::red, false, Icon::Stop) && !scanning_) { mem_.reset(); scanner_.reset(); memPid_ = 0; for (auto& x : watch_) x.frozen = false; }
    } else {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Pick the running game. Values are only read/written at addresses you choose; nothing is injected or hooked.");
        ImGui::PopTextWrapPos();
        if (!memErr_.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::red), "%s", memErr_.c_str()); ImGui::PopTextWrapPos(); }
        else { ImGui::PushFont(ui::fonts.small); ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s", platform::memoryAccessHint().c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont(); }
    }
    ImGui::Dummy(ImVec2(0, 4));
    drawProcessPicker(ImGui::GetContentRegionAvail().y, true);
    ui::endPanel();
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##memright", ImVec2(0, h), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    float scanH = h * 0.52f;
    ui::beginPanel("##scan", ImVec2(0, scanH), "Value scanner", ui::col::pink, false);
    ImGui::BeginDisabled(!mem_ || scanning_);
    const char* types[] = {"int8", "int16", "int32", "int64", "float", "double"};
    ImGui::SetNextItemWidth(110 * s); ImGui::Combo("##vt", &scanType_, types, 6); ImGui::SameLine();
    const char* modes[] = {"exact value", "changed", "unchanged", "increased", "decreased"};
    ImGui::SetNextItemWidth(150 * s); ImGui::Combo("##sm", &scanMode_, modes, 5); ImGui::SameLine();
    ImGui::SetNextItemWidth(140 * s); ImGui::InputText("##sv", &scanValue_);
    bool active = scanner_ && scanner_->active();
    if (ui::button("First scan", ImVec2(0, 0), ui::col::pink, !active, Icon::Play)) startScan(true);
    ImGui::SameLine();
    ImGui::BeginDisabled(!active);
    if (ui::button("Next scan", ImVec2(0, 0), ui::col::cyan, active, Icon::Refresh)) startScan(false);
    ImGui::EndDisabled();
    ImGui::SameLine(); if (ui::button("Reset", ImVec2(0, 0), ui::col::purple) && scanner_) scanner_->reset();
    ImGui::EndDisabled();
    if (scanning_) { ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::cyan), "Scanning..."); ui::shimmerBar(0, 6 * s); }
    else if (scanner_ && scanner_->active()) {
        size_t n = scanner_->hits().size();
        ImGui::TextDisabled("%zu candidate address(es)%s", n, scanner_->truncated() ? " (list truncated - narrow it down)" : (n > 200 ? " - narrow it down to name one" : ""));
    } else ImGui::TextDisabled(mem_ ? "Search a value (e.g. your gold), change it in the game, then use Next scan." : "Attach to a process first.");
    if (!scanning_ && scanner_ && scanner_->active()) {
        if (ImGui::BeginTable("##hits", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
            ImGui::TableSetupColumn("Address"); ImGui::TableSetupColumn("Value"); ImGui::TableSetupColumn("Previous"); ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110 * s);
            ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
            const auto& hits = scanner_->hits(); heng::ValueType vt = scanner_->type();
            for (size_t i = 0; i < hits.size() && i < 200; ++i) {
                ImGui::TableNextRow(); ImGui::PushID((int)i);
                ImGui::TableSetColumnIndex(0); ImGui::PushFont(ui::fonts.mono); ImGui::Text("0x%llX", (unsigned long long)hits[i].addr); ImGui::PopFont();
                ImGui::TableSetColumnIndex(1); std::vector<uint8_t> b; if (heng::readValue(*mem_, hits[i].addr, vt, b)) ImGui::TextUnformatted(heng::formatValue(vt, b.data()).c_str()); else ImGui::TextDisabled("?");
                ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("%s", heng::formatValue(vt, hits[i].prev).c_str());
                ImGui::TableSetColumnIndex(3); if (ui::button("Watch", ImVec2(96 * s, 0), ui::col::cyan, false, Icon::Plus)) { labelForHit_ = (int)i; newLabel_.clear(); ImGui::OpenPopup("##label"); }
                ImGui::PopID();
            }
            if (ImGui::BeginPopup("##label")) {
                ImGui::TextUnformatted("Name this address (e.g. gold, hp):");
                ImGui::SetNextItemWidth(220 * s); bool ent = ImGui::InputText("##lab", &newLabel_, ImGuiInputTextFlags_EnterReturnsTrue);
                bool unique = heng::findWatch(watch_, newLabel_) == nullptr && !heng::trimCopy(newLabel_).empty();
                ImGui::BeginDisabled(!unique);
                if ((ui::button("Add to watch list", ImVec2(0, 0), ui::col::pink, true) || ent) && unique && labelForHit_ >= 0 && labelForHit_ < (int)hits.size()) {
                    heng::WatchEntry we; we.label = heng::trimCopy(newLabel_); we.address = hits[labelForHit_].addr; we.type = vt; watch_.push_back(we); ImGui::CloseCurrentPopup(); ui::toast(Toast::Success, "Watching \"" + we.label + "\"");
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::EndTable();
        }
    }
    ui::endPanel();
    ui::beginPanel("##watch", ImVec2(0, h - scanH - ImGui::GetStyle().ItemSpacing.y), "Watch list  (the AI can only use these labels)", ui::col::yellow, false);
    if (watch_.empty()) ImGui::TextDisabled("Empty. Add addresses from scan results.");
    else if (ImGui::BeginTable("##wt", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupColumn("Label"); ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 60 * s); ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150 * s); ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 90 * s);
        ImGui::TableSetupColumn("Set value", ImGuiTableColumnFlags_WidthFixed, 200 * s); ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150 * s);
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        int del = -1;
        for (size_t i = 0; i < watch_.size(); ++i) {
            auto& we = watch_[i]; ImGui::TableNextRow(); ImGui::PushID((int)i);
            ImGui::TableSetColumnIndex(0); ImGui::PushFont(ui::fonts.bold); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::yellow), "%s", we.label.c_str()); ImGui::PopFont();
            ImGui::TableSetColumnIndex(1); ImGui::TextDisabled("%s", heng::valueTypeName(we.type));
            ImGui::TableSetColumnIndex(2); ImGui::PushFont(ui::fonts.mono); ImGui::Text("0x%llX", (unsigned long long)we.address); ImGui::PopFont();
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(we.lastText.c_str());
            ImGui::TableSetColumnIndex(4);
            std::string& buf = writeBuf_[we.label]; ImGui::SetNextItemWidth(90 * s); ImGui::InputText("##nv", &buf); ImGui::SameLine();
            if (ui::button("Write", ImVec2(0, 0), ui::col::cyan) && mem_) { std::string e; if (heng::writeValue(*mem_, we.address, we.type, buf, e)) { ui::toast(Toast::Success, we.label + " = " + buf); if (we.frozen) { std::vector<uint8_t> nb; std::string e2; heng::parseValue(we.type, buf, nb, e2); we.freezeBytes = nb; } } else ui::toast(Toast::Error, e); }
            ImGui::TableSetColumnIndex(5);
            bool fr = we.frozen; if (ImGui::Checkbox("freeze", &fr) && mem_) { we.frozen = fr; if (fr) { std::vector<uint8_t> b; if (heng::readValue(*mem_, we.address, we.type, b)) we.freezeBytes = b; if (!buf.empty()) { std::vector<uint8_t> nb; std::string e; if (heng::parseValue(we.type, buf, nb, e)) we.freezeBytes = nb; } } }
            ImGui::SameLine(); if (ui::iconButton("##del", Icon::Trash, ui::col::red, 28 * s, "Remove")) del = (int)i;
            ImGui::PopID();
        }
        if (del >= 0) watch_.erase(watch_.begin() + del);
        ImGui::EndTable();
    }
    ui::endPanel();
    ImGui::EndChild();
}

// ----------------------------------------------------------------- Output page
std::vector<std::string> App::listOutputs() const {
    std::vector<std::string> r; std::error_code ec;
    std::string dir = outputBase() + "/" + heng::baseNameOf(ws_.root().empty() ? std::string("target") : ws_.root());
    for (fs::directory_iterator it(P(dir), ec), end; it != end && !ec; it.increment(ec)) { std::error_code e2; if (it->is_directory(e2)) r.push_back(S(it->path())); }
    std::sort(r.begin(), r.end(), std::greater<std::string>());
    return r;
}

void App::pageOutput(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f;
    if (ImGui::GetTime() - outputsAt_ > 2.0) { outputsCache_ = listOutputs(); outputsAt_ = ImGui::GetTime(); }
    float leftW = std::max(340 * s, w * 0.38f);
    ui::beginPanel("##outlist", ImVec2(leftW, h), "Saved results", ui::col::yellow, true);
    if (outputsCache_.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextDisabled("Nothing here yet. Apply a plan and the modified files appear in a timestamped folder."); ImGui::PopTextWrapPos(); }
    for (size_t i = 0; i < outputsCache_.size(); ++i) {
        std::string name = heng::baseNameOf(outputsCache_[i]);
        if (ImGui::Selectable(name.c_str(), selOutput_ == outputsCache_[i], 0, ImVec2(0, ImGui::GetFontSize() * 1.8f))) selOutput_ = outputsCache_[i];
    }
    ui::endPanel();
    ImGui::SameLine();
    ui::beginPanel("##outdetail", ImVec2(0, h), "Details", ui::col::cyan, true);
    if (selOutput_.empty()) ImGui::TextDisabled("Select a result on the left.");
    else {
        ImGui::PushFont(ui::fonts.mono); ImGui::PushTextWrapPos(0); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::cyan), "%s", selOutput_.c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont();
        std::error_code ec; int n = 0; uint64_t total = 0; std::vector<std::string> files;
        for (fs::recursive_directory_iterator it(P(selOutput_), ec), end; it != end && !ec; it.increment(ec)) { std::error_code e2; if (it->is_regular_file(e2)) { ++n; total += it->file_size(e2); if (files.size() < 200) files.push_back(S(fs::relative(it->path(), P(selOutput_), e2))); } }
        ImGui::TextDisabled("%d file(s), %s", n, heng::humanSize(total).c_str());
        if (ui::button("Open folder", ImVec2(0, 0), ui::col::yellow, false, Icon::Folder)) platform::openInFileManager(selOutput_);
        ImGui::SameLine();
        ImGui::BeginDisabled(ws_.root().empty());
        if (ui::button("Install into game...", ImVec2(0, 0), ui::col::orange, true, Icon::Bolt)) ImGui::OpenPopup("##instOut");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(ws_.root().empty());
        if (ui::button("Restore originals", ImVec2(0, 0), ui::col::red, false, Icon::Refresh)) ImGui::OpenPopup("##restoreOut");
        ImGui::EndDisabled();
        if (!installMsg_.empty()) { ImGui::Dummy(ImVec2(0, 4)); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::green), "%s", installMsg_.c_str()); }
        ImGui::Dummy(ImVec2(0, 6));
        ui::sectionLabel("Files", ui::col::purple);
        ImGui::PushFont(ui::fonts.mono);
        for (auto& f : files) ImGui::TextUnformatted(f.c_str());
        ImGui::PopFont();
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
        if (ImGui::BeginPopupModal("##instOut", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            ui::sectionLabel("Install into the game folder?", ui::col::orange);
            ImGui::Text("Files overwrite the originals in:"); ImGui::PushFont(ui::fonts.mono); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::cyan), "%s", ws_.root().c_str()); ImGui::PopFont();
            ImGui::TextDisabled(cfg_.backupOnInstall ? "Originals are kept as <file>.hebak." : "Backups are OFF in Settings.");
            ImGui::Dummy(ImVec2(0, 6));
            if (ui::button("Install", ImVec2(130 * s, 0), ui::col::orange, true, Icon::Bolt)) { ImGui::CloseCurrentPopup(); InstallResult ir = Workspace::installOutputs(selOutput_, ws_.root(), cfg_.backupOnInstall); installMsg_ = ir.ok ? "Installed " + std::to_string(ir.installed.size()) + " file(s), " + std::to_string(ir.backedUp) + " backed up." : "Install failed: " + ir.error; ui::toast(ir.ok ? Toast::Success : Toast::Error, installMsg_); }
            ImGui::SameLine(); if (ui::button("Cancel", ImVec2(110 * s, 0), ui::col::purple)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
        if (ImGui::BeginPopupModal("##restoreOut", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
            size_t nb = Workspace::findBackups(ws_.root()).size();
            ui::sectionLabel("Restore the original files?", ui::col::red);
            ImGui::Text("%zu backup(s) (.hebak) found in the game folder will be copied back.", nb);
            ImGui::Dummy(ImVec2(0, 6));
            if (ui::button("Restore", ImVec2(130 * s, 0), ui::col::red, true, Icon::Refresh)) { ImGui::CloseCurrentPopup(); std::string e; int c = Workspace::restoreBackups(ws_.root(), e); installMsg_ = e.empty() ? "Restored " + std::to_string(c) + " file(s)." : e; ui::toast(e.empty() ? Toast::Success : Toast::Error, installMsg_); }
            ImGui::SameLine(); if (ui::button("Cancel", ImVec2(110 * s, 0), ui::col::purple)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    ui::endPanel();
}

// ----------------------------------------------------------------- About
void App::pageAbout(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f;
    ui::beginPanel("##about", ImVec2(0, h), nullptr, ui::col::pink, true);
    ImGui::Dummy(ImVec2(0, 20 * s));
    ImVec2 p = ImGui::GetCursorScreenPos(); float cw = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList(); float tile = 120 * s;
    ImVec2 a(p.x + (cw - tile) * .5f, p.y), b(a.x + tile, a.y + tile);
    float bob = std::sin((float)ImGui::GetTime() * 1.4f) * 3.f; a.y += bob; b.y += bob;
    for (int g = 4; g >= 1; --g) dl->AddRect(ImVec2(a.x - g * 3, a.y - g * 3), ImVec2(b.x + g * 3, b.y + g * 3), ui::col::withAlpha(ui::col::yellow, 0.05f * (5 - g)), 26 * s + g * 3, 0, 3.f);
    dl->AddRectFilled(a, b, IM_COL32(243, 229, 171, 255), 26 * s);
    dl->AddRectFilled(ImVec2(a.x + 3, a.y + 3), ImVec2(b.x - 3, a.y + tile * 0.45f), IM_COL32(255, 250, 220, 90), 23 * s, ImDrawFlags_RoundCornersTop);
    dl->AddRect(a, b, ui::col::withAlpha(ui::col::yellow, 0.95f), 26 * s, 0, 3.f);
    ImGui::PushFont(ui::fonts.logo); ImVec2 ts = ImGui::CalcTextSize(":)");
    dl->AddText(ui::fonts.logo, ui::fonts.logo->FontSize, ImVec2(a.x + (tile - ts.x) * .5f, a.y + (tile - ts.y) * .5f - 3), IM_COL32(58, 38, 24, 255), ":)"); ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, tile + 18 * s));
    const char* name = "Happy Engineering";
    ImGui::PushFont(ui::fonts.title); ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(name).x) * .5f); ui::gradientText(name, ui::col::pink, ui::col::orange, ui::fonts.title); ImGui::PopFont();
    std::string v = std::string("version 1.0 · ") + platform::osName() + (sizeof(void*) == 8 ? " 64-bit" : " 32-bit");
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(v.c_str()).x) * .5f); ImGui::TextDisabled("%s", v.c_str());
    ImGui::Dummy(ImVec2(0, 14 * s));
    float colW = std::min(620.f * s, cw - 40);
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - colW) * .5f);
    ImGui::BeginGroup(); ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + colW);
    ImGui::TextUnformatted("An AI-driven reverse-engineering and game-modding assistant. Happy explores your target with read-only tools, then proposes a plan of concrete changes - text edits, byte patches, image re-texturing (also inside .zip/.jar/.apk/.pk3 archives) and memory values - that you review before anything is written.");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextDisabled("It does not inject code, hook processes, bypass DRM/anti-cheat, or help with cheating in online games. Use it on software you have the right to modify.");
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextDisabled("Source & releases: github.com/dim-gray/happy-engineering  ·  MIT license");
    ImGui::PopTextWrapPos(); ImGui::EndGroup();
    ui::endPanel();
}

} // namespace he
