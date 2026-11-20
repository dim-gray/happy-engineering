// ai_session.h -- runs the agent loop on a worker thread and exposes a
// thread-safe activity log + final plan to the UI.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <memory>
#include "core/chat.h"
#include "core/agent.h"
#include "core/actions.h"
#include "workspace.h"

namespace he {

enum class LogKind { User, Assistant, ToolCall, ToolResult, Info, Error };
struct LogItem { LogKind kind; std::string title, text; double time = 0; };

enum class SessionState { Idle, Thinking, RunningTools, Finished, Failed, Cancelled };

struct SessionSettings {
    heng::ApiConfig api;
    int maxTokens = 8192;
    int maxSteps = 12;
    std::string personality;   // custom, may be empty
};

class AiSession {
public:
    AiSession();
    ~AiSession();
    // Starts (or continues, when history exists) a conversation turn.
    // `contextText` is prepended to the user's message on the FIRST turn only.
    void send(const SessionSettings& s, Workspace* ws, const std::string& userText,
              const std::string& contextText, const std::vector<heng::ImagePart>& images);
    void cancel();
    void reset();                         // forget the conversation
    bool busy() const { SessionState st = state_.load(); return st == SessionState::Thinking || st == SessionState::RunningTools; }
    SessionState state() const { return state_.load(); }
    int step() const { return step_.load(); }
    bool hasHistory() const;

    std::vector<LogItem> log() const;     // copy
    size_t logSize() const;
    bool takePlan(heng::Plan& out);       // true once when a new plan arrived
    std::string lastError() const;
    // token usage is not parsed; we report rough counts
    size_t requestCount() const { return requests_.load(); }

private:
    void run(SessionSettings s, Workspace* ws);
    void addLog(LogKind k, const std::string& title, const std::string& text);

    mutable std::mutex mu_;
    std::vector<LogItem> log_;
    std::vector<heng::ChatMessage> hist_;
    heng::Plan plan_; bool planReady_ = false;
    std::string error_;
    std::atomic<SessionState> state_{SessionState::Idle};
    std::atomic<int> step_{0};
    std::atomic<size_t> requests_{0};
    std::atomic<bool> cancel_{false};
    std::thread th_;
};

} // namespace he
