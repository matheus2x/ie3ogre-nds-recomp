// Child processes for the build tools, without console windows popping up on Windows.
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include "launcher.h"

namespace launcher {

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// One argument as CommandLineToArgvW reads it back: quotes around anything with spaces,
// backslashes doubled only before a quote.
void append_arg(std::wstring& cmd, const std::wstring& arg) {
    if (!cmd.empty()) cmd += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        cmd += arg;
        return;
    }
    cmd += L'"';
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            slashes++;
            continue;
        }
        cmd.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        cmd += c;
    }
    cmd.append(slashes * 2, L'\\');
    cmd += L'"';
}

std::wstring command_line(const std::vector<std::string>& args) {
    std::wstring cmd;
    for (const std::string& a : args) append_arg(cmd, widen(a));
    return cmd;
}

}  // namespace

int run_process(const std::vector<std::string>& args, std::string& output, const std::string& cwd) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE read_end, write_end;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) return -1;
    SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = write_end;
    si.hStdError = write_end;
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = command_line(args), wcwd = widen(cwd);
    BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  cwd.empty() ? nullptr : wcwd.c_str(), &si, &pi);
    CloseHandle(write_end);
    CloseHandle(nul);
    if (!started) {
        CloseHandle(read_end);
        output = "cannot start " + args[0];
        return -1;
    }
    char buf[4096];
    DWORD n;
    while (ReadFile(read_end, buf, sizeof buf, &n, nullptr) && n) output.append(buf, n);
    CloseHandle(read_end);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return int(code);
}

bool start_process(const std::vector<std::string>& args, const std::string& cwd, std::function<void()> on_exit) {
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = command_line(args), wcwd = widen(cwd);
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, wcwd.c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    if (!on_exit) {
        CloseHandle(pi.hProcess);
        return true;
    }
    std::thread([process = pi.hProcess, on_exit = std::move(on_exit)] {
        WaitForSingleObject(process, INFINITE);
        CloseHandle(process);
        on_exit();
    }).detach();
    return true;
}

std::string system_tool(const char* name) {
    wchar_t dir[MAX_PATH];
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    std::string out(n * 3 + 1, '\0');
    int len = WideCharToMultiByte(CP_UTF8, 0, dir, int(n), out.data(), int(out.size()), nullptr, nullptr);
    out.resize(len);
    return out + "\\" + name + ".exe";
}

#else

namespace {

std::vector<char*> argv_of(const std::vector<std::string>& args) {
    std::vector<char*> argv;
    for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    return argv;
}

}  // namespace

int run_process(const std::vector<std::string>& args, std::string& output, const std::string& cwd) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", 0, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 2);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    if (!cwd.empty()) posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    std::vector<char*> argv = argv_of(args);
    pid_t pid;
    int err = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (err != 0) {
        close(fds[0]);
        output = "cannot start " + args[0];
        return -1;
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof buf)) > 0) output.append(buf, size_t(n));
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

bool start_process(const std::vector<std::string>& args, const std::string& cwd, std::function<void()> on_exit) {
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
    std::vector<char*> argv = argv_of(args);
    pid_t pid;
    int err = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (err != 0) return false;
    if (on_exit) {
        std::thread([pid, on_exit = std::move(on_exit)] {
            int status;
            waitpid(pid, &status, 0);
            on_exit();
        }).detach();
    }
    return true;
}

std::string system_tool(const char* name) { return name; }

#endif

}  // namespace launcher
