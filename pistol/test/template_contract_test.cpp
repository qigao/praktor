#include <dag/workflow_context.hpp>
#include <dag/workflow_executor.hpp>
#include <workflow_runner.hpp>
#include <yml/task.hpp>
#include <yml/task_parser.hpp>

#include <catch2/catch_all.hpp>
#include <jsoncons/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

std::filesystem::path createTempDir()
{
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("praktor_pistol_templates_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::string trim(std::string value)
{
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
                value.end());
    return value;
}

bool isTruthy(const WorkflowValue& value)
{
    if (value.is_bool()) {
        return value.as<bool>();
    }
    if (value.is_int64()) {
        return value.as<int64_t>() != 0;
    }
    if (value.is_double()) {
        return std::fpclassify(value.as<double>()) != FP_ZERO;
    }
    if (value.is_string()) {
        const auto text = trim(value.as<std::string>());
        return text == "true" || text == "1";
    }
    return !value.is_null();
}

std::filesystem::path pistolDir()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path();
}

std::vector<std::filesystem::path> collectYamlFiles(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".yml") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

struct ExecutionResult {
    bool success;
    std::unique_ptr<WorkflowContext> context;
};

ExecutionResult executeWorkflow(const std::filesystem::path& workflow_path,
                                std::unordered_map<std::string, std::string> input_values = {})
{
    auto workflow = TaskParser::parseFileWithImports(workflow_path.string(), workflow_path.parent_path().string());
    auto context = std::make_unique<WorkflowContext>();
    context->setEmbeddedModules(workflow.embedded);
    context->setNativeModules(workflow.native_modules);
    context->setSourcePath(workflow.source_path);

    const auto set_workflow_variable = [&context](const std::string& key, const std::string& value) {
        context->setValue(key, value);
        context->setValue("variables." + key, value);
    };

    for (const auto& [key, value] : input_values) {
        set_workflow_variable(key, value);
        if (context->getValue<std::string>(key) != value ||
            context->getValue<std::string>("variables." + key) != value) {
            throw std::runtime_error("Failed to initialize workflow input: " + key);
        }
    }

    for (const auto& [key, value] : workflow.variables) {
        if (input_values.find(key) == input_values.end()) {
            set_workflow_variable(key, value);
        }
    }

    auto graph = TaskParser::buildGraph(workflow);
    WorkflowExecutor executor(graph, workflow.tasks, {}, 1, false);
    executor.execute(*context);

    const auto status = context->getValueOrDefault<std::string>("workflow_status", "unknown");
    return {status != "failed", std::move(context)};
}

long long currentProcessId()
{
#ifdef _WIN32
    return static_cast<long long>(::GetCurrentProcessId());
#else
    return static_cast<long long>(::getpid());
#endif
}

struct ManagedSleepProcess {
    long long pid = 0;

#ifdef _WIN32
    HANDLE process_handle = nullptr;
    HANDLE thread_handle = nullptr;
#else
    pid_t child_pid = -1;
#endif

    ManagedSleepProcess() = default;
    ManagedSleepProcess(const ManagedSleepProcess&) = delete;
    ManagedSleepProcess& operator=(const ManagedSleepProcess&) = delete;

    ManagedSleepProcess(ManagedSleepProcess&& other) noexcept
    {
        *this = std::move(other);
    }

    ManagedSleepProcess& operator=(ManagedSleepProcess&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }
        cleanup();
        pid = other.pid;
#ifdef _WIN32
        process_handle = other.process_handle;
        thread_handle = other.thread_handle;
        other.process_handle = nullptr;
        other.thread_handle = nullptr;
#else
        child_pid = other.child_pid;
        other.child_pid = -1;
#endif
        other.pid = 0;
        return *this;
    }

    ~ManagedSleepProcess()
    {
        cleanup();
    }

    bool isRunning()
    {
#ifdef _WIN32
        if (!process_handle) {
            return false;
        }
        DWORD exit_code = 0;
        if (!::GetExitCodeProcess(process_handle, &exit_code)) {
            return false;
        }
        return exit_code == STILL_ACTIVE;
#else
        if (child_pid <= 0) {
            return false;
        }
        int status = 0;
        pid_t result = ::waitpid(child_pid, &status, WNOHANG);
        return result == 0;
#endif
    }

    void cleanup()
    {
#ifdef _WIN32
        if (process_handle) {
            if (isRunning()) {
                ::TerminateProcess(process_handle, 1);
                ::WaitForSingleObject(process_handle, 5000);
            }
            ::CloseHandle(process_handle);
            process_handle = nullptr;
        }
        if (thread_handle) {
            ::CloseHandle(thread_handle);
            thread_handle = nullptr;
        }
#else
        if (child_pid > 0) {
            if (isRunning()) {
                ::kill(child_pid, SIGKILL);
                ::waitpid(child_pid, nullptr, 0);
            }
            child_pid = -1;
        }
#endif
        pid = 0;
    }
};

ManagedSleepProcess startSleepProcess()
{
    ManagedSleepProcess process;

#ifdef _WIN32
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    std::wstring command = L"cmd.exe /c ping -n 60 127.0.0.1 >nul";

    REQUIRE(::CreateProcessW(
        nullptr,
        command.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup_info,
        &process_info));

    process.pid = static_cast<long long>(process_info.dwProcessId);
    process.process_handle = process_info.hProcess;
    process.thread_handle = process_info.hThread;
#else
    pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ::execlp("sleep", "sleep", "60", static_cast<char*>(nullptr));
        _exit(127);
    }
    process.pid = static_cast<long long>(child);
    process.child_pid = child;
#endif

    REQUIRE(process.isRunning());
    return process;
}

bool waitForProcessExit(ManagedSleepProcess& process, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!process.isRunning()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !process.isRunning();
}

} // namespace

TEST_CASE("all pistol templates and examples validate", "[pistol][templates]")
{
    const auto templates_dir = pistolDir() / "templates";
    const auto examples_dir = pistolDir() / "examples";

    for (const auto& dir : {templates_dir, examples_dir}) {
        for (const auto& path : collectYamlFiles(dir)) {
            CAPTURE(path.string());
            auto workflow = TaskParser::parseFile(path.string());
            REQUIRE_NOTHROW(TaskParser::buildGraph(workflow));
        }
    }
}

TEST_CASE("process status template reports current process by pid", "[pistol][templates]")
{
    const auto template_path = pistolDir() / "templates" / "process-status.yml";

    auto execution = executeWorkflow(template_path, {
        {"PID", std::to_string(currentProcessId())}
    });
    REQUIRE(execution.success);

    const auto result = execution.context->getValueByPath("tasks.emit_result.outputs.result");
    CHECK(result["status"].as<std::string>() == "running");
    CHECK(isTruthy(result["running"]));
    CHECK(trim(result["pid"].as<std::string>()) == std::to_string(currentProcessId()));
}

TEST_CASE("process stop template terminates a child process by pid", "[pistol][templates]")
{
    const auto template_path = pistolDir() / "templates" / "process-stop.yml";
    auto child = startSleepProcess();

    auto execution = executeWorkflow(template_path, {
        {"PID", std::to_string(child.pid)}
    });

    REQUIRE(execution.success);
    REQUIRE(waitForProcessExit(child, std::chrono::milliseconds(3000)));

    const auto result = execution.context->getValueByPath("tasks.emit_result.outputs.result");
    CHECK(result["status"].as<std::string>() == "stopped");
    CHECK(trim(result["pid"].as<std::string>()) == std::to_string(child.pid));
}

TEST_CASE("archive templates create and extract zip with completed results", "[pistol][templates]")
{
    const auto dir = createTempDir();
    const auto source_dir = dir / "srcdir";
    const auto source_file = source_dir / "payload.txt";
    const auto archive_path = dir / "bundle.zip";
    const auto extract_dir = dir / "unpacked";
    const auto create_template = pistolDir() / "templates" / "archive-create.yml";
    const auto extract_template = pistolDir() / "templates" / "archive-extract.yml";

    writeFile(source_file, "payload-data");

    auto create_execution = executeWorkflow(create_template, {
        {"SRC_PATH", source_dir.generic_string()},
        {"DST_ARCHIVE", archive_path.generic_string()},
        {"FORMAT", "zip"},
        {"OVERWRITE", "true"}
    });
    REQUIRE(create_execution.success);

    REQUIRE(std::filesystem::exists(archive_path));

    auto extract_execution = executeWorkflow(extract_template, {
        {"SRC_ARCHIVE", archive_path.generic_string()},
        {"DST_DIR", extract_dir.generic_string()},
        {"FORMAT", "zip"}
    });
    REQUIRE(extract_execution.success);

    const auto extracted_file = extract_dir / source_dir.filename() / source_file.filename();
    REQUIRE(std::filesystem::exists(extracted_file));
    CHECK(readFile(extracted_file) == "payload-data");

    const auto create_result = create_execution.context->getValueByPath("tasks.emit_result.outputs.result");
    const auto extract_result = extract_execution.context->getValueByPath("tasks.emit_result.outputs.result");
    CHECK(create_result["status"].as<std::string>() == "completed");
    CHECK(extract_result["status"].as<std::string>() == "completed");
    CHECK(create_result["archive"].as<std::string>() == archive_path.generic_string());
    CHECK(extract_result["destination"].as<std::string>() == extract_dir.generic_string());

    std::filesystem::remove_all(dir);
}

TEST_CASE("uses vars override nested workflow defaults", "[pistol][templates]")
{
    const auto dir = createTempDir();
    const auto nested_path = dir / "nested.yml";
    const auto wrapper_path = dir / "wrapper.yml";

    writeFile(
        nested_path,
        "variables:\n"
        "  NAME: default\n"
        "tasks:\n"
        "  - name: emit\n"
        "    script: |\n"
        "      ctx.output(\"value\", ctx.get(\"NAME\"));\n");

    writeFile(
        wrapper_path,
        "tasks:\n"
        "  - name: call\n"
        "    uses: " + nested_path.generic_string() + "\n"
        "    vars:\n"
        "      NAME: override\n");

    auto execution = executeWorkflow(wrapper_path);
    REQUIRE(execution.success);
    CHECK(execution.context->getValueByPath("tasks.call.outputs.nested_tasks.emit.outputs.value").as<std::string>() == "override");

    std::filesystem::remove_all(dir);
}
