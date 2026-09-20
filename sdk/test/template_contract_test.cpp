#include <dag/workflow_context.hpp>
#include <dag/workflow_executor.hpp>
#include <workflow_runner.hpp>
#include <yml/task.hpp>
#include <yml/task_parser.hpp>

#include <catch2/catch_all.hpp>
#include "data/workflow_value.hpp"

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
        / ("praktor_pistol_examples_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);
    return dir;
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
    if (!std::filesystem::exists(root)) {
        return files;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".yml") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    REQUIRE(stream.is_open());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

struct ExecutionResult {
    bool success;
    std::unique_ptr<WorkflowContext> context;
};

ExecutionResult executeWorkflow(const std::filesystem::path& workflow_path,
                                std::unordered_map<std::string, std::string> input_values = {})
{
    auto workflow = TaskParser::parseFileWithIncludes(workflow_path.string(), workflow_path.parent_path().string());
    auto context = std::make_unique<WorkflowContext>();
    context->setSourcePath(workflow.source_path);

    const auto set_workflow_variable = [&context](const std::string& key, const std::string& value) {
        context->setValue(key, value);
        context->setValue("variables." + key, value);
    };

    for (const auto& [key, value] : input_values) {
        set_workflow_variable(key, value);
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

TEST_CASE("all pistol examples validate and build DAG", "[pistol][examples]")
{
    const auto examples_dir = pistolDir() / "examples";
    const auto result_models_path = examples_dir / "result_models.tbs";
    const auto result_models = readTextFile(result_models_path);

    CHECK(result_models.find("import(\"mapper\")") != std::string::npos);
    CHECK(result_models.find("class ActionResult") != std::string::npos);
    CHECK(result_models.find("class StatusResult") != std::string::npos);
    CHECK(result_models.find("class ProcessResult") != std::string::npos);

    size_t class_count = 0;
    for (size_t position = 0;
         (position = result_models.find("class ", position)) != std::string::npos;
         position += 6) {
        ++class_count;
    }
    CHECK(class_count == 3);

    const auto example_files = collectYamlFiles(examples_dir);
    REQUIRE(!example_files.empty());

    for (const auto& path : example_files) {
        CAPTURE(path.string());
        const auto source = readTextFile(path);
        CHECK(source.find("json.stringify") == std::string::npos);
        CHECK(source.find("import(\"./result_models.tbs\")") != std::string::npos);
        CHECK(source.find("class ") == std::string::npos);
        CHECK(source.find("mapper.write_json") != std::string::npos);
        auto workflow = TaskParser::parseFile(path.string());
        REQUIRE_NOTHROW(TaskParser::buildGraph(workflow));
    }
}

TEST_CASE("process status BT workflow queries current process", "[pistol][examples]")
{
    const auto example_path = pistolDir() / "examples" / "process-status-example.yml";

    auto execution = executeWorkflow(example_path, {
        {"PROCESS_NAME", "ping"}
    });
    REQUIRE(execution.success);

    const auto result = execution.context->getValueByPath("tasks.emit_result.outputs.result");
    REQUIRE(result.is_string());
    const auto parsed_result = WorkflowValue::parse(result.as<std::string>());
    CHECK(parsed_result.at("process").as<std::string>() == "ping");
    CHECK(parsed_result.at("action").at("running").is_bool());
    CHECK(parsed_result.at("output").is_string());
}

TEST_CASE("ops event handler BT workflow parses JSON event and executes branch", "[pistol][examples]")
{
    const auto example_path = pistolDir() / "examples" / "ops-event-handler-example.yml";

    auto workflow = TaskParser::parseFile(example_path.string());
    WorkflowContext context;
    context.setSourcePath(workflow.source_path);

    std::string mock_payload = R"({"action":"status","target":"service-nginx"})";
    context.setValue("event_payload", mock_payload);

    const auto handle_task = std::find_if(
        workflow.tasks.begin(),
        workflow.tasks.end(),
        [](const auto& task) { return task.name == "handle_ops_event"; });
    REQUIRE(handle_task != workflow.tasks.end());
    REQUIRE(std::holds_alternative<OrchParams>(handle_task->specifics));

    auto& params = std::get<OrchParams>(handle_task->specifics);
    REQUIRE(params.root.type == "Sequence");
    REQUIRE(!params.root.children.empty());
    REQUIRE(params.root.children.front().type == "WaitEvent");

    // The mock payload represents the event after WaitEvent has delivered it.
    params.root.children.erase(params.root.children.begin());

    auto graph = TaskParser::buildGraph(workflow);
    WorkflowExecutor executor(graph, workflow.tasks, {}, 1, false);
    executor.execute(context);

    const auto status = context.getValueOrDefault<std::string>("workflow_status", "unknown");
    REQUIRE(status != "failed");

    const auto result_val = context.getValueByPath("tasks.emit_result.outputs.result");
    REQUIRE(result_val.is_string());
    const auto parsed_result = WorkflowValue::parse(result_val.as<std::string>());
    CHECK(parsed_result.at("status").as<std::string>() == "completed");
    CHECK(parsed_result.at("action").at("action").as<std::string>() == "status");
    CHECK(parsed_result.at("action").at("target").as<std::string>() == "service-nginx");
    CHECK(parsed_result.at("action").at("success").as<bool>());
}
