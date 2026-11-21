#include "app.h"
#include "theme.h"
#include "app/fsutil.h"
#include "core/textutil.h"
#include "core/attachments.h"
#include <algorithm>

namespace he {

void FileBrowser::open(const std::string& title, Mode mode, const std::string& startDir, int purpose) {
    title_ = title; mode_ = mode; purpose_ = purpose; selected_.clear(); filter_.clear();
    std::error_code ec;
    dir_ = startDir;
    if (dir_.empty() || !fs::is_directory(P(dir_), ec)) dir_ = platform::documentsDir();
    pathInput_ = dir_;
    wantOpen_ = true; open_ = true;
    refresh();
}

void FileBrowser::refresh() {
    rows_.clear(); err_.clear();
    std::error_code ec;
    for (fs::directory_iterator it(P(dir_), fs::directory_options::skip_permission_denied, ec), end; it != end && !ec; it.increment(ec)) {
        std::error_code e2;
        std::string name = S(it->path().filename());
        if (!showHidden_ && !name.empty() && name[0] == '.') continue;
        Row r; r.name = name; r.isDir = it->is_directory(e2); r.size = r.isDir ? 0 : (uint64_t)it->file_size(e2);
        rows_.push_back(r);
    }
    if (ec) err_ = ec.message();
    std::sort(rows_.begin(), rows_.end(), [](const Row& a, const Row& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return heng::lowerCopy(a.name) < heng::lowerCopy(b.name);
    });
}

bool FileBrowser::draw(std::string& chosen, int& purposeOut) {
    if (wantOpen_) { ImGui::OpenPopup("##filebrowser"); wantOpen_ = false; }
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, io.DisplaySize.y * .5f), ImGuiCond_Appearing, ImVec2(.5f, .5f));
    float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(std::min(io.DisplaySize.x - 40, fs * 46), std::min(io.DisplaySize.y - 40, fs * 34)), ImGuiCond_Appearing);
    bool result = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs, fs));
    if (ImGui::BeginPopupModal("##filebrowser", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        dl->AddRect(ImVec2(wp.x + 1, wp.y + 1), ImVec2(wp.x + ws.x - 1, wp.y + ws.y - 1), ui::col::pink, 14.f, 0, 1.6f);
        ui::sectionLabel(title_.c_str(), ui::col::cyan);
        // path bar
        if (ui::iconButton("##up", ui::Icon::Folder, ui::col::yellow, fs * 1.9f, "Parent folder")) {
            fs::path p = P(dir_); if (p.has_parent_path() && p.parent_path() != p) { dir_ = S(p.parent_path()); pathInput_ = dir_; refresh(); }
        }
        ImGui::SameLine();
        if (ui::iconButton("##home", ui::Icon::Workspace, ui::col::cyan, fs * 1.9f, "Home")) { dir_ = platform::homeDir(); pathInput_ = dir_; refresh(); }
        ImGui::SameLine();
        if (ui::iconButton("##docs", ui::Icon::File, ui::col::purple, fs * 1.9f, "Documents")) { dir_ = platform::documentsDir(); pathInput_ = dir_; refresh(); }
#ifdef _WIN32
        for (char d = 'C'; d <= 'F'; ++d) {
            std::string root = std::string(1, d) + ":/"; std::error_code ec;
            if (!fs::is_directory(P(root), ec)) continue;
            ImGui::SameLine(); std::string lbl = std::string(1, d) + ":";
            if (ui::button(lbl.c_str(), ImVec2(0, fs * 1.9f), ui::col::purple)) { dir_ = root; pathInput_ = dir_; refresh(); }
        }
#else
        ImGui::SameLine(); if (ui::button("/", ImVec2(0, fs * 1.9f), ui::col::purple)) { dir_ = "/"; pathInput_ = dir_; refresh(); }
#endif
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##path", &pathInput_, ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::error_code ec; if (fs::is_directory(P(pathInput_), ec)) { dir_ = pathInput_; refresh(); }
            else if (fs::is_regular_file(P(pathInput_), ec) && mode_ == Mode::OpenFile) { chosen = pathInput_; purposeOut = purpose_; result = true; open_ = false; ImGui::CloseCurrentPopup(); }
        }
        ImGui::SetNextItemWidth(fs * 14); ImGui::InputTextWithHint("##flt", "filter...", &filter_);
        ImGui::SameLine(); if (ImGui::Checkbox("hidden", &showHidden_)) refresh();
        float footer = fs * 3.4f;
        ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_None);
        std::string fl = heng::lowerCopy(filter_);
        if (!err_.empty()) ImGui::TextColored(ImVec4(1, .3f, .4f, 1), "%s", err_.c_str());
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row& r = rows_[i];
            if (!fl.empty() && heng::lowerCopy(r.name).find(fl) == std::string::npos) continue;
            if (mode_ == Mode::OpenFolder && !r.isDir) continue;
            ImGui::PushID((int)i);
            bool sel = (selected_ == r.name);
            ImVec2 p = ImGui::GetCursorScreenPos();
            std::string label = "      " + r.name;
            if (ImGui::Selectable(label.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)) {
                selected_ = r.name;
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (r.isDir) { dir_ = S(P(dir_) / P(r.name)); pathInput_ = dir_; selected_.clear(); refresh(); ImGui::PopID(); break; }
                    if (mode_ == Mode::OpenFile) { chosen = S(P(dir_) / P(r.name)); purposeOut = purpose_; result = true; open_ = false; ImGui::CloseCurrentPopup(); ImGui::PopID(); break; }
                }
            }
            ui::drawIcon(ImGui::GetWindowDrawList(), r.isDir ? ui::Icon::Folder : ui::Icon::File, ImVec2(p.x + fs * .9f, p.y + fs * .55f), fs * .95f, r.isDir ? ui::col::yellow : ui::col::muted);
            if (!r.isDir) { std::string sz = heng::humanSize(r.size); float tw = ImGui::CalcTextSize(sz.c_str()).x; ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + ImGui::GetContentRegionAvail().x - tw - 6, p.y + ImGui::GetStyle().FramePadding.y * .2f), ui::col::muted, sz.c_str()); }
            ImGui::PopID();
        }
        ImGui::EndChild();
        std::string cur = mode_ == Mode::OpenFolder ? dir_ : (selected_.empty() ? std::string() : S(P(dir_) / P(selected_)));
        ImGui::TextDisabled("%s", cur.empty() ? "Select a file" : cur.c_str());
        bool canPick = mode_ == Mode::OpenFolder ? true : !selected_.empty();
        if (mode_ == Mode::OpenFile && !selected_.empty()) { for (auto& r : rows_) if (r.name == selected_ && r.isDir) canPick = false; }
        if (mode_ == Mode::OpenFolder && !selected_.empty()) { for (auto& r : rows_) if (r.name == selected_ && r.isDir) cur = S(P(dir_) / P(selected_)); }
        ImGui::BeginDisabled(!canPick);
        if (ui::button(mode_ == Mode::OpenFolder ? "Use this folder" : "Open", ImVec2(fs * 9, 0), ui::col::pink, true, ui::Icon::Check)) { chosen = cur; purposeOut = purpose_; result = true; open_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ui::button("Cancel", ImVec2(fs * 7, 0), ui::col::purple) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { open_ = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    return result;
}

} // namespace he
