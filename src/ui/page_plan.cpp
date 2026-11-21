#include "app.h"
#include "theme.h"
#include "app/fsutil.h"
#include <algorithm>
#include <sstream>

namespace he {
using namespace ui;

static std::string targetFolderName(const std::string& root) {
    std::string n = heng::baseNameOf(root);
    if (n.empty()) n = "target";
    for (char& c : n) if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '\\') c = '_';
    return n;
}
std::string App::outputBase() const {
    std::string base = cfg_.outputDir.empty() ? platform::documentsDir() + "/HappyEngineering/Output" : cfg_.outputDir;
    return base;
}

void App::applyIncomingPlan(const heng::Plan& p) {
    plan_ = p; selected_.assign(p.actions.size(), true);
    for (size_t i = 0; i < p.actions.size(); ++i) if (!p.actions[i].error.empty()) selected_[i] = false;
    selAction_ = p.actions.empty() ? -1 : 0; hasReport_ = false; lastSave_ = SaveResult(); planNotice_.clear();
    if (!p.actions.empty()) { ui::toast(Toast::Success, "Plan ready: " + std::to_string(p.actions.size()) + " action(s)"); page_ = Page::Plan; }
    else if (!p.error.empty()) ui::toast(Toast::Error, "The plan could not be read: " + p.error);
}

static std::string reportLog(const heng::Plan& plan, const heng::ExecutionReport& rep) {
    std::ostringstream o;
    o << "Happy Engineering log\nSummary: " << plan.summary << "\n\n";
    for (size_t i = 0; i < plan.actions.size() && i < rep.results.size(); ++i) {
        const auto& r = rep.results[i];
        o << (r.skipped ? "[SKIP] " : r.ok ? "[ OK ] " : "[FAIL] ") << heng::describeAction(plan.actions[i]) << "\n       " << r.message;
        if (!r.warning.empty()) o << "\n       warning: " << r.warning;
        o << "\n";
    }
    return o.str();
}

void App::runVerify() {
    if (plan_.actions.empty()) return;
    report_ = heng::runPlan(plan_, selected_, ws_, watch_, nullptr, false, &imgconv_);
    hasReport_ = true; lastWasApply_ = false;
    ui::toast(report_.failCount ? Toast::Warn : Toast::Success, "Check done: " + std::to_string(report_.okCount) + " ok, " + std::to_string(report_.failCount) + " problem(s)");
}

void App::runApply() {
    if (plan_.actions.empty()) return;
    bool anyMem = false;
    for (size_t i = 0; i < plan_.actions.size(); ++i) if (i < selected_.size() && selected_[i] && plan_.actions[i].type == heng::ActionType::MemWrite) anyMem = true;
    report_ = heng::runPlan(plan_, selected_, ws_, watch_, mem_.get(), anyMem, &imgconv_);
    hasReport_ = true; lastWasApply_ = true;
    if (!report_.outputs.empty()) {
        lastSave_ = ws_.saveOutputs(report_.outputs, outputBase() + "/" + targetFolderName(ws_.root()), reportLog(plan_, report_));
        if (lastSave_.ok) ui::toast(Toast::Success, "Saved " + std::to_string(lastSave_.files.size()) + " file(s) to Output");
        else ui::toast(Toast::Error, "Save failed: " + lastSave_.error);
    } else if (!anyMem) ui::toast(Toast::Warn, "Nothing was written (no successful file changes).");
    else ui::toast(Toast::Success, "Memory values written.");
    outputsAt_ = -100;
}

static void diffBlock(const char* sign, const std::string& text, ImU32 color) {
    ImGui::PushFont(ui::fonts.mono);
    ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
    ImVec2 ts = ImGui::CalcTextSize(text.c_str(), nullptr, false, w - 40);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + ts.y + 12), ui::col::withAlpha(color, 0.10f), 8.f);
    dl->AddRectFilled(p, ImVec2(p.x + 4, p.y + ts.y + 12), color, 2.f);
    dl->AddText(ImVec2(p.x + 12, p.y + 6), color, sign);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 34, p.y + 6));
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + w - 44); ImGui::TextUnformatted(text.empty() ? "(empty)" : text.c_str()); ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + ts.y + 16));
    ImGui::PopFont();
}
static std::string hexOf(const std::vector<uint8_t>& v) { std::string s; char b[4]; for (size_t i = 0; i < v.size(); ++i) { std::snprintf(b, sizeof b, "%02X ", v[i]); s += b; } return s; }

void App::drawActionDetail(const heng::PlanAction& a, const heng::ActionResult* r) {
    ImGui::PushFont(ui::fonts.bold); ImGui::TextUnformatted(heng::actionTypeLabel(a.type)); ImGui::PopFont();
    ImGui::PushTextWrapPos(0);
    if (!a.file.empty()) { ImGui::PushFont(ui::fonts.mono); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::cyan), "%s", a.file.c_str()); ImGui::PopFont(); }
    if (!a.note.empty()) ImGui::TextDisabled("Why: %s", a.note.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, 4));
    if (!a.error.empty()) { ui::badge("INVALID", ui::col::red); ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::red), "%s", a.error.c_str()); }
    switch (a.type) {
        case heng::ActionType::ReplaceText:
            ImGui::TextDisabled("%s  ·  %s", a.encoding.c_str(), a.all ? "every match" : "first match only");
            diffBlock("-", a.find, ui::col::red); ImGui::Dummy(ImVec2(0, 2)); diffBlock("+", a.replace, ui::col::green); break;
        case heng::ActionType::PatchBytes: {
            char off[32]; std::snprintf(off, sizeof off, "0x%llX", (unsigned long long)a.offset); ImGui::TextDisabled("at offset %s", off);
            diffBlock("-", hexOf(a.expect), ui::col::red); ImGui::Dummy(ImVec2(0, 2)); diffBlock("+", hexOf(a.write), ui::col::green); break; }
        case heng::ActionType::ReplaceImage:
            ImGui::Text("Attachment: %s", a.attachment.c_str()); ImGui::TextDisabled("Auto-converted to the original's pixel size and format (fit: %s).", a.fit.c_str()); break;
        case heng::ActionType::ReplaceFile:
            ImGui::Text("Attachment: %s", a.attachment.c_str()); ImGui::TextDisabled("Raw swap: the file is replaced byte for byte."); break;
        case heng::ActionType::MemWrite:
            ImGui::Text("Label: %s", a.label.c_str()); ImGui::Text("New value: %s", a.value.c_str()); break;
        default: break;
    }
    if (r) {
        ImGui::Dummy(ImVec2(0, 6));
        ImU32 c = r->skipped ? ui::col::muted : r->ok ? ui::col::green : ui::col::red;
        ui::badge(r->skipped ? "SKIPPED" : r->ok ? (lastWasApply_ ? "APPLIED" : "WOULD WORK") : "FAILED", c);
        ImGui::PushTextWrapPos(0); ImGui::SameLine(); ImGui::TextWrapped("%s", r->message.c_str());
        if (!r->warning.empty()) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::yellow), "Warning: %s", r->warning.c_str());
        ImGui::PopTextWrapPos();
    }
}

void App::pagePlan(float w, float h) {
    float s = ImGui::GetFontSize() / 17.f;
    if (plan_.actions.empty()) {
        ui::beginPanel("##noplan", ImVec2(0, h), nullptr, ui::col::purple, false);
        ImGui::Dummy(ImVec2(0, h * 0.25f));
        const char* t1 = plan_.error.empty() ? "No plan yet" : "The last plan could not be read";
        ImGui::PushFont(ui::fonts.title); ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(t1).x) * .5f); ImGui::TextUnformatted(t1); ImGui::PopFont();
        const char* t2 = plan_.error.empty() ? "Ask Happy for a change on the Workspace page. The proposed edits appear here for your review." : plan_.error.c_str();
        ImGui::SetCursorPosX(std::max(20.f, (ImGui::GetWindowWidth() - ImGui::CalcTextSize(t2).x) * .5f)); ImGui::TextDisabled("%s", t2);
        ImGui::Dummy(ImVec2(0, 10)); ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 190 * s) * .5f);
        if (ui::button("Go to Workspace", ImVec2(190 * s, 0), ui::col::pink, true, Icon::Workspace)) page_ = Page::Workspace;
        ui::endPanel(); return;
    }
    float footH = 118 * s;
    ui::beginPanel("##planhead", ImVec2(0, h - footH - ImGui::GetStyle().ItemSpacing.y), nullptr, ui::col::pink, false);
    ImGui::PushFont(ui::fonts.bold); ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(plan_.summary.empty() ? "Proposed changes" : plan_.summary.c_str()); ImGui::PopTextWrapPos(); ImGui::PopFont();
    int nsel = 0; for (bool b : selected_) nsel += b;
    ImGui::TextDisabled("%d of %d actions selected", nsel, (int)plan_.actions.size());
    ImGui::SameLine(); if (ui::button("All", ImVec2(0, 0), ui::col::cyan)) for (size_t i = 0; i < selected_.size(); ++i) selected_[i] = plan_.actions[i].error.empty();
    ImGui::SameLine(); if (ui::button("None", ImVec2(0, 0), ui::col::purple)) std::fill(selected_.begin(), selected_.end(), false);
    ImGui::Dummy(ImVec2(0, 2));
    float bodyH = ImGui::GetContentRegionAvail().y;
    float listW = std::max(380 * s, ImGui::GetContentRegionAvail().x * 0.52f);
    ImGui::BeginChild("##actlist", ImVec2(listW, bodyH), ImGuiChildFlags_None);
    if (ImGui::BeginTable("##acts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28 * s);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 84 * s);
        ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 74 * s);
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        for (size_t i = 0; i < plan_.actions.size(); ++i) {
            const auto& a = plan_.actions[i];
            ImGui::TableNextRow(); ImGui::PushID((int)i);
            ImGui::TableSetColumnIndex(0); bool v = i < selected_.size() && selected_[i]; ImGui::BeginDisabled(!a.error.empty());
            if (ImGui::Checkbox("##sel", &v)) selected_[i] = v; ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(1);
            ImU32 tc = a.type == heng::ActionType::ReplaceText ? ui::col::cyan : a.type == heng::ActionType::ReplaceImage ? ui::col::pink : a.type == heng::ActionType::MemWrite ? ui::col::yellow : ui::col::purple;
            ui::badge(heng::actionTypeLabel(a.type), tc);
            ImGui::TableSetColumnIndex(2);
            std::string d = heng::describeAction(a);
            if (ImGui::Selectable(d.c_str(), selAction_ == (int)i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) selAction_ = (int)i;
            ImGui::TableSetColumnIndex(3);
            if (!a.error.empty()) ui::badge("invalid", ui::col::red);
            else if (hasReport_ && i < report_.results.size()) { const auto& r = report_.results[i]; ui::badge(r.skipped ? "skipped" : r.ok ? (lastWasApply_ ? "done" : "ok") : "failed", r.skipped ? ui::col::muted : r.ok ? ui::col::green : ui::col::red); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##actdetail", ImVec2(0, bodyH), ImGuiChildFlags_None);
    if (selAction_ >= 0 && selAction_ < (int)plan_.actions.size()) drawActionDetail(plan_.actions[selAction_], hasReport_ && selAction_ < (int)report_.results.size() ? &report_.results[selAction_] : nullptr);
    ImGui::EndChild();
    ui::endPanel();

    // footer: actions
    ui::beginPanel("##planfoot", ImVec2(0, footH), nullptr, ui::col::orange, false);
    if (ui::button("Check (dry run)", ImVec2(0, 0), ui::col::cyan, false, Icon::Eye)) runVerify();
    ImGui::SameLine();
    bool hasSel = nsel > 0;
    ImGui::BeginDisabled(!hasSel);
    if (ui::button("Apply to Output", ImVec2(0, 0), ui::col::pink, true, Icon::Check)) {
        bool anyMem = false; for (size_t i = 0; i < plan_.actions.size(); ++i) if (selected_[i] && plan_.actions[i].type == heng::ActionType::MemWrite) anyMem = true;
        if (cfg_.confirmBeforeApply || anyMem) { confirmMem_ = anyMem; ImGui::OpenPopup("##confirmApply"); } else runApply();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    bool canOpen = lastSave_.ok;
    ImGui::BeginDisabled(!canOpen);
    if (ui::button("Open output folder", ImVec2(0, 0), ui::col::yellow, false, Icon::Folder)) platform::openInFileManager(lastSave_.dir);
    ImGui::SameLine();
    if (ui::button("Install into game...", ImVec2(0, 0), ui::col::orange, false, Icon::Bolt)) ImGui::OpenPopup("##confirmInstall");
    ImGui::EndDisabled();
    if (lastSave_.ok) { ImGui::PushFont(ui::fonts.small); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::green), "Saved to %s", lastSave_.dir.c_str()); ImGui::PopFont(); }
    else if (!installMsg_.empty()) ImGui::TextDisabled("%s", installMsg_.c_str());
    else ImGui::TextDisabled("Nothing is written to your game until you press Install.");
    // popups
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    if (ImGui::BeginPopupModal("##confirmApply", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ui::sectionLabel("Apply these changes?", ui::col::pink);
        ImGui::Text("%d action(s) will be written to the Output folder.", nsel);
        ImGui::TextDisabled("Your game files stay untouched until you press Install.");
        if (confirmMem_) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::yellow), "This plan also writes values into the running process's memory.");
        ImGui::Dummy(ImVec2(0, 6));
        if (ui::button("Apply", ImVec2(130 * s, 0), ui::col::pink, true, Icon::Check)) { ImGui::CloseCurrentPopup(); runApply(); }
        ImGui::SameLine(); if (ui::button("Cancel", ImVec2(110 * s, 0), ui::col::purple)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    if (ImGui::BeginPopupModal("##confirmInstall", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        ui::sectionLabel("Install into the game folder?", ui::col::orange);
        ImGui::Text("%d file(s) will overwrite the originals in:", (int)lastSave_.files.size());
        ImGui::PushFont(ui::fonts.mono); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::cyan), "%s", ws_.root().c_str()); ImGui::PopFont();
        ImGui::TextDisabled(cfg_.backupOnInstall ? "Originals are kept as <file>.hebak (first time only)." : "Backups are turned OFF in Settings.");
        ImGui::Dummy(ImVec2(0, 6));
        if (ui::button("Install", ImVec2(130 * s, 0), ui::col::orange, true, Icon::Bolt)) {
            ImGui::CloseCurrentPopup();
            InstallResult ir = Workspace::installOutputs(lastSave_.dir, ws_.root(), cfg_.backupOnInstall);
            installMsg_ = ir.ok ? "Installed " + std::to_string(ir.installed.size()) + " file(s), " + std::to_string(ir.backedUp) + " backed up." : "Install failed: " + ir.error;
            ui::toast(ir.ok ? Toast::Success : Toast::Error, installMsg_);
        }
        ImGui::SameLine(); if (ui::button("Cancel", ImVec2(110 * s, 0), ui::col::purple)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ui::endPanel();
}

} // namespace he
