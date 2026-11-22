#include "process.h"
#include <chrono>
#include <thread>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
namespace he {
static std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring r = L"\"";
    for (size_t i = 0; i < a.size(); ++i) {
        size_t bs = 0;
        while (i < a.size() && a[i] == L'\\') { ++bs; ++i; }
        if (i == a.size()) { r.append(bs * 2, L'\\'); break; }
        if (a[i] == L'"') { r.append(bs * 2 + 1, L'\\'); r.push_back(L'"'); }
        else { r.append(bs, L'\\'); r.push_back(a[i]); }
    }
    r.push_back(L'"');
    return r;
}
RunResult runProcess(const std::vector<std::string>& argv, const std::string& workDir, int timeoutSec,
                     std::atomic<bool>* cancel, const std::function<void(const std::string&)>& onOutput, size_t maxOutput) {
    RunResult rr;
    if (argv.empty()) { rr.error = "empty command"; return rr; }
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = nullptr;
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { rr.error = "CreatePipe failed"; return rr; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nul;
    std::wstring cmd;
    for (size_t i = 0; i < argv.size(); ++i) { if (i) cmd += L' '; cmd += quoteArg(widen(argv[i])); }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    std::wstring wd = widen(workDir);
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, wd.empty() ? nullptr : wd.c_str(), &si, &pi);
    CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { rr.error = "cannot start '" + argv[0] + "' (error " + std::to_string(GetLastError()) + ")"; CloseHandle(rd); return rr; }
    rr.started = true;
    auto t0 = std::chrono::steady_clock::now();
    char tmp[4096];
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD got = 0;
            if (ReadFile(rd, tmp, (DWORD)std::min<size_t>(sizeof tmp, avail), &got, nullptr) && got) {
                std::string piece(tmp, got);
                if (rr.output.size() < maxOutput) rr.output += piece;
                if (onOutput) onOutput(piece);
            }
            continue;
        }
        DWORD w = WaitForSingleObject(pi.hProcess, 40);
        if (w == WAIT_OBJECT_0) {
            // drain what is left
            for (;;) { DWORD a2 = 0; if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &a2, nullptr) || a2 == 0) break;
                DWORD got = 0; if (!ReadFile(rd, tmp, (DWORD)std::min<size_t>(sizeof tmp, a2), &got, nullptr) || !got) break;
                std::string piece(tmp, got); if (rr.output.size() < maxOutput) rr.output += piece; if (onOutput) onOutput(piece); }
            break;
        }
        bool stop = false;
        if (cancel && cancel->load()) { rr.cancelled = true; stop = true; }
        if (timeoutSec > 0 && std::chrono::steady_clock::now() - t0 > std::chrono::seconds(timeoutSec)) { rr.timedOut = true; stop = true; }
        if (stop) { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 2000); break; }
    }
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code); rr.exitCode = (int)code;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    return rr;
}
} // namespace he

#else
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>
namespace he {
RunResult runProcess(const std::vector<std::string>& argv, const std::string& workDir, int timeoutSec,
                     std::atomic<bool>* cancel, const std::function<void(const std::string&)>& onOutput, size_t maxOutput) {
    RunResult rr;
    if (argv.empty()) { rr.error = "empty command"; return rr; }
    int pfd[2], efd[2];
    if (pipe(pfd) != 0) { rr.error = "pipe failed"; return rr; }
    if (pipe(efd) != 0) { close(pfd[0]); close(pfd[1]); rr.error = "pipe failed"; return rr; }
    fcntl(efd[1], F_SETFD, FD_CLOEXEC);   // closes on successful exec -> parent sees EOF
    std::vector<char*> av; for (const std::string& s : argv) av.push_back(const_cast<char*>(s.c_str())); av.push_back(nullptr);
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); close(efd[0]); close(efd[1]); rr.error = "fork failed"; return rr; }
    if (pid == 0) {
        setpgid(0, 0);
        dup2(pfd[1], 1); dup2(pfd[1], 2);
        int dn = open("/dev/null", O_RDONLY); if (dn >= 0) { dup2(dn, 0); close(dn); }
        close(pfd[0]); close(pfd[1]); close(efd[0]);
        if (!workDir.empty() && chdir(workDir.c_str()) != 0) { int e = errno; (void)!write(efd[1], &e, sizeof e); _exit(126); }
        execvp(av[0], av.data());
        int e = errno; (void)!write(efd[1], &e, sizeof e); _exit(127);
    }
    close(pfd[1]); close(efd[1]);
    int execErr = 0; ssize_t n = read(efd[0], &execErr, sizeof execErr); close(efd[0]);
    if (n == (ssize_t)sizeof execErr) {
        int st; waitpid(pid, &st, 0); close(pfd[0]);
        rr.error = "cannot start '" + argv[0] + "': " + std::strerror(execErr); return rr;
    }
    rr.started = true;
    auto t0 = std::chrono::steady_clock::now();
    char tmp[4096]; bool eof = false;
    while (!eof) {
        struct pollfd p; p.fd = pfd[0]; p.events = POLLIN; p.revents = 0;
        int r = poll(&p, 1, 50);
        if (r > 0) {
            ssize_t g = read(pfd[0], tmp, sizeof tmp);
            if (g > 0) { std::string piece(tmp, (size_t)g); if (rr.output.size() < maxOutput) rr.output += piece; if (onOutput) onOutput(piece); }
            else if (g == 0 || (g < 0 && errno != EINTR && errno != EAGAIN)) eof = true;
        }
        bool stop = false;
        if (cancel && cancel->load()) { rr.cancelled = true; stop = true; }
        if (timeoutSec > 0 && std::chrono::steady_clock::now() - t0 > std::chrono::seconds(timeoutSec)) { rr.timedOut = true; stop = true; }
        if (stop) { kill(-pid, SIGKILL); kill(pid, SIGKILL); break; }
    }
    close(pfd[0]);
    int st = 0; waitpid(pid, &st, 0);
    rr.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : (WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1);
    return rr;
}
} // namespace he
#endif
