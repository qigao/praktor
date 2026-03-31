#include "btdsl/shell_executor.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <thread>
#include <chrono>
#include <map>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#endif

namespace btdsl {

namespace {

ShellExecutor::StreamCallback g_stream_callback;

void emitShellLogLine(const std::string& line) {
    if (line.empty() || !g_stream_callback) {
        return;
    }
    g_stream_callback(line);
}

void mirrorWithPrefix(const char* buffer, size_t bytes_read, bool stream_output, std::string& pending_line) {
    if (!stream_output || bytes_read == 0) {
        return;
    }

    for (size_t i = 0; i < bytes_read; ++i) {
        const char ch = buffer[i];
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            emitShellLogLine(pending_line);
            pending_line.clear();
            continue;
        }
        pending_line.push_back(ch);
    }
}

void flushPendingLine(bool stream_output, std::string& pending_line) {
    if (!stream_output || pending_line.empty()) {
        return;
    }
    emitShellLogLine(pending_line);
    pending_line.clear();
}

} // namespace

void ShellExecutor::setStreamCallback(StreamCallback callback) {
    g_stream_callback = std::move(callback);
}

void ShellExecutor::emitStreamLine(const std::string& line) {
    emitShellLogLine(line);
}

ShellResult ShellExecutor::execute(
    const std::string& command,
    const std::string& input,
    const std::string& working_dir,
    int timeout_ms,
    const std::map<std::string, std::string>& env,
    bool stream_output
) {
#ifdef _WIN32
    return executeWindows(command, input, working_dir, timeout_ms, env, stream_output);
#else
    return executeUnix(command, input, working_dir, timeout_ms, env, stream_output);
#endif
}

#ifdef _WIN32
// Build environment block for Windows CreateProcess
static std::vector<char> buildEnvironmentBlock(const std::map<std::string, std::string>& env) {
    if (env.empty()) {
        return {};
    }

    std::vector<char> env_block;
    for (const auto& [key, value] : env) {
        std::string entry = key + "=" + value;
        env_block.insert(env_block.end(), entry.begin(), entry.end());
        env_block.push_back('\0');
    }
    env_block.push_back('\0');  // Double null terminator
    return env_block;
}

static void appendAndMirror(std::string& sink, const char* buffer, DWORD bytes_read, std::ostream& stream,
                            bool stream_output, std::string& pending_line) {
    if (bytes_read == 0) {
        return;
    }
    sink.append(buffer, buffer + bytes_read);
    (void)stream;
    mirrorWithPrefix(buffer, bytes_read, stream_output, pending_line);
}

static void drainPipe(HANDLE pipe, std::string& sink, std::ostream& stream,
                      bool stream_output, std::string& pending_line) {
    for (;;) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL) || available == 0) {
            break;
        }

        char buffer[4096];
        DWORD bytes_to_read = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
        DWORD bytes_read = 0;
        if (!ReadFile(pipe, buffer, bytes_to_read, &bytes_read, NULL) || bytes_read == 0) {
            break;
        }

        appendAndMirror(sink, buffer, bytes_read, stream, stream_output, pending_line);
    }
}

ShellResult ShellExecutor::executeWindows(
    const std::string& command,
    const std::string& input,
    const std::string& working_dir,
    int timeout_ms,
    const std::map<std::string, std::string>& env,
    bool stream_output
) {
    ShellResult result;
    result.output_streamed_live = stream_output;

    // Create pipes for stdout/stderr
    HANDLE hStdoutRead, hStdoutWrite;
    HANDLE hStderrRead, hStderrWrite;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) ||
        !CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        result.stderr_output = "Failed to create pipes";
        result.exit_code = -1;
        return result;
    }

    // Don't inherit read handles
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    // Setup process
    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi = {0};

    // Create command line (cmd.exe /c "command")
    std::string cmdLine = "cmd.exe /c \"" + command + "\"";

    // Build environment block
    std::vector<char> env_block = buildEnvironmentBlock(env);
    void* env_ptr = env_block.empty() ? NULL : env_block.data();

    // Create process
    BOOL success = CreateProcessA(
        NULL,
        const_cast<char*>(cmdLine.c_str()),
        NULL, NULL, TRUE, 0,
        env_ptr,  // Environment block
        working_dir.empty() ? NULL : working_dir.c_str(),
        &si, &pi
    );

    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!success) {
        result.stderr_output = "Failed to create process";
        result.exit_code = -1;
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        return result;
    }

    // Store PID
    result.pid = static_cast<int>(pi.dwProcessId);
    std::string stdout_pending_line;
    std::string stderr_pending_line;

    const DWORD poll_interval_ms = 50;
    auto start = std::chrono::steady_clock::now();
    for (;;) {
        drainPipe(hStdoutRead, result.stdout_output, std::cout, stream_output, stdout_pending_line);
        drainPipe(hStderrRead, result.stderr_output, std::cerr, stream_output, stderr_pending_line);

        DWORD wait_result = WaitForSingleObject(pi.hProcess, poll_interval_ms);
        if (wait_result == WAIT_OBJECT_0) {
            break;
        }
        if (wait_result == WAIT_TIMEOUT) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            if (elapsed > timeout_ms) {
                TerminateProcess(pi.hProcess, 1);
                result.stderr_output = "Command timed out";
                result.exit_code = -1;
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                CloseHandle(hStdoutRead);
                CloseHandle(hStderrRead);
                return result;
            }
            continue;
        }

        result.stderr_output = "Failed while waiting for process";
        result.exit_code = -1;
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        return result;
    }

    // Get exit code
    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = static_cast<int>(exit_code);

    drainPipe(hStdoutRead, result.stdout_output, std::cout, stream_output, stdout_pending_line);
    drainPipe(hStderrRead, result.stderr_output, std::cerr, stream_output, stderr_pending_line);
    flushPendingLine(stream_output, stdout_pending_line);
    flushPendingLine(stream_output, stderr_pending_line);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdoutRead);
    CloseHandle(hStderrRead);

    return result;
}
#else
// Build environment array for Unix exec
static std::vector<char*> buildEnvironmentArray(const std::map<std::string, std::string>& env, std::vector<std::string>& storage) {
    std::vector<char*> env_ptrs;

    if (env.empty()) {
        // Use current environment
        extern char** environ;
        for (char** e = environ; *e != nullptr; e++) {
            env_ptrs.push_back(*e);
        }
    } else {
        // Use provided environment
        for (const auto& [key, value] : env) {
            storage.push_back(key + "=" + value);
        }
        for (auto& entry : storage) {
            env_ptrs.push_back(&entry[0]);
        }
    }

    env_ptrs.push_back(nullptr);
    return env_ptrs;
}

static void appendAndMirror(std::string& sink, const char* buffer, ssize_t bytes_read, std::ostream& stream,
                            bool stream_output, std::string& pending_line) {
    if (bytes_read <= 0) {
        return;
    }
    sink.append(buffer, buffer + bytes_read);
    (void)stream;
    mirrorWithPrefix(buffer, static_cast<size_t>(bytes_read), stream_output, pending_line);
}

ShellResult ShellExecutor::executeUnix(
    const std::string& command,
    const std::string& input,
    const std::string& working_dir,
    int timeout_ms,
    const std::map<std::string, std::string>& env,
    bool stream_output
) {
    ShellResult result;
    result.output_streamed_live = stream_output;

    // Create pipes
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.stderr_output = "Failed to create pipes";
        result.exit_code = -1;
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.stderr_output = "Failed to fork process";
        result.exit_code = -1;
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        // Child process
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        // Change working directory
        if (!working_dir.empty()) {
            chdir(working_dir.c_str());
        }

        // Build environment array
        std::vector<std::string> env_storage;
        std::vector<char*> env_ptrs = buildEnvironmentArray(env, env_storage);

        // Execute command with environment
        execle("/bin/sh", "sh", "-c", command.c_str(), NULL, env_ptrs.data());
        _exit(127);
    }

    // Parent process
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Store PID
    result.pid = static_cast<int>(pid);
    std::string stdout_pending_line;
    std::string stderr_pending_line;

    // Set non-blocking
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    // Wait with timeout
    auto start = std::chrono::steady_clock::now();
    int status;
    bool timed_out = false;

    while (true) {
        pid_t wait_result = waitpid(pid, &status, WNOHANG);

        if (wait_result == pid) {
            // Process finished
            break;
        }

        char buffer[4096];
        ssize_t bytes_read = 0;
        while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
            appendAndMirror(result.stdout_output, buffer, bytes_read, std::cout, stream_output, stdout_pending_line);
        }
        while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
            appendAndMirror(result.stderr_output, buffer, bytes_read, std::cerr, stream_output, stderr_pending_line);
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();

        if (elapsed > timeout_ms) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            timed_out = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (timed_out) {
        result.stderr_output = "Command timed out";
        result.exit_code = -1;
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        return result;
    }

    // Get exit code
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
        // success removed
    }

    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(stdout_pipe[0], buffer, sizeof(buffer))) > 0) {
        appendAndMirror(result.stdout_output, buffer, bytes_read, std::cout, stream_output, stdout_pending_line);
    }

    while ((bytes_read = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
        appendAndMirror(result.stderr_output, buffer, bytes_read, std::cerr, stream_output, stderr_pending_line);
    }
    flushPendingLine(stream_output, stdout_pending_line);
    flushPendingLine(stream_output, stderr_pending_line);

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    return result;
}
#endif

void ShellExecutor::executeAsync(
    const std::string& command,
    const std::string& input,
    const std::string& working_dir,
    const std::map<std::string, std::string>& env,
    bool stream_output
) {
    // Launch in background thread
    std::thread([=]() {
        execute(command, input, working_dir, 30000, env, stream_output);
    }).detach();
}

bool ShellExecutor::killProcess(int pid) {
    if (pid <= 0) {
        return false;
    }

#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (hProcess == NULL) {
        return false;
    }
    BOOL result = TerminateProcess(hProcess, 1);
    CloseHandle(hProcess);
    return result != 0;
#else
    return kill(static_cast<pid_t>(pid), SIGTERM) == 0;
#endif
}

} // namespace btdsl
