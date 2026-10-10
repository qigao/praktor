#include "workflow_runner.hpp"
#include "dag/workflow_executor_internal.hpp"
#include "util/file_hash.hpp"
#include "util/shared_thread_pool.hpp"
#include "util/variable_substitution.hpp"

#include <catch2/catch_all.hpp>

#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <latch>
#include <sstream>
#include <string>
#include <stdexcept>

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        static std::atomic<unsigned> sequence{0};
        path = std::filesystem::temp_directory_path() /
            ("praktor-execution-regression-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(sequence++));
        REQUIRE(std::filesystem::create_directory(path));
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    REQUIRE(output.is_open());
    output << content;
    REQUIRE(output.good());
}

class AliasHost final : public Praktor::Execution::HostToolHost {
public:
    explicit AliasHost(bool fail_second) : fail_second(fail_second) {}
    bool validate(std::string_view, const WorkflowValue&, std::string*) override { return true; }
    Praktor::Execution::HostToolResult invoke(
        std::string_view tool, const WorkflowValue&,
        const std::shared_ptr<Praktor::Execution::ExecutionControl>&,
        const std::shared_ptr<Praktor::Execution::ExecutionObserver>&) override {
        started.arrive_and_wait();
        if (tool == "second") {
            first_completed.wait();
            if (fail_second) {
                return {Praktor::Execution::HostToolStatus::Failed,
                        WorkflowValue::null(), "second task failed"};
            }
        }
        return {Praktor::Execution::HostToolStatus::Ok,
                WorkflowValue(std::string(tool)), {}};
    }
    std::latch first_completed{1};
private:
    std::barrier<> started{2};
    bool fail_second;
};

class ThrowingHost final : public Praktor::Execution::HostToolHost {
public:
    bool validate(std::string_view, const WorkflowValue&, std::string*) override { return true; }
    Praktor::Execution::HostToolResult invoke(
        std::string_view tool, const WorkflowValue&,
        const std::shared_ptr<Praktor::Execution::ExecutionControl>&,
        const std::shared_ptr<Praktor::Execution::ExecutionObserver>&) override {
        if (tool == "after") {
            ++after_calls;
            return {Praktor::Execution::HostToolStatus::Ok, WorkflowValue(true), {}};
        }
        ++active;
        if (synchronize) started.arrive_and_wait();
        if (tool == "panic") {
            --active;
            if (finalize_context) {
                // Inject a registry invariant failure outside the action's
                // exception handler, after the action returns a failed result.
                finalize_context->addFailedTask("panic", "injected early finalization");
                return {Praktor::Execution::HostToolStatus::Failed,
                        WorkflowValue::null(), "injected task failure"};
            }
            throw 42;
        }
        waiting.count_down();
        release.wait();
        --active;
        ++finished;
        return {Praktor::Execution::HostToolStatus::Ok, WorkflowValue(true), {}};
    }
    std::latch waiting{1};
    std::latch release{1};
    std::atomic<int> active{0};
    std::atomic<int> finished{0};
    std::atomic<int> after_calls{0};
    WorkflowContext* finalize_context = nullptr;
    bool synchronize = true;
private:
    std::barrier<> started{2};
};

}  // namespace

TEST_CASE("parallel nested tasks finalize their aggregate alias only after every child",
          "[executor][concurrency][uses][regression]") {
    const bool fails = GENERATE(false, true);
    TemporaryDirectory directory;
    writeFile(directory.path / "nested.yml", R"(tasks:
  - name: first_child
    tool: first
    with: {}
  - name: second_child
    tool: second
    with: {}
)");
    const auto path = directory.path / "workflow.yml";
    writeFile(path, "tasks:\n  - name: parent\n    uses: ./nested.yml\n");
    auto host = std::make_shared<AliasHost>(fails);
    std::atomic<int> parent_terminal_events{0};
    auto observer = std::make_shared<Praktor::Execution::ExecutionObserver>(
        [&](const Praktor::Execution::ExecutionEvent& event) {
            if (event.type == Praktor::Execution::ExecutionEventType::TaskCompleted &&
                event.task_name == "first_child") {
                host->first_completed.count_down();
            }
            if (event.task_name == "parent" &&
                (event.type == Praktor::Execution::ExecutionEventType::TaskCompleted ||
                 event.type == Praktor::Execution::ExecutionEventType::TaskFailed)) {
                ++parent_terminal_events;
            }
        });
    const auto result = WorkflowRunner(path.string()).executeObservedWithHostTools(
        {}, observer, host, true, 2);
    INFO(result.error_message);
    CHECK(result.success == !fails);
    CHECK(parent_terminal_events.load() == 1);
    const auto parent = result.value.at("tasks").at("parent");
    CHECK(parent.at("status").as<std::string>() == (fails ? "failed" : "success"));
    const auto outputs = parent.at("outputs");
    const auto children = outputs.at("nested_tasks");
    CHECK_FALSE(children.contains("parent"));
    CHECK(children.at("first_child").at("status").as<std::string>() == "success");
    CHECK(children.at("first_child").at("outputs").at("result").as<std::string>() == "first");
    CHECK(children.at("second_child").at("status").as<std::string>() ==
          (fails ? "failed" : "success"));
    CHECK(outputs.at("workflow_status").as<std::string>() == (fails ? "failed" : "success"));
    if (fails) {
        CHECK(result.value.at("agent_output").at("failure").at("inner")
                  .at("task_name").as<std::string>() == "second_child");
    } else {
        CHECK(outputs.at("result").as<std::string>() == "second");
    }
}

TEST_CASE("unexpected worker exceptions stop successors and drain admitted tasks",
          "[executor][concurrency][exception][regression]") {
    TemporaryDirectory directory;
    const auto path = directory.path / "workflow.yml";
    writeFile(path, R"(tasks:
  - name: panic
    tool: panic
    with: {}
  - name: slow
    tool: slow
    with: {}
  - name: after
    depends_on: [panic, slow]
    tool: after
    with: {}
)");
    auto host = std::make_shared<ThrowingHost>();
    std::atomic<int> workflow_completed{0};
    auto observer = std::make_shared<Praktor::Execution::ExecutionObserver>(
        [&](const Praktor::Execution::ExecutionEvent& event) {
            if (event.type == Praktor::Execution::ExecutionEventType::WorkflowCompleted) {
                ++workflow_completed;
            }
        });
    auto execution = std::async(std::launch::async, [&]() {
        return WorkflowRunner(path.string()).executeObservedWithHostTools(
            {}, observer, host, true, 2);
    });
    host->waiting.wait();
    CHECK(execution.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    CHECK(host->finished.load() == 0);
    host->release.count_down();
    const auto result = execution.get();
    CHECK_FALSE(result.success);
    CHECK(result.value.at("workflow_status").as<std::string>() == "failed");
    CHECK(result.error_message.find("non-standard exception") != std::string::npos);
    CHECK(host->active.load() == 0);
    CHECK(host->finished.load() == 1);
    CHECK(host->after_calls.load() == 0);
    CHECK(workflow_completed.load() == 1);
}

TEST_CASE("executor rejects a drained graph with unreachable nodes instead of waiting",
          "[executor][graph][regression]") {
    const int concurrency = GENERATE(1, 2);
    const bool has_runnable = GENERATE(false, true);
    auto workflow = TaskParser::parseText(R"(tasks:
  - name: a
    command: echo a
  - name: b
    command: echo b
)", "graph-regression");
    if (has_runnable) {
        auto standalone = TaskParser::parseText(
            "tasks:\n  - name: standalone\n    command: echo standalone\n", "graph-regression");
        workflow.tasks.push_back(standalone.tasks.front());
    }
    auto graph = TaskParser::buildGraph(workflow);
    graph.addEdge(workflow.tasks[0], workflow.tasks[1]);
    graph.addEdge(workflow.tasks[1], workflow.tasks[0]);
    WorkflowContext context;
    WorkflowExecutor executor(graph, workflow.tasks, {}, concurrency);
    CHECK_THROWS_WITH(executor.execute(context, "a"),
                      Catch::Matchers::ContainsSubstring("collides with a task name"));
    CHECK_THROWS_WITH(executor.execute(context),
                      Catch::Matchers::ContainsSubstring("cycles or unreachable tasks"));
    if (has_runnable) CHECK(context.getTaskStatus("standalone") == "success");
}

TEST_CASE("registry finalization exceptions propagate after sibling tasks drain",
          "[executor][concurrency][exception][regression]") {
    auto workflow = TaskParser::parseText(R"(tasks:
  - name: panic
    tool: panic
    with: {}
  - name: slow
    tool: slow
    with: {}
  - name: after
    depends_on: [panic, slow]
    tool: after
    with: {}
)", "finalization-regression");
    auto graph = TaskParser::buildGraph(workflow);
    WorkflowContext context;
    auto host = std::make_shared<ThrowingHost>();
    host->finalize_context = &context;
    context.setHostToolHost(host);
    WorkflowExecutor executor(graph, workflow.tasks, {}, 2);
    auto execution = std::async(std::launch::async, [&]() { executor.execute(context); });
    host->waiting.wait();
    CHECK(execution.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout);
    host->release.count_down();
    CHECK_THROWS_AS(execution.get(), std::logic_error);
    CHECK(host->active.load() == 0);
    CHECK(host->finished.load() == 1);
    CHECK(host->after_calls.load() == 0);
    CHECK(context.getTaskStatus("slow") == "success");
    // The caller's context remains usable after the drained failure.
    CHECK_NOTHROW(context.setCurrentTaskOutput("after_error", true));
    CHECK(context.getValueByPath("tasks.__root__.outputs.after_error").as<bool>());
}

TEST_CASE("serial task scopes unwind when a backend throws an unknown exception",
          "[executor][exception][scope][regression]") {
    auto workflow = TaskParser::parseText(
        "tasks:\n  - name: panic\n    tool: panic\n    with: {}\n", "scope-regression");
    auto graph = TaskParser::buildGraph(workflow);
    WorkflowContext context;
    auto host = std::make_shared<ThrowingHost>();
    host->synchronize = false;
    context.setHostToolHost(host);
    WorkflowExecutor executor(graph, workflow.tasks, {}, 1);
    CHECK_THROWS_WITH(executor.execute(context),
                      Catch::Matchers::ContainsSubstring("non-standard exception"));
    CHECK_NOTHROW(context.setCurrentTaskOutput("after_error", true));
    CHECK(context.getValueByPath("tasks.__root__.outputs.after_error").as<bool>());
    CHECK(context.getValueByPath("tasks.panic.outputs.after_error").is_null());
}

TEST_CASE("Mustache resolves flat namespaces and object members in array iterations",
          "[executor][template][regression]") {
    WorkflowContext context;
    context.setValue("variables.first", "alpha");
    context.setValue("variables.second", "beta");
    context.setValue("first", "root-first");
    context.setValue("suffix", "done");
    context.setValue("env.PRAKTOR_TEMPLATE_TEST", "test-value");
    context.setValue("variables.payload", WorkflowValue::parse(R"({"branch":"main"})"));
    context.setValue("config", WorkflowValue::parse(R"({"label":"object-value"})"));
    context.setValue("config.label", "flat-value");
    context.setValue("items", WorkflowValue::parse(
        R"([{"name":"one","nested":{"value":1}},{"name":"two","nested":{"value":2}}])"));

    CHECK(substituteVariables("{{ variables.first }} + {{ variables.second }}", context) ==
          "alpha + beta");
    CHECK(substituteVariables("{{ env.PRAKTOR_TEMPLATE_TEST }}", context) == "test-value");
    CHECK(substituteVariables("{{ variables.payload.branch }}", context) == "main");
    CHECK(substituteVariables("{{ variables.missing }}", context).empty());
    CHECK(substituteVariables("{{ config.label }}", context) == "flat-value");
    CHECK(substituteVariables("{{#variables}}{{first}}/{{second}}{{/variables}}", context) ==
          "alpha/beta");
    CHECK(substituteVariables("{{#items}}{{name}}={{nested.value}};{{/items}}", context) ==
          "one=1;two=2;");
    CHECK(substituteVariables("{{#items}}{{name}}:{{suffix}};{{/items}}", context) ==
          "one:done;two:done;");
}

TEST_CASE("Mustache sections distinguish false and empty arrays from iterable values",
          "[executor][template][regression]") {
    const std::string json = GENERATE("false", "null", "[]", "true", "0", "\"\"", "{}");
    WorkflowContext context;
    context.setValue("value", WorkflowValue::parse(json));
    const bool empty = json == "false" || json == "null" || json == "[]";
    CHECK(substituteVariables("{{#value}}present{{/value}}{{^value}}empty{{/value}}", context) ==
          (empty ? "empty" : "present"));
}

TEST_CASE("a waiting pool worker assists only its requested workflow group",
          "[executor][concurrency][regression]") {
    pubcxx::ThreadPool pool(1);
    int unrelated_group = 0;
    int nested_group = 0;
    CHECK_FALSE(pool.isWorkerThread());
    auto outer = pool.enqueue([&]() {
        auto unrelated = pool.enqueueFor(&unrelated_group, []() { return 1; });
        auto nested = pool.enqueueFor(&nested_group, []() { return 2; });
        if (!pool.tryRunOneFor(&nested_group)) return false;
        return nested.get() == 2 &&
               unrelated.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
    });
    CHECK(outer.get());
}

TEST_CASE("empty matrix dimensions fail during parsing and executor admission",
          "[executor][matrix][regression]") {
    const std::string source = R"(tasks:
  - name: matrix_task
    each:
      matrix:
        platform: []
    command: echo matrix
)";
    CHECK_THROWS_WITH(TaskParser::parseText(source, "matrix-regression"),
                      Catch::Matchers::ContainsSubstring("must not be empty"));

    // In-memory workflows must get the same rejection before scheduling.
    auto workflow = TaskParser::parseText(
        "tasks:\n  - name: matrix_task\n    command: echo matrix\n", "matrix-regression");
    workflow.tasks.front().each = Each{};
    workflow.tasks.front().each->matrix["platform"] = {};
    auto graph = TaskParser::buildGraph(workflow);
    CHECK_THROWS_WITH(WorkflowExecutor(graph, workflow.tasks),
                      Catch::Matchers::ContainsSubstring("must not be empty"));
}

TEST_CASE("non-empty matrices retain every Cartesian-product iteration",
          "[executor][matrix][regression]") {
    TemporaryDirectory directory;
    const auto path = directory.path / "workflow.yml";
    writeFile(path, R"(tasks:
  - name: matrix_task
    each:
      matrix:
        platform: [linux, windows]
        arch: [x64, arm64]
      as: config
    command: echo {{ config.platform }}-{{ config.arch }}
)");
    const auto result = WorkflowRunner(path.string()).execute();
    INFO(result.error_message);
    REQUIRE(result.success);
    const WorkflowValue iterations = result.value["tasks"]["matrix_task"]["outputs"]["iterations"];
    REQUIRE(iterations.size() == 4);
    for (const auto& iteration : iterations.array_range()) {
        CHECK(iteration["status"].as<std::string>() == "success");
    }
}

TEST_CASE("saturated shared workers drain nested workflows on success failure and cancellation",
          "[executor][concurrency][uses][regression]") {
    const std::string mode = GENERATE("success", "failed", "cancelled");
    TemporaryDirectory directory;
    const auto worker_count = Praktor::SharedThreadPool::instance().num_threads();
    std::barrier parents_started(static_cast<std::ptrdiff_t>(worker_count));
    auto control = std::make_shared<Praktor::Execution::ExecutionControl>();
    std::atomic<std::size_t> parent_starts{0};
    std::atomic<std::size_t> parent_completions{0};
    std::atomic<std::size_t> leaf_starts{0};
    auto observer = std::make_shared<Praktor::Execution::ExecutionObserver>(
        [&](const Praktor::Execution::ExecutionEvent& event) {
            if (event.task_name.starts_with("parent_") &&
                (event.type == Praktor::Execution::ExecutionEventType::TaskCompleted ||
                 event.type == Praktor::Execution::ExecutionEventType::TaskFailed)) {
                ++parent_completions;
            }
            if (event.type != Praktor::Execution::ExecutionEventType::TaskStarted) return;
            if (event.task_name.starts_with("parent_")) {
                ++parent_starts;
                // Every pool worker holds a parent before any nested work starts.
                parents_started.arrive_and_wait();
            } else if (event.task_name == "leaf") {
                ++leaf_starts;
                if (mode == "cancelled") control->requestCancel();
            }
        });
    const std::string leaf_command = mode == "failed" ? "exit 1" : "echo nested";
    writeFile(directory.path / "leaf.yml",
              "tasks:\n  - name: leaf\n    command: " + leaf_command + "\n");
    writeFile(directory.path / "middle.yml",
              "tasks:\n  - name: middle\n    uses: ./leaf.yml\n");
    std::ostringstream source;
    source << "tasks:\n";
    for (std::size_t index = 0; index < worker_count; ++index) {
        source << "  - name: parent_" << index << "\n    uses: ./middle.yml\n";
    }
    const auto path = directory.path / "workflow.yml";
    writeFile(path, source.str());
    const auto result = WorkflowRunner(path.string()).executeObserved(
        control, observer, true, static_cast<int>(worker_count));
    INFO(result.error_message);
    CHECK(parent_starts.load() == worker_count);
    CHECK(parent_completions.load() == worker_count);
    CHECK(leaf_starts.load() > 0);
    CHECK(result.success == (mode == "success"));
    CHECK(result.value["workflow_status"].as<std::string>() == mode);
    if (mode == "success") CHECK(leaf_starts.load() == worker_count);
}

TEST_CASE("nested workflows retain parallelism when workers are available",
          "[executor][concurrency][uses][regression]") {
    TemporaryDirectory directory;
    writeFile(directory.path / "nested.yml", R"(tasks:
  - name: child_a
    command: echo a
  - name: child_b
    command: echo b
)");
    const auto path = directory.path / "workflow.yml";
    writeFile(path, "tasks:\n  - name: parent\n    uses: ./nested.yml\n");
    std::barrier children_started(2);
    auto observer = std::make_shared<Praktor::Execution::ExecutionObserver>(
        [&](const Praktor::Execution::ExecutionEvent& event) {
            if (event.type == Praktor::Execution::ExecutionEventType::TaskStarted &&
                event.task_name.starts_with("child_")) {
                children_started.arrive_and_wait();
            }
        });
    const auto result = WorkflowRunner(path.string()).executeObserved({}, observer, true, 2);
    INFO(result.error_message);
    CHECK(result.success);
}

TEST_CASE("concurrent cache readers tolerate insertions and preserve warm entries",
          "[executor][concurrency][cache][regression]") {
    TemporaryDirectory directory;
    writeFile(directory.path / "source.txt", "unchanged source");
    constexpr std::size_t task_count = 64;
    std::ostringstream source;
    source << "tasks:\n";
    for (std::size_t index = 0; index < task_count; ++index) {
        writeFile(directory.path / ("marker_" + std::to_string(index)), "existing output");
        source << "  - name: cached_" << index << "\n"
               << "    command: echo cache\n"
               << "    sources: [source.txt]\n"
               << "    generates: [marker_" << index << "]\n";
    }
    const auto path = directory.path / "workflow.yml";
    writeFile(path, source.str());
    const auto workflow = TaskParser::parseFile(path.string());
    const auto source_path = directory.path / "source.txt";
    const auto source_hash = Praktor::Util::computeFileHash(source_path);
    WorkflowValue cache = WorkflowValue::array();
    for (std::size_t index = 0; index < task_count / 2; ++index) {
        WorkflowValue entry = WorkflowValue::object();
        entry["task"] = workflow.tasks[index].name;
        entry["action_hash"] = Praktor::Execution::Internal::computeTaskActionHash(workflow.tasks[index]);
        entry["sources"] = WorkflowValue::object();
        entry["sources"][source_path.string()] = source_hash;
        cache.push_back(std::move(entry));
    }
    writeFile(directory.path / ".praktor_cache", cache.to_string());
    const auto first = WorkflowRunner(path.string()).execute(true, 8);
    INFO(first.error_message);
    REQUIRE(first.success);
    for (std::size_t index = 0; index < task_count; ++index) {
        const WorkflowValue status = first.value["tasks"]["cached_" + std::to_string(index)]["status"];
        CHECK(status.as<std::string>() == (index < task_count / 2 ? "skipped" : "success"));
    }
    const auto warm = WorkflowRunner(path.string()).execute(true, 8);
    REQUIRE(warm.success);
    for (std::size_t index = 0; index < task_count; ++index) {
        CHECK(warm.value["tasks"]["cached_" + std::to_string(index)]["status"].as<std::string>() == "skipped");
    }
}

TEST_CASE("every download parameter participates in the cache fingerprint",
          "[executor][download][cache][regression]") {
    auto workflow = TaskParser::parseText(R"(tasks:
  - name: download
    download:
      url: https://example.invalid/package
      path: package.zip
      sha256: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
      overwrite: true
      timeout_ms: 30000
)", "download-cache-regression");
    const Task baseline = workflow.tasks.front();
    const auto original = Praktor::Execution::Internal::computeTaskActionHash(baseline);
    const int parameter = GENERATE(0, 1, 2, 3, 4);
    Task changed = baseline;
    auto& params = std::get<DownloadParams>(changed.specifics);
    switch (parameter) {
    case 0: params.url += "-v2"; break;
    case 1: params.path = "package-v2.zip"; break;
    case 2: params.sha256[0] = 'b'; break;
    case 3: params.overwrite = false; break;
    case 4: params.timeout_ms += 1; break;
    }
    CHECK(Praktor::Execution::Internal::computeTaskActionHash(changed) != original);
}

TEST_CASE("changed download URL executes validation instead of reusing a stale cache entry",
          "[executor][download][cache][regression]") {
    TemporaryDirectory directory;
    writeFile(directory.path / "package.zip", "previous download");
    const auto path = directory.path / "workflow.yml";
    auto workflow = TaskParser::parseText(R"(tasks:
  - name: download
    generates: [package.zip]
    download:
      url: https://example.invalid/package
      path: package.zip
      overwrite: true
)", path.string());
    WorkflowValue cache = WorkflowValue::array();
    WorkflowValue entry = WorkflowValue::object();
    entry["task"] = "download";
    entry["action_hash"] = Praktor::Execution::Internal::computeTaskActionHash(workflow.tasks.front());
    entry["sources"] = WorkflowValue::object();
    cache.push_back(std::move(entry));
    writeFile(directory.path / ".praktor_cache", cache.to_string());

    const auto cached = WorkflowRunner(workflow, WorkflowInputs{}).execute();
    REQUIRE(cached.success);
    CHECK(cached.value["tasks"]["download"]["status"].as<std::string>() == "skipped");
    std::get<DownloadParams>(workflow.tasks.front().specifics).url = "http://example.invalid/package";
    const auto changed = WorkflowRunner(workflow, WorkflowInputs{}).execute();
    CHECK_FALSE(changed.success);
    CHECK(changed.value["tasks"]["download"]["status"].as<std::string>() == "failed");
    CHECK(changed.value["agent_output"]["failure"]["error"].as<std::string>().find("absolute HTTPS URL") != std::string::npos);
}
