#include "praktor_test.hpp"
#include "script/script_engine.hpp"
#include "dag/workflow_context.hpp"
#include "turbo_script.h"

#include <atomic>
#include <barrier>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace {

constexpr double kFloatTolerance = 1e-12;

bool nearlyEqual(double lhs, double rhs)
{
    return std::fabs(lhs - rhs) <= kFloatTolerance;
}

std::filesystem::path createTempDir()
{
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    std::mt19937 rng(static_cast<unsigned>(now));
    std::filesystem::path dir = std::filesystem::temp_directory_path()
        / ("praktor_script_shell_" + std::to_string(rng()));
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

std::string escape_script_string(std::string value)
{
    std::string escaped;
    escaped.reserve(value.size() * 2);
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

void writeFile(const std::filesystem::path& path, const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::string pythonExecutable()
{
#ifdef PRAKTOR_TEST_PYTHON
    return PRAKTOR_TEST_PYTHON;
#else
    return "python";
#endif
}

struct PythonHttpServer {
    std::filesystem::path dir;
    std::filesystem::path script_path;
    std::filesystem::path ready_path;
    int port = 0;

#ifdef _WIN32
    HANDLE process_handle = nullptr;
    HANDLE thread_handle = nullptr;
#else
    pid_t child_pid = -1;
#endif

    PythonHttpServer() = default;
    PythonHttpServer(const PythonHttpServer&) = delete;
    PythonHttpServer& operator=(const PythonHttpServer&) = delete;

    ~PythonHttpServer()
    {
        stop();
    }

    void start()
    {
        dir = createTempDir();
        script_path = dir / "http_echo_server.py";
        ready_path = dir / "ready.txt";
        writeFile(script_path, R"py(
import json
import pathlib
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from socketserver import TCPServer

ready_path = pathlib.Path(sys.argv[1])

class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        return

    def _send(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("X-Server", "praktor-test")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self._send(200, {
            "path": self.path,
            "auth": self.headers.get("Authorization", ""),
            "accept": self.headers.get("Accept", ""),
        })

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length).decode("utf-8") if length else ""
        try:
            parsed = json.loads(raw) if raw else None
        except Exception:
            parsed = {"_raw": raw}
        self._send(201, {
            "path": self.path,
            "auth": self.headers.get("Authorization", ""),
            "content_type": self.headers.get("Content-Type", ""),
            "body": parsed,
        })

class LoopbackHttpServer(ThreadingHTTPServer):
    def server_bind(self):
        # HTTPServer.server_bind performs reverse DNS, which can stall macOS CI.
        # The fixture uses a numeric loopback address and needs no DNS identity.
        TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.server_address[1]

server = LoopbackHttpServer(("127.0.0.1", 0), Handler)
ready_path.write_text(str(server.server_port), encoding="utf-8")
server.serve_forever()
)py");

#ifdef _WIN32
        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};
        const auto exe = std::filesystem::path(pythonExecutable()).wstring();
        const auto script = script_path.wstring();
        const auto ready = ready_path.wstring();
        std::wstring command = L"\"" + exe + L"\" \"" + script + L"\" \"" + ready + L"\"";

        check((::CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup_info,
            &process_info)));

        process_handle = process_info.hProcess;
        thread_handle = process_info.hThread;
#else
        // Other test cases may already have created threads; spawning avoids
        // allocating C++ state between fork and exec in a multithreaded process.
        auto exe = pythonExecutable();
        auto script = script_path.string();
        auto ready = ready_path.string();
        char *argv[] = {exe.data(), script.data(), ready.data(), nullptr};
        const int spawn_error = ::posix_spawnp(&child_pid, exe.c_str(), nullptr,
                                               nullptr, argv, environ);
        check((spawn_error == 0));
#endif

        // Bound fixture startup independently of HTTP request assertions.
        // This bounds fixture readiness; HTTP behavior is still asserted below.
        info("%s", praktor_test::message([&](std::ostream& out) { out << "HTTP fixture interpreter: " << pythonExecutable(); }).c_str());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline) {
            if (std::filesystem::exists(ready_path)) {
                const std::string ready_text = trim_newlines(readFile(ready_path));
                int candidate = 0;
                const auto [end, error] =
                    std::from_chars(ready_text.data(),
                                    ready_text.data() + ready_text.size(), candidate);
                if (error == std::errc{} &&
                    end == ready_text.data() + ready_text.size() &&
                    candidate > 0) {
                    port = candidate;
                    break;
                }
            }
#ifdef _WIN32
            DWORD exit_code = STILL_ACTIVE;
            check((::GetExitCodeProcess(process_handle, &exit_code)));
            info("%s", praktor_test::message([&](std::ostream& out) { out << "HTTP fixture process exit code: " << exit_code; }).c_str());
            check((exit_code == STILL_ACTIVE));
#else
            int status = 0;
            const auto exited = ::waitpid(child_pid, &status, WNOHANG);
            if (exited == child_pid) child_pid = -1;
            info("%s", praktor_test::message([&](std::ostream& out) { out << "HTTP fixture waitpid result: " << exited << ", status: " << status; }).c_str());
            check((exited == 0));
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        check((port > 0));
    }

    void stop()
    {
#ifdef _WIN32
        if (process_handle) {
            ::TerminateProcess(process_handle, 1);
            ::WaitForSingleObject(process_handle, 5000);
            ::CloseHandle(process_handle);
            process_handle = nullptr;
        }
        if (thread_handle) {
            ::CloseHandle(thread_handle);
            thread_handle = nullptr;
        }
#else
        if (child_pid > 0) {
            ::kill(child_pid, SIGTERM);
            ::waitpid(child_pid, nullptr, 0);
            child_pid = -1;
        }
#endif
        if (!dir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            dir.clear();
        }
        port = 0;
    }
};

#ifdef _WIN32
std::string shell_failure_command()
{
    return "echo stdout_line & echo stderr_line 1>&2 & exit 7";
}

std::string shell_pwd_command()
{
    return "cd";
}

std::string shell_env_echo_command(const std::string& name)
{
    return "echo %" + name + "%";
}

#else
std::string shell_failure_command()
{
    return "printf 'stdout_line\\n'; printf 'stderr_line\\n' >&2; exit 7";
}

std::string shell_pwd_command()
{
    return "pwd";
}

std::string shell_env_echo_command(const std::string& name)
{
    return "printf '%s\\n' \"$" + name + "\"";
}

#endif

struct HostAbiInterruptProbe {
    std::atomic<unsigned> checks{0};
    unsigned request_after = 2;
};

int hostAbiInterrupt(void* user_data)
{
    auto* probe = static_cast<HostAbiInterruptProbe*>(user_data);
    const auto checks = probe->checks.fetch_add(1, std::memory_order_relaxed) + 1;
    return checks >= probe->request_after ? 1 : 0;
}

turbo_script_status_t hostAbiDouble(
    void* /*user_data*/,
    const turbo_script_value_view_t* args,
    size_t arg_count,
    turbo_script_host_result_builder_t* builder)
{
    if (!args || arg_count != 1) {
        return TURBO_SCRIPT_STATUS_INVALID_ARGUMENT;
    }

    turbo_script_value_view_t output{};
    if (args[0].kind == TURBO_SCRIPT_VALUE_INT64) {
        output.kind = TURBO_SCRIPT_VALUE_INT64;
        output.as.integer = args[0].as.integer * 2;
    } else if (args[0].kind == TURBO_SCRIPT_VALUE_NUMBER) {
        output.kind = TURBO_SCRIPT_VALUE_NUMBER;
        output.as.number = args[0].as.number * 2.0;
    } else {
        return TURBO_SCRIPT_STATUS_INVALID_ARGUMENT;
    }
    return turbo_script_host_result_set_value(builder, &output);
}

} // namespace

[[maybe_unused]] static void test_turboscript_public_host_abi_supports_jit_callbacks_and_interruption_1() {
    turbo_script_ctx_t* ctx = turbo_script_init(TURBO_SCRIPT_INIT_DEFAULT);
    check((ctx != nullptr));

    turbo_script_result_t* result = nullptr;
    check((turbo_script_result_create(ctx, &result) == TURBO_SCRIPT_STATUS_OK));
    check((result != nullptr));

    turbo_script_host_function_descriptor_t descriptor{};
    descriptor.struct_size = sizeof(descriptor);
    descriptor.name = {"host_double", sizeof("host_double") - 1};
    descriptor.min_arity = 1;
    descriptor.max_arity = 1;
    check((turbo_script_context_register_host_function(
                ctx, &descriptor, hostAbiDouble, nullptr, result) == TURBO_SCRIPT_STATUS_OK));

    constexpr char kSource[] =
        "func host_entry(){return host_double(21);};"
        "func loop(n){i=0;while(i<n){i=i+1;};return i;};"
        "export(\"host_entry\");export(\"loop\");";

    turbo_script_module_options_t module_options{};
    turbo_script_module_options_init(&module_options);
    module_options.module_name = {"praktor-host-abi-contract",
                                  sizeof("praktor-host-abi-contract") - 1};

    turbo_script_module_t* module = nullptr;
    check((turbo_script_module_compile(
                ctx,
                {kSource, sizeof(kSource) - 1},
                &module_options,
                result,
                &module) == TURBO_SCRIPT_STATUS_OK));
    check((module != nullptr));

    turbo_script_instance_options_t instance_options{};
    turbo_script_instance_options_init(&instance_options);
    instance_options.mode = TURBO_SCRIPT_EXEC_JIT;

    turbo_script_instance_t* instance = nullptr;
    check((turbo_script_instance_create(
                module, &instance_options, result, &instance) == TURBO_SCRIPT_STATUS_OK));
    check((instance != nullptr));

    turbo_script_export_handle_t host_handle = 0;
    check((turbo_script_instance_resolve_export(
                instance,
                {"host_entry", sizeof("host_entry") - 1},
                result,
                &host_handle) == TURBO_SCRIPT_STATUS_OK));

    turbo_script_call_options_t call_options{};
    turbo_script_call_options_init(&call_options);
    check((turbo_script_instance_call(
                instance, host_handle, nullptr, 0, &call_options, result) ==
            TURBO_SCRIPT_STATUS_OK));

    turbo_script_value_view_t value{};
    check((turbo_script_result_get_value(result, &value) == TURBO_SCRIPT_STATUS_OK));
    check(((value.kind == TURBO_SCRIPT_VALUE_INT64 ||
             value.kind == TURBO_SCRIPT_VALUE_NUMBER)));
    if (value.kind == TURBO_SCRIPT_VALUE_INT64) {
        check((value.as.integer == 42));
    } else {
        check_within(value.as.number, 42.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(42.0));
    }

    turbo_script_export_handle_t loop_handle = 0;
    check((turbo_script_instance_resolve_export(
                instance,
                {"loop", sizeof("loop") - 1},
                result,
                &loop_handle) == TURBO_SCRIPT_STATUS_OK));

    HostAbiInterruptProbe probe;
    turbo_script_call_options_init(&call_options);
    call_options.max_steps = UINT32_MAX;
    call_options.max_loop_iterations = UINT32_MAX;
    call_options.interrupt = hostAbiInterrupt;
    call_options.interrupt_user_data = &probe;

    turbo_script_value_view_t loop_count{};
    loop_count.kind = TURBO_SCRIPT_VALUE_INT64;
    loop_count.as.integer = 100000;
    check((turbo_script_instance_call(
                instance, loop_handle, &loop_count, 1, &call_options, result) ==
            TURBO_SCRIPT_STATUS_INTERRUPTED));
    check((probe.checks.load(std::memory_order_relaxed) >= probe.request_after));

    turbo_script_error_info_t error{};
    error.struct_size = sizeof(error);
    check((turbo_script_result_get_error(result, &error) == TURBO_SCRIPT_STATUS_OK));
    check((error.status == TURBO_SCRIPT_STATUS_INTERRUPTED));
    check((error.phase == TURBO_SCRIPT_ERROR_PHASE_INTERRUPT));

    check((turbo_script_instance_destroy(instance, result) == TURBO_SCRIPT_STATUS_OK));
    check((turbo_script_module_destroy(module, result) == TURBO_SCRIPT_STATUS_OK));
    turbo_script_result_destroy(result);
    turbo_script_free(ctx);
}

spec("TurboScript public Host ABI supports JIT callbacks and interruption") {
    it("complete case") { test_turboscript_public_host_abi_supports_jit_callbacks_and_interruption_1(); }
}

[[maybe_unused]] static void test_script_engine_basics_2(int section_index) {
    WorkflowContext context;

    if (section_index == 0) {
        // TurboScript uses '=' for assignment.
        // We use ctx_set to write to workflow context.
        auto r = Praktor::Script::execute("x = 10; ctx_set(\"x_val\", x);", context);
        if (!r.success) {
            info("%s", praktor_test::message([&](std::ostream& out) { out << "Error: " << r.error_message; }).c_str());
        }
        check((r.success));
        check_within(context.getValue<double>("x_val"), 10.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(10.0));
    }

    if (section_index == 1) {
        auto r = Praktor::Script::execute("ctx_set(\"res\", 2 + 3 * 4);", context);
        if (!r.success) {
            info("%s", praktor_test::message([&](std::ostream& out) { out << "Error: " << r.error_message; }).c_str());
        }
        check((r.success));
        check_within(context.getValue<double>("res"), 14.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(14.0));
    }
}

spec("Script engine: basics") {
    it("variable assignment and context read") { test_script_engine_basics_2(0); }
    it("arithmetic") { test_script_engine_basics_2(1); }
}

[[maybe_unused]] static void test_script_engine_concurrent_executions_stay_stable_3() {
    constexpr int kThreadCount = 8;
    constexpr int kIterationsPerThread = 16;

    std::barrier start_line(kThreadCount);
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kThreadCount);

    for (int thread_index = 0; thread_index < kThreadCount; ++thread_index) {
        workers.emplace_back([&]() {
            start_line.arrive_and_wait();

            for (int iteration = 0; iteration < kIterationsPerThread; ++iteration) {
                WorkflowContext context;
                auto result = Praktor::Script::execute(R"script(
                    payload = json.parse("{\"answer\":42,\"items\":[1,2,3]}");
                    ctx.set("answer", payload.answer);
                    ctx.set("item_count", json.query(payload.items, "length(@)"));
                )script", context);

                if (!result.success ||
                    !nearlyEqual(context.getValue<double>("answer"), 42.0) ||
                    !nearlyEqual(context.getValue<double>("item_count"), 3.0)) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    check((failures.load(std::memory_order_relaxed) == 0));
}

spec("Script engine: concurrent executions stay stable") {
    it("complete case") { test_script_engine_concurrent_executions_stay_stable_3(); }
}

[[maybe_unused]] static void test_script_engine_context_execution_control_4(int section_index) {
    using Control = Praktor::Execution::ExecutionControl;
    using namespace std::chrono_literals;

    if (section_index == 0) {
        WorkflowContext context;
        auto control = std::make_shared<Control>();
        control->requestCancel();
        context.setExecutionControl(control);

        const auto result = Praktor::Script::execute(R"script(
            import("net");
            ctx.set("should_not_run", 1);
        )script", context);

        check((!(result.success)));
        check((result.error_message.find("cancel") != std::string::npos));
        check((!(context.hasKey("should_not_run"))));
    }

    if (section_index == 1) {
        WorkflowContext context;
        auto control = std::make_shared<Control>(Control::Clock::now() - 1ms);
        context.setExecutionControl(control);

        const auto result =
            Praktor::Script::execute("ctx.set(\"should_not_run\", 1);", context);

        check((!(result.success)));
        check((result.error_message.find("timed out") != std::string::npos));
        check((!(context.hasKey("should_not_run"))));
    }

    if (section_index == 2) {
        WorkflowContext context;
        auto control = std::make_shared<Control>();
        context.setExecutionControl(control);

        std::thread canceller([control]() {
            std::this_thread::sleep_for(50ms);
            control->requestCancel();
        });

        const auto started = std::chrono::steady_clock::now();
        const auto result = Praktor::Script::execute(R"script(
            import("net");
            i = 0;
            while (i < 1000000000) {
                i = i + 1;
            }
            ctx.set("should_not_run", 1);
        )script", context);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        canceller.join();

        check((!(result.success)));
        check((result.error_message.find("cancel") != std::string::npos));
        check((!(context.hasKey("should_not_run"))));
        check((elapsed < 5s));
    }
}

spec("Script engine: context execution control") {
    it("pre-cancel prevents script side effects while preserving legacy run path") { test_script_engine_context_execution_control_4(0); }
    it("expired deadline is reported distinctly") { test_script_engine_context_execution_control_4(1); }
    it("running JIT loop observes cooperative cancellation") { test_script_engine_context_execution_control_4(2); }
}

[[maybe_unused]] static void test_script_engine_execution_control_cancels_running_shell_process_5() {
    using Control = Praktor::Execution::ExecutionControl;
    using namespace std::chrono_literals;

    WorkflowContext context;
    auto control = std::make_shared<Control>();
    context.setExecutionControl(control);

#ifdef _WIN32
    const std::string command =
        "powershell -NoProfile -Command \"Start-Sleep -Seconds 10\"";
#else
    const std::string command = "sleep 10";
#endif

    std::thread canceller([control]() {
        std::this_thread::sleep_for(100ms);
        control->requestCancel();
    });

    const auto started = std::chrono::steady_clock::now();
    const auto result = Praktor::Script::execute(
        "shell.exec(\"" + escape_script_string(command) + "\");", context);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    canceller.join();

    check((!(result.success)));
    check((result.error_message.find("cancel") != std::string::npos));
    check((elapsed < 5s));
}

spec("Script engine: execution control cancels running shell process") {
    it("complete case") { test_script_engine_execution_control_cancels_running_shell_process_5(); }
}

[[maybe_unused]] static void test_script_engine_control_flow_6(int section_index) {
    WorkflowContext context;

    if (section_index == 0) {
        auto r = Praktor::Script::execute(R"(
            i = 0;
            sum = 0;
            while (i < 5) {
                sum = sum + i;
                i = i + 1;
            }
            ctx_set("sum", sum);
        )", context);
        check((r.success));
        check_within(context.getValue<double>("sum"), 10.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(10.0));
    }

    if (section_index == 1) {
        auto r = Praktor::Script::execute(R"(
            x = 10;
            if (x > 5) {
                ctx_set("branch", "then");
            } else {
                ctx_set("branch", "else");
            }
        )", context);
        check((r.success));
        check((context.getValue<std::string>("branch") == "then"));
    }
}

spec("Script engine: control flow") {
    it("while loop") { test_script_engine_control_flow_6(0); }
    it("if/else") { test_script_engine_control_flow_6(1); }
}

[[maybe_unused]] static void test_script_engine_context_integration_7(int section_index) {
    WorkflowContext context;
    context.setValue("input", 42.0);
    context.setValue("BUILD_TARGET", "all");
    context.setValue("variables.BUILD_TARGET", "all");

    if (section_index == 0) {
        auto r = Praktor::Script::execute(R"(
            val = ctx_get("input");
            ctx_set("output", val * 2);
        )", context);
        check((r.success));
        check_within(context.getValue<double>("output"), 84.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(84.0));
    }

    if (section_index == 1) {
        auto r = Praktor::Script::execute(R"(
            val = ctx.get("input");
            ctx.set("copied", val);
            log.info("copied input");
        )", context);
        check((r.success));
        check_within(context.getValue<double>("copied"), 42.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(42.0));
    }

    if (section_index == 2) {
        auto r = Praktor::Script::execute(R"(
            target = ctx.get("BUILD_TARGET");
            ctx.set("copied_target", target);
            log.info("Build of target '" + target + "' completed successfully.");
        )", context);
        check((r.success));
        check((context.getValue<std::string>("copied_target") == "all"));
    }

    if (section_index == 3) {
        context.setValue("EMPTY_TARGET", "");

        auto r = Praktor::Script::execute(R"(
            if (trim(ctx.get("BUILD_TARGET")) == "") {
                ctx.set("non_empty_branch", "empty");
            } else {
                ctx.set("non_empty_branch", "non-empty");
            }

            if (trim(ctx.get("EMPTY_TARGET")) != "") {
                ctx.set("empty_branch", "non-empty");
            } else {
                ctx.set("empty_branch", "empty");
            }
        )", context);

        check((r.success));
        check((context.getValue<std::string>("non_empty_branch") == "non-empty"));
        check((context.getValue<std::string>("empty_branch") == "empty"));
    }

    if (section_index == 4) {
        auto r = Praktor::Script::execute(R"(
            ctx.set("payload", "{\"name\":\"demo\",\"code\":7,\"items\":[\"one\",\"two\"]}");
        )", context);
        check((r.success));
        auto payload = context.getValueByPath("payload");
        check((payload["name"].as<std::string>() == "demo"));
        check((payload["code"].as<int>() == 7));
        check((payload["items"][0].as<std::string>() == "one"));
        check((payload["items"][1].as<std::string>() == "two"));
    }

    if (section_index == 5) {
        context.setTaskStatus("build", "running");
        context.setValue("workflow_status", "running");

        auto task_status = Praktor::Script::execute(R"(
            ctx.set("tasks.build.status", "success");
        )", context);
        check((!(task_status.success)));
        check((task_status.error_message.find("reserved runtime path") != std::string::npos));
        check((context.getTaskStatus("build") == "running"));

        auto workflow_status = Praktor::Script::execute(R"(
            ctx.set("workflow_status", "success");
        )", context);
        check((!(workflow_status.success)));
        check((workflow_status.error_message.find("reserved runtime path") != std::string::npos));
        check((context.getValue<std::string>("workflow_status") == "running"));
    }

    if (section_index == 6) {
        context.pushTaskScope("emit");
        auto r = Praktor::Script::execute(R"(
            ctx.output("payload", "{\"status\":\"ok\",\"value\":3}");
            ctx.output("enabled", true);
            ctx.output("count", 7);
        )", context);
        context.popTaskScope();
        check((r.success));
        auto payload = context.getValueByPath("tasks.emit.outputs.payload");
        check((payload["status"].as<std::string>() == "ok"));
        check((payload["value"].as<int>() == 3));
        check((context.getValueByPath("tasks.emit.outputs.enabled").as<bool>()));
        check((context.getValueByPath("tasks.emit.outputs.count").as<int64_t>() == 7));
    }

    if (section_index == 7) {
        context.setValue("items", WorkflowValue::parse(R"([{"name":"alpha"},{"name":"beta"}])"));

        auto r = Praktor::Script::execute(R"script(
            items = ctx.get("items");
            names = "";
            for (item in items) {
                if (names != "") {
                    names = names + ",";
                }
                names = names + item.name;
            }
            ctx.set("names", names);
            ctx.set("count", json.query(items, "length(@)"));
        )script", context);

        check((r.success));
        check((context.getValue<std::string>("names") == "alpha,beta"));
        check_within(context.getValue<double>("count"), 2.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(2.0));
    }

    if (section_index == 8) {
        context.setValue("payload", WorkflowValue::parse(R"({"name":"demo","meta":{"ok":true}})"));

        auto r = Praktor::Script::execute(R"(
            payload = ctx.get("payload");
            ctx.set("payload_name", payload.name);
            ctx.set("payload_ok", payload.meta.ok);
        )", context);

        check((r.success));
        check((context.getValue<std::string>("payload_name") == "demo"));
        check((context.getValue<bool>("payload_ok")));
    }

    if (section_index == 9) {
        context.setValue("payload_json", R"({"name":"demo","items":[1,2]})");

        auto r = Praktor::Script::execute(R"script(
            payload = ctx.get("payload_json");
            ctx.set("payload_name", payload.name);
            ctx.set("item_count", json.query(payload.items, "length(@)"));
        )script", context);

        check((r.success));
        check((context.getValue<std::string>("payload_name") == "demo"));
        check_within(context.getValue<double>("item_count"), 2.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(2.0));
    }

    if (section_index == 10) {
        auto r = Praktor::Script::execute(R"(
            parsed_object = json.parse("{\"name\":\"demo\"}");
            parsed_number = json.parse("7");
            ctx.set("parsed_name", parsed_object.name);
            ctx.set("parsed_number", parsed_number);
        )", context);

        check((r.success));
        check((context.getValue<std::string>("parsed_name") == "demo"));
        check_within(context.getValue<double>("parsed_number"), 7.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(7.0));
    }

    if (section_index == 11) {
        auto r = Praktor::Script::execute(R"(
            headers = json.parse("{\"Authorization\":\"Bearer demo\",\"Content-Type\":\"application/json\"}");
            ctx.set("auth_header", headers["Authorization"]);
            ctx.set("content_type", headers["Content-Type"]);
        )", context);

        check((r.success));
        check((context.getValue<std::string>("auth_header") == "Bearer demo"));
        check((context.getValue<std::string>("content_type") == "application/json"));
    }
}

spec("Script engine: context integration") {
    it("ctx_get reads from context") { test_script_engine_context_integration_7(0); }
    it("dot syntax is normalized before execution") { test_script_engine_context_integration_7(1); }
    it("ctx.get returns stable strings") { test_script_engine_context_integration_7(2); }
    it("native strings compare by content in conditions") { test_script_engine_context_integration_7(3); }
    it("ctx.set stores structured values") { test_script_engine_context_integration_7(4); }
    it("ctx.set rejects reserved runtime paths") { test_script_engine_context_integration_7(5); }
    it("ctx.output stores structured task outputs") { test_script_engine_context_integration_7(6); }
    it("ctx.get returns structured arrays for iteration") { test_script_engine_context_integration_7(7); }
    it("ctx.get returns structured objects for member access") { test_script_engine_context_integration_7(8); }
    it("ctx.get parses JSON-like strings into structured values") { test_script_engine_context_integration_7(9); }
    it("json.parse returns structured values") { test_script_engine_context_integration_7(10); }
    it("maps support bracket access for dashed keys") { test_script_engine_context_integration_7(11); }
}

[[maybe_unused]] static void test_script_engine_builtins_and_imported_modules_8(int section_index) {
    WorkflowContext context;

    if (section_index == 0) {
        auto r = Praktor::Script::execute(R"(
            fail("boom");
        )", context);

        check_false(r.success);
        check((r.error_message == "boom"));
    }

    if (section_index == 1) {
        auto r = Praktor::Script::execute(R"(
            ctx_set("s", sin(0));
            ctx_set("c", cos(0));
        )", context);
        if (!r.success) {
            info("%s", praktor_test::message([&](std::ostream& out) { out << "Error: " << r.error_message; }).c_str());
        }
        check((r.success));
        check_within(context.getValue<double>("s"), 0.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(0.0));
        check_within(context.getValue<double>("c"), 1.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(1.0));
    }

    if (section_index == 2) {
        auto r = Praktor::Script::execute(R"(
            js = "{\"score\": 42}";
            res = json.query(js, "score");
            ctx_set("score", res);
        )", context);
        if (!r.success) {
            info("%s", praktor_test::message([&](std::ostream& out) { out << "Error: " << r.error_message; }).c_str());
        }
        check((r.success));
        check_within(context.getValue<double>("score"), 42.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(42.0));
    }
}

spec("Script engine: builtins and imported modules") {
    it("fail marks the script as failed with message") { test_script_engine_builtins_and_imported_modules_8(0); }
    it("math functions are available by default") { test_script_engine_builtins_and_imported_modules_8(1); }
    it("json query (from feeds)") { test_script_engine_builtins_and_imported_modules_8(2); }
}

[[maybe_unused]] static void test_script_engine_resolves_relative_tbs_imports_from_workflow_source_9() {
    const auto dir = createTempDir();
    const auto module_path = dir / "result_models.tbs";
    writeFile(module_path, R"script(
        class ImportedResult {
            value: string;
        }
    )script");

    WorkflowContext context;
    context.setSourcePath((dir / "workflow.yml").string());
    auto result = Praktor::Script::execute(R"script(
        import("./result_models.tbs");
        var payload = ImportedResult();
        payload.value = "resolved";
        ctx.set("imported_value", payload.value);
    )script", context);

    info("%s", praktor_test::message([&](std::ostream& out) { out << result.error_message; }).c_str());
    check((result.success));
    check((context.getValue<std::string>("imported_value") == "resolved"));

    std::filesystem::remove_all(dir);
}

spec("Script engine resolves relative tbs imports from workflow source") {
    it("complete case") { test_script_engine_resolves_relative_tbs_imports_from_workflow_source_9(); }
}

[[maybe_unused]] static void test_script_engine_prefers_task_source_for_relative_tbs_imports_10() {
    const auto dir = createTempDir();
    const auto task_dir = dir / "included";
    writeFile(task_dir / "task_models.tbs", R"script(
        func imported_value() {
            return "task-source";
        }
    )script");

    WorkflowContext context;
    context.setSourcePath((dir / "root.yml").string());
    auto result = Praktor::Script::execute(R"script(
        import("./task_models.tbs");
        ctx.set("imported_value", imported_value());
    )script", context, (task_dir / "task.yml").string());

    info("%s", praktor_test::message([&](std::ostream& out) { out << result.error_message; }).c_str());
    check((result.success));
    check((context.getValue<std::string>("imported_value") == "task-source"));

    std::filesystem::remove_all(dir);
}

spec("Script engine prefers task source for relative tbs imports") {
    it("complete case") { test_script_engine_prefers_task_source_for_relative_tbs_imports_10(); }
}

[[maybe_unused]] static void test_script_engine_shell_module_11(int section_index) {
    WorkflowContext context;

    if (section_index == 0) {
        const std::string script =
            "ctx.set(\"shell_result\", shell.exec(\"" + escape_script_string(shell_failure_command()) + "\"));";

        auto r = Praktor::Script::execute(script, context);
        check((r.success));

        auto result = context.getValueByPath("shell_result");
        check((result.is_object()));
        check((result["exit_code"].as<int>() == 7));
        check((result["success"].as<bool>() == false));
        check((result["stdout"].as<std::string>().find("stdout_line") != std::string::npos));
        check((result["stderr"].as<std::string>().find("stderr_line") != std::string::npos));
        check((result["output_streamed_live"].as<bool>() == false));
    }

    if (section_index == 1) {
        const auto dir = createTempDir();
        const auto working_dir = dir / "work";
        std::filesystem::create_directories(working_dir);
        context.setSourcePath((dir / "workflow.yml").string());
        const std::string options = "{\"working_dir\":\"work\"}";

        const std::string script =
            "ctx.set(\"shell_result\", shell.exec(\"" + escape_script_string(shell_pwd_command()) +
            "\", \"" + escape_script_string(options) + "\"));";

        auto r = Praktor::Script::execute(script, context);
        check((r.success));

        auto result = context.getValueByPath("shell_result");
        check((result.is_object()));
        check((result["working_dir"].as<std::string>() == working_dir.lexically_normal().string()));
        check((std::filesystem::equivalent(trim_newlines(result["stdout"].as<std::string>()), working_dir)));

        std::filesystem::remove_all(dir);
    }

    if (section_index == 2) {
        const std::string env_name = "PRAKTOR_SCRIPT_SHELL_TEST";
        const std::string options = "{\"env\":{\"" + env_name + "\":\"inside\"}}";
        const std::string script =
            "ctx.set(\"shell_result\", shell.exec(\"" + escape_script_string(shell_env_echo_command(env_name)) +
            "\", \"" + escape_script_string(options) + "\"));";

        auto r = Praktor::Script::execute(script, context);
        check((r.success));

        auto result = context.getValueByPath("shell_result");
        check((result.is_object()));
        check((trim_newlines(result["stdout"].as<std::string>()) == "inside"));
        check((result["success"].as<bool>() == true));
    }
}

spec("Script engine: shell module") {
    it("shell.exec returns structured execution results") { test_script_engine_shell_module_11(0); }
    it("shell.exec resolves working_dir from workflow source path") { test_script_engine_shell_module_11(1); }
    it("shell.exec merges env overrides onto the current process environment") { test_script_engine_shell_module_11(2); }
}

[[maybe_unused]] static void test_script_engine_http_module_12(int section_index) {
    WorkflowContext context;
    PythonHttpServer server;
    server.start();

    if (section_index == 0) {
        const std::string script = R"script(
            import("net");
            resp = http.get("http://127.0.0.1:PORT/data", map{
                headers: map{
                    Authorization: "Bearer demo",
                    Accept: "application/json"
                },
                timeout: 2000
            });
            ctx.set("http_status", resp.status);
            ctx.set("http_auth", resp.data.auth);
            ctx.set("http_accept", resp.data.accept);
            ctx.set("http_path", resp.data.path);
            ctx.set("server_header", resp.headers["X-Server"]);
        )script";

        auto rendered = script;
        const std::string port = std::to_string(server.port);
        const auto pos = rendered.find("PORT");
        rendered.replace(pos, 4, port);

        auto r = Praktor::Script::execute(rendered, context);
        info("%s", praktor_test::message([&](std::ostream& out) { out << r.error_message; }).c_str());
        check((r.success));
        check_within(context.getValue<double>("http_status"), 200.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(200.0));
        check((context.getValue<std::string>("http_auth") == "Bearer demo"));
        check((context.getValue<std::string>("http_accept") == "application/json"));
        check((context.getValue<std::string>("http_path") == "/data"));
        check((context.getValue<std::string>("server_header") == "praktor-test"));
    }

    if (section_index == 1) {
        const std::string script = R"script(
            import("net");
            body = json.stringify(map{name: "demo"});
            headers = json.parse("{\"Authorization\":\"Bearer post\",\"Content-Type\":\"application/json\"}");
            resp = http.post("http://127.0.0.1:PORT/submit", body, map{
                headers: headers,
                timeout: 2000
            });
            ctx.set("post_status", resp.status);
            ctx.set("post_auth", resp.data.auth);
            ctx.set("post_type", resp.data.content_type);
            ctx.set("post_name", resp.data.body.name);
            ctx.set("post_path", resp.data.path);
        )script";

        auto rendered = script;
        const std::string port = std::to_string(server.port);
        const auto pos = rendered.find("PORT");
        rendered.replace(pos, 4, port);

        auto r = Praktor::Script::execute(rendered, context);
        info("%s", praktor_test::message([&](std::ostream& out) { out << r.error_message; }).c_str());
        check((r.success));
        check_within(context.getValue<double>("post_status"), 201.0, 100.0 * std::numeric_limits<float>::epsilon() * std::abs(201.0));
        check((context.getValue<std::string>("post_auth") == "Bearer post"));
        check((context.getValue<std::string>("post_type").find("application/json") != std::string::npos));
        check((context.getValue<std::string>("post_name") == "demo"));
        check((context.getValue<std::string>("post_path") == "/submit"));
    }
}

spec("Script engine: http module") {
    it("http.get with options returns structured response") { test_script_engine_http_module_12(0); }
    it("http.post with options returns structured JSON data") { test_script_engine_http_module_12(1); }
}
