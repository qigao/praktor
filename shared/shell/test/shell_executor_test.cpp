#include "praktor/shell/shell_executor.hpp"
#include "praktor/shell/process_executor.hpp"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/stat.h>
#endif

namespace {

constexpr int kLongLineLength = 6000;
constexpr int kProcessTreeTimeoutMs = 3000;
constexpr std::size_t kDuplexPayloadSize = 256 * 1024;
constexpr const char* kTailFragment = "tail_fragment";

std::filesystem::path createTempDir()
{
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("praktor_shell_test_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

std::string trim_newlines(std::string value)
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

bool containsLineFragment(const std::vector<std::string>& lines, const std::string& expected)
{
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(expected) != std::string::npos;
    });
}

void writeTextFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.good());
    output << content;
    REQUIRE(output.good());
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

template <typename Predicate>
bool waitUntil(Predicate predicate, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return predicate();
}

int readPidFile(const std::filesystem::path& path)
{
    return std::stoi(trim_newlines(readTextFile(path)));
}

bool containsExactLine(const std::vector<std::string>& lines, const std::string& expected)
{
    return std::find(lines.begin(), lines.end(), expected) != lines.end();
}

std::string repeatedCharacter(char ch, std::size_t count)
{
    return std::string(count, ch);
}

void setProcessEnvironment(const std::string& name, const std::string& value)
{
#ifdef _WIN32
    REQUIRE(_putenv_s(name.c_str(), value.c_str()) == 0);
#else
    REQUIRE(::setenv(name.c_str(), value.c_str(), 1) == 0);
#endif
}

void unsetProcessEnvironment(const std::string& name)
{
#ifdef _WIN32
    REQUIRE(_putenv_s(name.c_str(), "") == 0);
#else
    REQUIRE(::unsetenv(name.c_str()) == 0);
#endif
}

struct ScopedEnvironmentVariable {
    ScopedEnvironmentVariable(std::string name, std::string value)
        : name(std::move(name))
    {
        setProcessEnvironment(this->name, value);
    }

    ~ScopedEnvironmentVariable()
    {
        unsetProcessEnvironment(name);
    }

    std::string name;
};

bool isProcessAlive(int pid)
{
    if (pid <= 0) {
        return false;
    }

#ifdef _WIN32
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr) {
        return false;
    }
    const DWORD wait_result = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait_result == WAIT_TIMEOUT;
#else
    if (::kill(pid, 0) == 0) {
        const auto proc_status_path = std::filesystem::path("/proc") / std::to_string(pid) / "stat";
        std::ifstream status_input(proc_status_path);
        if (!status_input.good()) {
            return true;
        }

        std::string stat_line;
        std::getline(status_input, stat_line);
        const auto state_pos = stat_line.rfind(") ");
        if (state_pos == std::string::npos || state_pos + 2 >= stat_line.size()) {
            return true;
        }

        return stat_line[state_pos + 2] != 'Z';
    }
    return errno != ESRCH;
#endif
}

#ifdef _WIN32
std::string stdoutStderrCommand()
{
    return "echo stdout_line & echo stderr_line 1>&2 & exit 7";
}

std::string printWorkingDirCommand()
{
    return "cd";
}

std::string envEchoCommand(const std::string& name)
{
    return "echo %" + name + "%";
}

std::string envPairEchoCommand(const std::string& first, const std::string& second)
{
    return "echo %" + first + "%,%" + second + "%";
}

std::string sleepCommand()
{
    return "ping 127.0.0.1 -n 4 > nul";
}

std::string commandForScript(const std::filesystem::path& path)
{
    return "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \"" + path.string() + "\"";
}

std::filesystem::path createKillScript(const std::filesystem::path& dir,
                                       const std::filesystem::path& pid_file,
                                       const std::filesystem::path& finished_file)
{
    const auto script = dir / "kill_process.ps1";
    writeTextFile(script,
                  "$ErrorActionPreference = 'Stop'\n"
                  "[System.IO.File]::WriteAllText('" + pid_file.string() + "', $PID.ToString())\n"
                  "Start-Sleep -Seconds 10\n"
                  "[System.IO.File]::WriteAllText('" + finished_file.string() + "', 'done')\n");
    return script;
}

std::filesystem::path createFragmentedOutputScript(const std::filesystem::path& dir)
{
    const auto script = dir / "fragmented_output.ps1";
    writeTextFile(script,
                  "$line = 'A' * " + std::to_string(kLongLineLength) + "\n"
                  "[Console]::Out.Write($line)\n"
                  "[Console]::Out.Write([Environment]::NewLine)\n"
                  "[Console]::Out.Write('" + std::string(kTailFragment) + "')\n");
    return script;
}

std::filesystem::path createEchoInputScript(const std::filesystem::path& dir)
{
    const auto script = dir / "echo_input.ps1";
    writeTextFile(script,
                  "$inputText = [Console]::In.ReadToEnd()\n"
                  "[Console]::Out.Write($inputText)\n");
    return script;
}

std::filesystem::path createAsyncWriteInputScript(const std::filesystem::path& dir,
                                                  const std::filesystem::path& output_file)
{
    const auto script = dir / "async_write_input.ps1";
    writeTextFile(script,
                  "Start-Sleep -Milliseconds 300\n"
                  "$inputText = [Console]::In.ReadToEnd()\n"
                  "[System.IO.File]::WriteAllText('" + output_file.string() + "', $inputText)\n");
    return script;
}

std::filesystem::path createProcessProbeScript(const std::filesystem::path& dir)
{
    const auto script = dir / "process_probe.ps1";
    writeTextFile(script,
                  "param([string]$ArgValue)\n"
                  "$inputText = [Console]::In.ReadToEnd()\n"
                  "$cwdLeaf = Split-Path -Leaf (Get-Location).Path\n"
                  "[Console]::Out.Write($env:PRAKTOR_PROCESS_ENV + '|' + $ArgValue + '|' + $inputText + '|' + $cwdLeaf)\n");
    return script;
}

Praktor::Shell::ProcessSpec processProbeSpec(const std::filesystem::path& script,
                                             const std::filesystem::path& working_dir)
{
    Praktor::Shell::ProcessSpec spec;
    spec.program = "powershell.exe";
    spec.args = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script.string(), "alpha beta"};
    spec.input = "payload";
    spec.working_dir = working_dir.string();
    spec.env = {{"PRAKTOR_PROCESS_ENV", "raw"}};
    spec.stream_output = false;
    return spec;
}

std::filesystem::path createDuplexPipeScript(const std::filesystem::path& dir)
{
    const auto script = dir / "duplex_pipe.ps1";
    writeTextFile(script,
                  "$output = 'O' * " + std::to_string(kDuplexPayloadSize) + "\n"
                  "[Console]::Out.Write($output)\n"
                  "$inputText = [Console]::In.ReadToEnd()\n"
                  "[Console]::Out.Write('|INPUT=' + $inputText.Length)\n");
    return script;
}

Praktor::Shell::ProcessSpec duplexProcessSpec(const std::filesystem::path& script,
                                              const std::string& input)
{
    Praktor::Shell::ProcessSpec spec;
    spec.program = "powershell.exe";
    spec.args = {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script.string()};
    spec.input = input;
    spec.timeout_ms = 10000;
    spec.stream_output = false;
    return spec;
}
#else
std::string stdoutStderrCommand()
{
    return "printf 'stdout_line\\n'; printf 'stderr_line\\n' >&2; exit 7";
}

std::string printWorkingDirCommand()
{
    return "pwd";
}

std::string envEchoCommand(const std::string& name)
{
    return "printf '%s\\n' \"$" + name + "\"";
}

std::string envPairEchoCommand(const std::string& first, const std::string& second)
{
    return "printf '%s,%s\\n' \"$" + first + "\" \"$" + second + "\"";
}

std::string sleepCommand()
{
    return "sleep 2";
}

std::string commandForScript(const std::filesystem::path& path)
{
    return "/bin/sh \"" + path.string() + "\"";
}

std::filesystem::path createKillScript(const std::filesystem::path& dir,
                                       const std::filesystem::path& pid_file,
                                       const std::filesystem::path& finished_file)
{
    const auto script = dir / "kill_process.sh";
    writeTextFile(script,
                  "#!/bin/sh\n"
                  "printf '%s' \"$$\" > \"" + pid_file.string() + "\"\n"
                  "sleep 10\n"
                  "printf 'done' > \"" + finished_file.string() + "\"\n");
    ::chmod(script.string().c_str(), 0700);
    return script;
}

std::filesystem::path createFragmentedOutputScript(const std::filesystem::path& dir)
{
    const auto script = dir / "fragmented_output.sh";
    writeTextFile(script,
                  "#!/bin/sh\n"
                  "awk 'BEGIN { for (i = 0; i < " + std::to_string(kLongLineLength) +
                      "; ++i) printf \"A\"; printf \"\\n\"; }'\n"
                  "printf '" + std::string(kTailFragment) + "'\n");
    ::chmod(script.string().c_str(), 0700);
    return script;
}

std::filesystem::path createEchoInputScript(const std::filesystem::path& dir)
{
    const auto script = dir / "echo_input.sh";
    writeTextFile(script,
                  "#!/bin/sh\n"
                  "cat\n");
    ::chmod(script.string().c_str(), 0700);
    return script;
}

std::filesystem::path createAsyncWriteInputScript(const std::filesystem::path& dir,
                                                  const std::filesystem::path& output_file)
{
    const auto script = dir / "async_write_input.sh";
    writeTextFile(script,
                  "#!/bin/sh\n"
                  "sleep 1\n"
                  "cat > \"" + output_file.string() + "\"\n");
    ::chmod(script.string().c_str(), 0700);
    return script;
}

std::filesystem::path createProcessProbeScript(const std::filesystem::path& dir)
{
    const auto script = dir / "process_probe.sh";
    writeTextFile(script,
                  "#!/bin/sh\n"
                  "input=$(cat)\n"
                  "printf '%s|%s|%s|%s' \"$PRAKTOR_PROCESS_ENV\" \"$1\" \"$input\" \"$(basename \"$PWD\")\"\n");
    ::chmod(script.string().c_str(), 0700);
    return script;
}

std::filesystem::path createDuplexPipeScript(const std::filesystem::path& dir)
{
    const auto script = dir / "duplex_pipe.sh";
    writeTextFile(script,
                  "#!/bin/sh\n"
                  "awk 'BEGIN { for (i = 0; i < " + std::to_string(kDuplexPayloadSize) +
                      "; ++i) printf \"O\"; }'\n"
                  "input_size=$(wc -c | tr -d ' ')\n"
                  "printf '|INPUT=%s' \"$input_size\"\n");
    ::chmod(script.string().c_str(), 0700);
    return script;
}

Praktor::Shell::ProcessSpec duplexProcessSpec(const std::filesystem::path& script,
                                              const std::string& input)
{
    Praktor::Shell::ProcessSpec spec;
    spec.program = "/bin/sh";
    spec.args = {script.string()};
    spec.input = input;
    spec.timeout_ms = 10000;
    spec.stream_output = false;
    return spec;
}

Praktor::Shell::ProcessSpec processProbeSpec(const std::filesystem::path& script,
                                             const std::filesystem::path& working_dir)
{
    Praktor::Shell::ProcessSpec spec;
    spec.program = "/bin/sh";
    spec.args = {script.string(), "alpha beta"};
    spec.input = "payload";
    spec.working_dir = working_dir.string();
    spec.env = {{"PRAKTOR_PROCESS_ENV", "raw"}};
    spec.stream_output = false;
    return spec;
}
#endif

struct StreamCallbackGuard {
    ~StreamCallbackGuard()
    {
        Praktor::Shell::ShellExecutor::setStreamCallback({});
    }
};

} // namespace

TEST_CASE("shell executor captures stdout stderr and exit code", "[shell]") {
    const auto result = Praktor::Shell::ShellExecutor::execute(stdoutStderrCommand(), "", "", 30000, {}, false);

    CHECK(result.exit_code == 7);
    CHECK_FALSE(result.success());
    CHECK(result.stdout_output.find("stdout_line") != std::string::npos);
    CHECK(result.stderr_output.find("stderr_line") != std::string::npos);
    CHECK_FALSE(result.output_streamed_live);
}

TEST_CASE("shell executor respects working directory", "[shell]") {
    const auto dir = createTempDir();

    const auto result = Praktor::Shell::ShellExecutor::execute(
        printWorkingDirCommand(), "", dir.string(), 30000, {}, false);

    REQUIRE(result.success());
    CHECK(trim_newlines(result.stdout_output) == dir.lexically_normal().string());

    std::filesystem::remove_all(dir);
}

TEST_CASE("shell executor passes environment overrides", "[shell]") {
    constexpr const char* env_name = "PRAKTOR_SHELL_ENV_TEST";
    const auto result = Praktor::Shell::ShellExecutor::execute(
        envEchoCommand(env_name), "", "", 30000, {{env_name, "inside"}}, false);

    REQUIRE(result.success());
    CHECK(trim_newlines(result.stdout_output) == "inside");
}

TEST_CASE("shell executor preserves inherited environment when overriding variables", "[shell]") {
    constexpr const char* inherited_name = "PRAKTOR_SHELL_ENV_INHERITED";
    constexpr const char* override_name = "PRAKTOR_SHELL_ENV_OVERRIDE";
    ScopedEnvironmentVariable inherited(inherited_name, "outside");

    const auto result = Praktor::Shell::ShellExecutor::execute(
        envPairEchoCommand(override_name, inherited_name),
        "",
        "",
        30000,
        {{override_name, "inside"}},
        false);

    REQUIRE(result.success());
    CHECK(trim_newlines(result.stdout_output) == "inside,outside");
}

TEST_CASE("shell executor enforces timeout", "[shell]") {
    const auto result = Praktor::Shell::ShellExecutor::execute(
        sleepCommand(), "", "", 100, {}, false);

    CHECK(result.exit_code == -1);
    CHECK_FALSE(result.success());
    CHECK(result.stderr_output.find("timed out") != std::string::npos);
}

TEST_CASE("shell executor timeout terminates the spawned process", "[shell]") {
    const auto dir = createTempDir();
    const auto pid_file = dir / "timeout.pid";
    const auto finished_file = dir / "finished.txt";
    const auto script = createKillScript(dir, pid_file, finished_file);

    const auto result =
        Praktor::Shell::ShellExecutor::execute(
            commandForScript(script), "", "", kProcessTreeTimeoutMs, {}, false);

    CHECK(result.exit_code == -1);
    CHECK_FALSE(result.success());
    CHECK(result.stderr_output.find("timed out") != std::string::npos);
    REQUIRE(std::filesystem::exists(pid_file));

    const int pid = readPidFile(pid_file);
    REQUIRE(pid > 0);
    CHECK(waitUntil([&]() { return !isProcessAlive(pid); }, 5000));
    CHECK_FALSE(std::filesystem::exists(finished_file));

    std::filesystem::remove_all(dir);
}

TEST_CASE("shell executor forwards stdin to the child process", "[shell]") {
    const auto dir = createTempDir();
    const auto script = createEchoInputScript(dir);
    const std::string input = "first line\nsecond line\n";

    const auto result =
        Praktor::Shell::ShellExecutor::execute(commandForScript(script), input, "", 30000, {}, false);

    REQUIRE(result.success());
    CHECK(result.stdout_output == input);

    std::filesystem::remove_all(dir);
}

TEST_CASE("shell executor streams lines through callback", "[shell]") {
    StreamCallbackGuard guard;
    std::mutex mutex;
    std::vector<std::string> lines;

    Praktor::Shell::ShellExecutor::setStreamCallback([&](const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        lines.push_back(line);
    });

    const auto result = Praktor::Shell::ShellExecutor::execute(stdoutStderrCommand(), "", "", 30000, {}, true);

    REQUIRE_FALSE(lines.empty());
    CHECK(result.output_streamed_live);
    CHECK(containsLineFragment(lines, "stdout_line"));
    CHECK(containsLineFragment(lines, "stderr_line"));
}

TEST_CASE("shell executor flushes fragmented and trailing callback lines", "[shell]") {
    StreamCallbackGuard guard;
    const auto dir = createTempDir();
    const auto script = createFragmentedOutputScript(dir);
    const auto long_line = repeatedCharacter('A', kLongLineLength);

    std::mutex mutex;
    std::vector<std::string> lines;
    Praktor::Shell::ShellExecutor::setStreamCallback([&](const std::string& line) {
        std::lock_guard<std::mutex> lock(mutex);
        lines.push_back(line);
    });

    const auto result =
        Praktor::Shell::ShellExecutor::execute(commandForScript(script), "", "", 30000, {}, true);

    REQUIRE(result.success());
    CHECK(result.output_streamed_live);
    CHECK(containsExactLine(lines, long_line));
    CHECK(containsExactLine(lines, kTailFragment));
    CHECK(result.stdout_output.find(long_line) != std::string::npos);
    CHECK(result.stdout_output.find(kTailFragment) != std::string::npos);

    std::filesystem::remove_all(dir);
}

TEST_CASE("shell executor pumps large stdin and stdout without deadlock", "[shell][pipes]") {
    const auto dir = createTempDir();
    const auto script = createDuplexPipeScript(dir);
    const std::string input(kDuplexPayloadSize, 'I');

    const auto result = Praktor::Shell::ShellExecutor::execute(
        commandForScript(script), input, "", 10000, {}, false);

    REQUIRE(result.success());
    CHECK(result.stdout_output.size() >= kDuplexPayloadSize);
    CHECK(result.stdout_output.find("|INPUT=" + std::to_string(kDuplexPayloadSize)) !=
          std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("stream callback updates are synchronized with concurrent emission", "[shell][concurrency]") {
    StreamCallbackGuard guard;
    constexpr int kEmitterThreads = 4;
    constexpr int kMessagesPerThread = 1000;
    std::atomic<int> first_count{0};
    std::atomic<int> second_count{0};

    const Praktor::Shell::ShellExecutor::StreamCallback first =
        [&](const std::string&) { first_count.fetch_add(1); };
    const Praktor::Shell::ShellExecutor::StreamCallback second =
        [&](const std::string&) { second_count.fetch_add(1); };
    std::vector<std::thread> emitters;
    for (int i = 0; i < kEmitterThreads; ++i) {
        emitters.emplace_back([&, i]() {
            Praktor::Shell::ShellExecutor::setStreamCallback((i % 2 == 0) ? first : second);
            for (int message = 0; message < kMessagesPerThread; ++message) {
                Praktor::Shell::ShellExecutor::emitStreamLine("line");
            }
        });
    }

    for (auto& emitter : emitters) {
        emitter.join();
    }

    CHECK(first_count.load() == (kEmitterThreads / 2) * kMessagesPerThread);
    CHECK(second_count.load() == (kEmitterThreads / 2) * kMessagesPerThread);
}

TEST_CASE("shell executor kills a running process by pid", "[shell]") {
    const auto dir = createTempDir();
    const auto pid_file = dir / "shell.pid";
    const auto finished_file = dir / "finished.txt";
    const auto script = createKillScript(dir, pid_file, finished_file);

    Praktor::Shell::ShellExecutor::executeAsync(commandForScript(script), "", "", {}, false);

    REQUIRE(waitUntil([&]() {
        return std::filesystem::exists(pid_file) && !trim_newlines(readTextFile(pid_file)).empty();
    }, 5000));

    const int pid = readPidFile(pid_file);
    REQUIRE(pid > 0);
    REQUIRE(isProcessAlive(pid));
    REQUIRE(Praktor::Shell::ShellExecutor::killProcess(pid));
    CHECK(waitUntil([&]() { return !isProcessAlive(pid); }, 5000));
    CHECK_FALSE(std::filesystem::exists(finished_file));

    std::filesystem::remove_all(dir);
}

TEST_CASE("shell executor rejects invalid pids", "[shell]") {
    CHECK_FALSE(Praktor::Shell::ShellExecutor::killProcess(0));
    CHECK_FALSE(Praktor::Shell::ShellExecutor::killProcess(-1));
}

TEST_CASE("shell executor runs async commands without blocking the caller", "[shell]") {
    const auto dir = createTempDir();
    const auto output_file = dir / "async_output.txt";
    const auto script = createAsyncWriteInputScript(dir, output_file);
    const std::string input = "async input payload\n";

    const auto start = std::chrono::steady_clock::now();
    Praktor::Shell::ShellExecutor::executeAsync(commandForScript(script), input, "", {}, false);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start)
                                .count();

    CHECK(elapsed_ms < 250);
    CHECK_FALSE(std::filesystem::exists(output_file));
    REQUIRE(waitUntil([&]() {
        return std::filesystem::exists(output_file) && readTextFile(output_file) == input;
    }, 5000));
    CHECK(readTextFile(output_file) == input);

    std::filesystem::remove_all(dir);
}

TEST_CASE("process executor runs program args env stdin and working directory without shell parsing", "[process]") {
    const auto dir = createTempDir();
    const auto working_dir = dir / "work";
    std::filesystem::create_directories(working_dir);
    const auto script = createProcessProbeScript(dir);

    const auto result = Praktor::Shell::ProcessExecutor::execute(processProbeSpec(script, working_dir));

    REQUIRE(result.success());
    CHECK(result.stdout_output == "raw|alpha beta|payload|work");
    CHECK(result.stderr_output.empty());
    CHECK_FALSE(result.output_streamed_live);

    std::filesystem::remove_all(dir);
}

TEST_CASE("process executor pumps large stdin and stdout without deadlock", "[process][pipes]") {
    const auto dir = createTempDir();
    const auto script = createDuplexPipeScript(dir);
    const std::string input(kDuplexPayloadSize, 'I');

    const auto result = Praktor::Shell::ProcessExecutor::execute(duplexProcessSpec(script, input));

    REQUIRE(result.success());
    CHECK(result.stdout_output.size() >= kDuplexPayloadSize);
    CHECK(result.stdout_output.find("|INPUT=" + std::to_string(kDuplexPayloadSize)) !=
          std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("managed process exposes lifecycle and supports bounded waits", "[process][managed]") {
    const auto dir = createTempDir();
    const auto pid_file = dir / "managed.pid";
    const auto finished_file = dir / "finished.txt";
    const auto script = createKillScript(dir, pid_file, finished_file);

    auto process = Praktor::Shell::ShellExecutor::start(
        commandForScript(script), "", "", 10000, {}, false);

    REQUIRE(process.valid());
    REQUIRE(waitUntil([&]() { return process.pid() > 0; }, 5000));
    CHECK(process.isRunning());
    CHECK_FALSE(process.waitFor(std::chrono::milliseconds(25)));
    CHECK_FALSE(process.result().has_value());

    REQUIRE(process.cancel());
    const auto result = process.wait();
    CHECK(result.state == Praktor::Shell::ProcessState::Cancelled);
    CHECK_FALSE(result.success());
    CHECK_FALSE(process.cancel());
    CHECK(process.result().has_value());
    CHECK_FALSE(std::filesystem::exists(finished_file));

    std::filesystem::remove_all(dir);
}

TEST_CASE("managed process destructor terminates its process tree", "[process][managed]") {
    const auto dir = createTempDir();
    const auto pid_file = dir / "managed_destructor.pid";
    const auto finished_file = dir / "finished.txt";
    const auto script = createKillScript(dir, pid_file, finished_file);
    int managed_pid = -1;

    {
        auto process = Praktor::Shell::ShellExecutor::start(
            commandForScript(script), "", "", 10000, {}, false);
        REQUIRE(waitUntil([&]() { return process.pid() > 0; }, 5000));
        managed_pid = process.pid();
        REQUIRE(isProcessAlive(managed_pid));
    }

    CHECK(waitUntil([&]() { return !isProcessAlive(managed_pid); }, 5000));
    CHECK_FALSE(std::filesystem::exists(finished_file));
    std::filesystem::remove_all(dir);
}

TEST_CASE("managed process reports spawn failures distinctly", "[process][managed]") {
    Praktor::Shell::ProcessSpec spec;
    spec.program = "definitely_missing_managed_process_97531";
    spec.stream_output = false;

    auto process = Praktor::Shell::ProcessExecutor::start(spec);
    const auto result = process.wait();

    CHECK(result.state == Praktor::Shell::ProcessState::SpawnFailed);
    CHECK(result.exit_code == -1);
    CHECK_FALSE(result.success());
    CHECK_FALSE(result.stderr_output.empty());
}

TEST_CASE("managed process enforces captured output limits", "[process][managed][limits]") {
    const auto dir = createTempDir();
    const auto script = createDuplexPipeScript(dir);
    auto spec = duplexProcessSpec(script, "");
    constexpr std::size_t kOutputLimit = 4096;
    spec.max_output_bytes = kOutputLimit;

    const auto result = Praktor::Shell::ProcessExecutor::execute(spec);

    CHECK(result.state == Praktor::Shell::ProcessState::OutputLimitExceeded);
    CHECK(result.exit_code == -1);
    CHECK(result.stdout_output.size() + result.stderr_output.size() <=
          kOutputLimit + std::string("\nCaptured process output exceeded the configured limit").size());
    CHECK_FALSE(result.success());
    std::filesystem::remove_all(dir);
}
