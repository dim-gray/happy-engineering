#include "ai_session.h"
#include "platform/platform.h"
#include <chrono>

namespace he {

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

AiSession::AiSession() {}
AiSession::~AiSession() { cancel_ = true; if (th_.joinable()) th_.join(); }

void AiSession::addLog(LogKind k, const std::string& title, const std::string& text) {
    std::lock_guard<std::mutex> lk(mu_);
    LogItem it; it.kind = k; it.title = title; it.text = text; it.time = nowSec();
    log_.push_back(it);
}
std::vector<LogItem> AiSession::log() const { std::lock_guard<std::mutex> lk(mu_); return log_; }
size_t AiSession::logSize() const { std::lock_guard<std::mutex> lk(mu_); return log_.size(); }
bool AiSession::hasHistory() const { std::lock_guard<std::mutex> lk(mu_); return !hist_.empty(); }
std::string AiSession::lastError() const { std::lock_guard<std::mutex> lk(mu_); return error_; }
bool AiSession::takePlan(heng::Plan& out) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!planReady_) return false;
    out = plan_; planReady_ = false; return true;
}
void AiSession::cancel() { cancel_ = true; }
void AiSession::reset() {
    cancel_ = true;
    if (th_.joinable()) th_.join();
    cancel_ = false;
    std::lock_guard<std::mutex> lk(mu_);
    hist_.clear(); log_.clear(); planReady_ = false; error_.clear();
    state_ = SessionState::Idle; step_ = 0;
}

void AiSession::send(const SessionSettings& s, Workspace* ws, const std::string& userText,
                     const std::string& contextText, const std::vector<heng::ImagePart>& images) {
    if (busy()) return;
    if (th_.joinable()) th_.join();
    cancel_ = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        heng::ChatMessage m; m.role = "user"; m.images = images;
        m.text = hist_.empty() && !contextText.empty() ? userText + "\n\n---\n" + contextText : userText;
        hist_.push_back(m);
        error_.clear(); planReady_ = false;
    }
    addLog(LogKind::User, "You", userText);
    state_ = SessionState::Thinking; step_ = 0;
    th_ = std::thread([this, s, ws]() { run(s, ws); });
}

void AiSession::run(SessionSettings s, Workspace* ws) {
    const std::string system = heng::agentSystemPrompt(s.personality);
    for (int step = 1; step <= s.maxSteps + 1; ++step) {
        if (cancel_) { state_ = SessionState::Cancelled; addLog(LogKind::Info, "Cancelled", "Stopped by you."); return; }
        step_ = step;
        state_ = SessionState::Thinking;
        std::vector<heng::ChatMessage> hist;
        { std::lock_guard<std::mutex> lk(mu_); hist = hist_; }
        if (step == s.maxSteps + 1) {
            heng::ChatMessage m; m.role = "user";
            m.text = "Tool step limit reached. Do not call more tools. Using what you have learned, either give your final answer / plan now, or explain exactly what is still missing.";
            hist.push_back(m);
            std::lock_guard<std::mutex> lk(mu_); hist_.push_back(m);
        }
        heng::ApiRequest req = heng::buildChatRequest(s.api, system, hist, s.maxTokens);
        ++requests_;
        platform::HttpResult http = platform::httpPost(req, 600, &cancel_);
        if (cancel_) { state_ = SessionState::Cancelled; addLog(LogKind::Info, "Cancelled", "Stopped by you."); return; }
        if (!http.sent) { { std::lock_guard<std::mutex> lk(mu_); error_ = http.error; } addLog(LogKind::Error, "Network error", http.error); state_ = SessionState::Failed; return; }
        heng::ApiResult r = heng::parseResponse(s.api.provider, http.status, http.body);
        if (!r.ok) { { std::lock_guard<std::mutex> lk(mu_); error_ = r.error; } addLog(LogKind::Error, "AI error", r.error); state_ = SessionState::Failed; return; }
        {
            heng::ChatMessage m; m.role = "assistant"; m.text = r.text;
            std::lock_guard<std::mutex> lk(mu_); hist_.push_back(m);
        }
        heng::Plan plan = heng::extractPlan(r.text);
        std::vector<heng::ToolCall> calls = heng::extractToolCalls(r.text);
        std::string visible = heng::stripToolBlocks(heng::stripPlanBlock(r.text));
        if (!visible.empty()) addLog(LogKind::Assistant, "Happy", visible);
        if (r.truncated) addLog(LogKind::Info, "Reply was cut off", "The model hit its output limit. Raise 'Max reply tokens' in Settings > API, or ask it to continue.");

        if (plan.found) {
            if (!plan.error.empty()) addLog(LogKind::Error, "Plan problem", plan.error);
            else addLog(LogKind::Info, "Plan ready", std::to_string(plan.actions.size()) + " action(s) - review them on the Plan page.");
            { std::lock_guard<std::mutex> lk(mu_); plan_ = plan; planReady_ = true; }
            state_ = SessionState::Finished; return;
        }
        if (calls.empty()) { state_ = SessionState::Finished; return; }
        if (step == s.maxSteps + 1) { state_ = SessionState::Finished; return; }

        state_ = SessionState::RunningTools;
        std::string results = "TOOL RESULTS\n";
        for (size_t i = 0; i < calls.size(); ++i) {
            if (cancel_) { state_ = SessionState::Cancelled; return; }
            std::string argsText = calls[i].args.isObject() ? calls[i].args.dump() : std::string();
            addLog(LogKind::ToolCall, calls[i].name.empty() ? "tool" : calls[i].name, argsText);
            std::string out = heng::runTool(calls[i], *ws);
            std::string preview = out.size() > 1500 ? out.substr(0, 1500) + "\n..." : out;
            addLog(LogKind::ToolResult, calls[i].name, preview);
            results += "\n### result " + std::to_string(i + 1) + " (" + calls[i].name + ")\n" + out + "\n";
        }
        heng::ChatMessage m; m.role = "user"; m.text = results;
        std::lock_guard<std::mutex> lk(mu_); hist_.push_back(m);
    }
    state_ = SessionState::Finished;
}

} // namespace he
