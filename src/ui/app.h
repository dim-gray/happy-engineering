// app.h -- Happy Engineering UI application (ImGui only; no GLFW/GL here).
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "core/profiles.h"
#include "core/target.h"
#include "core/scanner.h"
#include "core/executor.h"
#include "app/workspace.h"
#include "app/ai_session.h"
#include "app/imgconv.h"
#include "app/toolbox.h"
#include "platform/platform.h"

namespace he {

enum class Page { Workspace = 0, Plan, Memory, Tools, Output, About };

// ---- in-app file browser (works the same on Windows and Linux) ---------------
class FileBrowser {
public:
    enum class Mode { OpenFile, OpenFolder };
    void open(const std::string& title, Mode mode, const std::string& startDir, int purpose);
    // Draws the modal. Returns true on the frame a path was chosen.
    bool draw(std::string& chosen, int& purposeOut);
    bool isOpen() const { return open_; }
private:
    void refresh();
    bool open_ = false, wantOpen_ = false, showHidden_ = false;
    Mode mode_ = Mode::OpenFile; int purpose_ = 0;
    std::string title_, dir_, pathInput_, filter_, selected_;
    struct Row { std::string name; bool isDir; uint64_t size; };
    std::vector<Row> rows_;
    std::string err_;
};

struct ProcPick { uint32_t pid = 0; std::string name; };

class App {
public:
    App();
    ~App();
    void init();                                   // load config, set up state
    void frame(ImVec2 display);                    // draw one UI frame (inside ImGui::NewFrame/Render)
    void onDrop(const std::vector<std::string>& paths);
    bool consumeScaleChange(float& newScale);      // true when fonts must be rebuilt at newScale
    void setDpiScale(float s) { dpi_ = s; }
    float effectiveScale() const { return dpi_ * cfg_.uiScale; }
    void shutdown();                               // save config, stop threads
    bool wantsQuit() const { return quit_; }

    // test / screenshot hooks
    void debugSetPage(Page p) { page_ = p; }
    void debugOpenSettings(int tab) { settingsOpen_ = true; settingsTab_ = tab; }
    void debugLoadDemo();
    void debugToolsTab(int t) { toolsTab_ = t; }
    heng::AppConfig& config() { return cfg_; }

private:
    // layout
    void drawSidebar(float w, float h);
    void drawTopBar(float w);
    void drawSettingsWindow(ImVec2 display);
    // pages
    void pageWorkspace(float w, float h);
    void pagePlan(float w, float h);
    void pageMemory(float w, float h);
    void pageTools(float w, float h);
    void pageOutput(float w, float h);
    void pageAbout(float w, float h);
    // workspace helpers
    void drawTargetPanel(float h);
    void drawAttachmentsPanel(float h);
    void drawAskPanel(float h);
    void drawFeed(float h);
    void drawProcessPicker(float h, bool forMemory);
    void setTargetPath(const std::string& path);
    void setTargetProcess(const ProcPick& p);
    void clearTarget();
    void sendToAi(const std::string& text);
    std::string buildContext(std::vector<heng::ImagePart>& images);
    bool aiReady(std::string& why) const;
    // plan helpers
    void applyIncomingPlan(const heng::Plan& p);
    void runVerify();
    void runApply();
    void drawActionDetail(const heng::PlanAction& a, const heng::ActionResult* r);
    // memory helpers
    void attachMemory(const ProcPick& p);
    void startScan(bool first);
    void tickMemory();
    // tools helpers
    void initToolbox();
    void drawInstallApproval();
    void toolsListPanel(float w, float h);
    void toolsDecompilePanel(float w, float h);
    void toolsBuildPanel(float w, float h);
    std::string buildsBase() const;
    void useCodeInTools(const std::string& code, const std::string& lang);
    std::string latestAiCode(std::string* lang) const;
    std::string chatAsText() const;
    // output helpers
    std::string outputBase() const;
    std::vector<std::string> listOutputs() const;
    // settings
    void saveConfig();
    void markDirty() { cfgDirty_ = true; cfgDirtyAt_ = ImGui::GetTime(); }
    void settingsProfiles();
    void settingsPersonality();
    void settingsBehavior();
    void settingsAppearance();
    void settingsStorage();
    void testConnection();
    void newProfile(heng::Provider p);

    // ---- state ----
    heng::AppConfig cfg_;
    bool cfgDirty_ = false; double cfgDirtyAt_ = 0;
    std::string cfgPath_;
    float dpi_ = 1.f, appliedScale_ = 0.f;
    bool quit_ = false;
    Page page_ = Page::Workspace; Page lastPage_ = Page::Workspace;

    // settings window
    bool settingsOpen_ = false; int settingsTab_ = 0; int selProfile_ = 0; bool showKey_ = false;
    double settingsOpenedAt_ = 0; float settingsAnim_ = 0.f;
    struct TestState { std::atomic<int> state{0}; std::string msg; std::mutex mu; std::thread th; } test_;
    std::string personalityBuf_; bool personalityLoaded_ = false;

    // target / workspace
    heng::Target target_; int targetKind_ = 1;           // 0 file 1 folder 2 html 3 process
    std::string pathBuf_;
    Workspace ws_; ImageConverter imgconv_;
    std::vector<platform::ProcessInfo> procs_; std::string procFilter_; double procRefreshAt_ = -100;
    uint32_t pickedPid_ = 0; std::string pickedName_;
    std::string targetSummary_;
    FileBrowser browser_;
    std::string prompt_;
    AiSession ai_;
    std::vector<double> feedBorn_; size_t feedSeen_ = 0; bool feedStick_ = true;

    // plan
    heng::Plan plan_; std::vector<bool> selected_; int selAction_ = -1;
    bool hasReport_ = false; heng::ExecutionReport report_; bool lastWasApply_ = false;
    SaveResult lastSave_; std::string planNotice_;
    bool confirmInstall_ = false, confirmMem_ = false, confirmRestore_ = false;
    std::string installMsg_;

    // memory
    std::unique_ptr<heng::IMemory> mem_; uint32_t memPid_ = 0; std::string memName_, memErr_;
    std::unique_ptr<heng::Scanner> scanner_;
    int scanType_ = 2, scanMode_ = 0; std::string scanValue_ = "100";
    std::vector<heng::WatchEntry> watch_;
    std::string newLabel_; int labelForHit_ = -1;
    std::atomic<bool> scanning_{false}; std::thread scanTh_; std::string scanMsg_;
    double lastWatchRead_ = 0, lastFreeze_ = 0;
    std::map<std::string, std::string> writeBuf_;

    // tools page
    Toolbox toolbox_; bool toolboxReady_ = false; std::unique_ptr<IDownloader> downloader_; bool wantApprovalPopup_ = false;
    std::string toolPickId_;
    std::string dcInput_, dcFunc_, dcTool_;            // decompile form (dcTool_ "" = auto)
    std::string dcShown_; std::string dcShownFile_;    // text currently displayed
    std::string srcBuf_, srcName_ = "patched", srcFlags_ = "-O2 -Wall", srcLang_ = "c", cpTool_;
    int cpBits_ = 0; std::string buildNote_, lastLogSeen_;
    bool buildWasFix_ = false; int toolsTab_ = 0; std::string dcToken_;

    // output page
    std::string selOutput_; std::vector<std::string> outputsCache_; double outputsAt_ = -100;
};

} // namespace he
