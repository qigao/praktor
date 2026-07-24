#include "script/script_engine.hpp"
#include "dag/workflow_context.hpp"

#include <catch2/catch_all.hpp>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
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

server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
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

        process_handle = process_info.hProcess;
        thread_handle = process_info.hThread;
#else
        child_pid = ::fork();
        REQUIRE(child_pid >= 0);
        if (child_pid == 0) {
            const auto exe = pythonExecutable();
            execlp(exe.c_str(), exe.c_str(), script_path.c_str(), ready_path.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
#endif

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (std::filesystem::exists(ready_path)) {
                port = std::stoi(trim_newlines(readFile(ready_path)));
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        REQUIRE(port > 0);
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

} // namespace

TEST_CASE("Script engine: basics", "[script]") {
    WorkflowContext context;

    SECTION("variable assignment and context read") {
        // TurboScript uses '=' for assignment.
        // We use ctx_set to write to workflow context.
        auto r = Praktor::Script::execute("x = 10; ctx_set(\"x_val\", x);", context);
        if (!r.success) {
            INFO("Error: " << r.error_message);
        }
        REQUIRE(r.success);
        CHECK(context.getValue<double>("x_val") == Catch::Approx(10.0));
    }

    SECTION("arithmetic") {
        auto r = Praktor::Script::execute("ctx_set(\"res\", 2 + 3 * 4);", context);
        if (!r.success) {
            INFO("Error: " << r.error_message);
        }
        REQUIRE(r.success);
        CHECK(context.getValue<double>("res") == Catch::Approx(14.0));
    }
}

TEST_CASE("Script engine: concurrent executions stay stable", "[script][concurrency]") {
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

    REQUIRE(failures.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("Script engine: control flow", "[script]") {
    WorkflowContext context;

    SECTION("while loop") {
        auto r = Praktor::Script::execute(R"(
            i = 0;
            sum = 0;
            while (i < 5) {
                sum = sum + i;
                i = i + 1;
            }
            ctx_set("sum", sum);
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<double>("sum") == Catch::Approx(10.0));
    }

    SECTION("if/else") {
        auto r = Praktor::Script::execute(R"(
            x = 10;
            if (x > 5) {
                ctx_set("branch", "then");
            } else {
                ctx_set("branch", "else");
            }
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("branch") == "then");
    }
}

TEST_CASE("Script engine: context integration", "[script]") {
    WorkflowContext context;
    context.setValue("input", 42.0);
    context.setValue("BUILD_TARGET", "all");
    context.setValue("variables.BUILD_TARGET", "all");

    SECTION("ctx_get reads from context") {
        auto r = Praktor::Script::execute(R"(
            val = ctx_get("input");
            ctx_set("output", val * 2);
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<double>("output") == Catch::Approx(84.0));
    }

    SECTION("dot syntax is normalized before execution") {
        auto r = Praktor::Script::execute(R"(
            val = ctx.get("input");
            ctx.set("copied", val);
            log.info("copied input");
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<double>("copied") == Catch::Approx(42.0));
    }

    SECTION("ctx.get returns stable strings") {
        auto r = Praktor::Script::execute(R"(
            target = ctx.get("BUILD_TARGET");
            ctx.set("copied_target", target);
            log.info("Build of target '" + target + "' completed successfully.");
        )", context);
        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("copied_target") == "all");
    }

    SECTION("ctx.set stores structured values") {
        auto r = Praktor::Script::execute(R"(
            ctx.set("payload", "{\"name\":\"demo\",\"code\":7,\"items\":[\"one\",\"two\"]}");
        )", context);
        REQUIRE(r.success);
        auto payload = context.getValueByPath("payload");
        CHECK(payload["name"].as<std::string>() == "demo");
        CHECK(payload["code"].as<int>() == 7);
        CHECK(payload["items"][0].as<std::string>() == "one");
        CHECK(payload["items"][1].as<std::string>() == "two");
    }

    SECTION("ctx.set rejects reserved runtime paths") {
        context.setTaskStatus("build", "running");
        context.setValue("workflow_status", "running");

        auto task_status = Praktor::Script::execute(R"(
            ctx.set("tasks.build.status", "success");
        )", context);
        CHECK_FALSE(task_status.success);
        CHECK(task_status.error_message.find("reserved runtime path") != std::string::npos);
        CHECK(context.getTaskStatus("build") == "running");

        auto workflow_status = Praktor::Script::execute(R"(
            ctx.set("workflow_status", "success");
        )", context);
        CHECK_FALSE(workflow_status.success);
        CHECK(workflow_status.error_message.find("reserved runtime path") != std::string::npos);
        CHECK(context.getValue<std::string>("workflow_status") == "running");
    }

    SECTION("ctx.output stores structured task outputs") {
        context.pushTaskScope("emit");
        auto r = Praktor::Script::execute(R"(
            ctx.output("payload", "{\"status\":\"ok\",\"value\":3}");
            ctx.output("enabled", true);
            ctx.output("count", 7);
        )", context);
        context.popTaskScope();
        REQUIRE(r.success);
        auto payload = context.getValueByPath("tasks.emit.outputs.payload");
        CHECK(payload["status"].as<std::string>() == "ok");
        CHECK(payload["value"].as<int>() == 3);
        CHECK(context.getValueByPath("tasks.emit.outputs.enabled").as<bool>());
        CHECK(context.getValueByPath("tasks.emit.outputs.count").as<int64_t>() == 7);
    }

    SECTION("ctx.get returns structured arrays for iteration") {
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

        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("names") == "alpha,beta");
        CHECK(context.getValue<double>("count") == Catch::Approx(2.0));
    }

    SECTION("ctx.get returns structured objects for member access") {
        context.setValue("payload", WorkflowValue::parse(R"({"name":"demo","meta":{"ok":true}})"));

        auto r = Praktor::Script::execute(R"(
            payload = ctx.get("payload");
            ctx.set("payload_name", payload.name);
            ctx.set("payload_ok", payload.meta.ok);
        )", context);

        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("payload_name") == "demo");
        CHECK(context.getValue<bool>("payload_ok"));
    }

    SECTION("ctx.get parses JSON-like strings into structured values") {
        context.setValue("payload_json", R"({"name":"demo","items":[1,2]})");

        auto r = Praktor::Script::execute(R"script(
            payload = ctx.get("payload_json");
            ctx.set("payload_name", payload.name);
            ctx.set("item_count", json.query(payload.items, "length(@)"));
        )script", context);

        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("payload_name") == "demo");
        CHECK(context.getValue<double>("item_count") == Catch::Approx(2.0));
    }

    SECTION("json.parse returns structured values") {
        auto r = Praktor::Script::execute(R"(
            parsed_object = json.parse("{\"name\":\"demo\"}");
            parsed_number = json.parse("7");
            ctx.set("parsed_name", parsed_object.name);
            ctx.set("parsed_number", parsed_number);
        )", context);

        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("parsed_name") == "demo");
        CHECK(context.getValue<double>("parsed_number") == Catch::Approx(7.0));
    }

    SECTION("maps support bracket access for dashed keys") {
        auto r = Praktor::Script::execute(R"(
            headers = json.parse("{\"Authorization\":\"Bearer demo\",\"Content-Type\":\"application/json\"}");
            ctx.set("auth_header", headers["Authorization"]);
            ctx.set("content_type", headers["Content-Type"]);
        )", context);

        REQUIRE(r.success);
        CHECK(context.getValue<std::string>("auth_header") == "Bearer demo");
        CHECK(context.getValue<std::string>("content_type") == "application/json");
    }
}

TEST_CASE("Script engine: builtins and imported modules", "[script]") {
    WorkflowContext context;

    SECTION("fail marks the script as failed with message") {
        auto r = Praktor::Script::execute(R"(
            fail("boom");
        )", context);

        REQUIRE_FALSE(r.success);
        CHECK(r.error_message == "boom");
    }

    SECTION("math functions are available by default") {
        auto r = Praktor::Script::execute(R"(
            ctx_set("s", sin(0));
            ctx_set("c", cos(0));
        )", context);
        if (!r.success) {
            INFO("Error: " << r.error_message);
        }
        REQUIRE(r.success);
        CHECK(context.getValue<double>("s") == Catch::Approx(0.0));
        CHECK(context.getValue<double>("c") == Catch::Approx(1.0));
    }

    SECTION("json query (from feeds)") {
        auto r = Praktor::Script::execute(R"(
            js = "{\"score\": 42}";
            res = json.query(js, "score");
            ctx_set("score", res);
        )", context);
        if (!r.success) {
            INFO("Error: " << r.error_message);
        }
        REQUIRE(r.success);
        CHECK(context.getValue<double>("score") == Catch::Approx(42.0));
    }
}

TEST_CASE("Script engine resolves relative tbs imports from workflow source",
          "[script][import]") {
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

    INFO(result.error_message);
    REQUIRE(result.success);
    CHECK(context.getValue<std::string>("imported_value") == "resolved");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Script engine prefers task source for relative tbs imports",
          "[script][import]") {
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

    INFO(result.error_message);
    REQUIRE(result.success);
    CHECK(context.getValue<std::string>("imported_value") == "task-source");

    std::filesystem::remove_all(dir);
}

TEST_CASE("Script engine: shell module", "[script][shell]") {
    WorkflowContext context;

    SECTION("shell.exec returns structured execution results") {
        const std::string script =
            "ctx.set(\"shell_result\", shell.exec(\"" + escape_script_string(shell_failure_command()) + "\"));";

        auto r = Praktor::Script::execute(script, context);
        REQUIRE(r.success);

        auto result = context.getValueByPath("shell_result");
        REQUIRE(result.is_object());
        CHECK(result["exit_code"].as<int>() == 7);
        CHECK(result["success"].as<bool>() == false);
        CHECK(result["stdout"].as<std::string>().find("stdout_line") != std::string::npos);
        CHECK(result["stderr"].as<std::string>().find("stderr_line") != std::string::npos);
        CHECK(result["output_streamed_live"].as<bool>() == false);
    }

    SECTION("shell.exec resolves working_dir from workflow source path") {
        const auto dir = createTempDir();
        const auto working_dir = dir / "work";
        std::filesystem::create_directories(working_dir);
        context.setSourcePath((dir / "workflow.yml").string());
        const std::string options = "{\"working_dir\":\"work\"}";

        const std::string script =
            "ctx.set(\"shell_result\", shell.exec(\"" + escape_script_string(shell_pwd_command()) +
            "\", \"" + escape_script_string(options) + "\"));";

        auto r = Praktor::Script::execute(script, context);
        REQUIRE(r.success);

        auto result = context.getValueByPath("shell_result");
        REQUIRE(result.is_object());
        CHECK(result["working_dir"].as<std::string>() == working_dir.lexically_normal().string());
        CHECK(trim_newlines(result["stdout"].as<std::string>()) == working_dir.lexically_normal().string());

        std::filesystem::remove_all(dir);
    }

    SECTION("shell.exec merges env overrides onto the current process environment") {
        const std::string env_name = "PRAKTOR_SCRIPT_SHELL_TEST";
        const std::string options = "{\"env\":{\"" + env_name + "\":\"inside\"}}";
        const std::string script =
            "ctx.set(\"shell_result\", shell.exec(\"" + escape_script_string(shell_env_echo_command(env_name)) +
            "\", \"" + escape_script_string(options) + "\"));";

        auto r = Praktor::Script::execute(script, context);
        REQUIRE(r.success);

        auto result = context.getValueByPath("shell_result");
        REQUIRE(result.is_object());
        CHECK(trim_newlines(result["stdout"].as<std::string>()) == "inside");
        CHECK(result["success"].as<bool>() == true);
    }
}

TEST_CASE("Script engine: http module", "[script][http]") {
    WorkflowContext context;
    PythonHttpServer server;
    server.start();

    SECTION("http.get with options returns structured response") {
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
        CAPTURE(r.error_message);
        REQUIRE(r.success);
        CHECK(context.getValue<double>("http_status") == Catch::Approx(200.0));
        CHECK(context.getValue<std::string>("http_auth") == "Bearer demo");
        CHECK(context.getValue<std::string>("http_accept") == "application/json");
        CHECK(context.getValue<std::string>("http_path") == "/data");
        CHECK(context.getValue<std::string>("server_header") == "praktor-test");
    }

    SECTION("http.post with options returns structured JSON data") {
        const std::string script = R"script(
            import("net");
            import("parser");
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
        CAPTURE(r.error_message);
        REQUIRE(r.success);
        CHECK(context.getValue<double>("post_status") == Catch::Approx(201.0));
        CHECK(context.getValue<std::string>("post_auth") == "Bearer post");
        CHECK(context.getValue<std::string>("post_type").find("application/json") != std::string::npos);
        CHECK(context.getValue<std::string>("post_name") == "demo");
        CHECK(context.getValue<std::string>("post_path") == "/submit");
    }
}
