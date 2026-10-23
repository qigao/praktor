#include "praktor_test.hpp"
#include "workflow_runner.hpp"
#include "dag/workflow_executor_internal.hpp"
#include "util/file_hash.h"
#include "util/shared_thread_pool.hpp"
#include "util/variable_substitution.hpp"


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
        check((std::filesystem::create_directory(path)));
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary);
    check((output.is_open()));
    output << content;
    check((output.good()));
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

[[maybe_unused]] static void test_parallel_nested_tasks_finalize_their_aggregate_alias_only_after_every_child_1(std::size_t row_0) {
    const bool fails = std::array{false, true}.at(row_0);
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
    info("%s", praktor_test::message([&](std::ostream& out) { out << result.error_message; }).c_str());
    check((result.success == !fails));
    check((parent_terminal_events.load() == 1));
    const auto parent = result.value.at("tasks").at("parent");
    check((parent.at("status").as<std::string>() == (fails ? "failed" : "success")));
    const auto outputs = parent.at("outputs");
    const auto children = outputs.at("nested_tasks");
    check((!(children.contains("parent"))));
    check((children.at("first_child").at("status").as<std::string>() == "success"));
    check((children.at("first_child").at("outputs").at("result").as<std::string>() == "first"));
    check((children.at("second_child").at("status").as<std::string>() ==
          (fails ? "failed" : "success")));
    check((outputs.at("workflow_status").as<std::string>() == (fails ? "failed" : "success")));
    if (fails) {
        check((result.value.at("agent_output").at("failure").at("inner")
                  .at("task_name").as<std::string>() == "second_child"));
    } else {
        check((outputs.at("result").as<std::string>() == "second"));
    }
}

spec("parallel nested tasks finalize their aggregate alias only after every child") {
    for (std::size_t row_0 = 0; row_0 < 2; ++row_0) {
        it("%s / row %zu", "complete case", row_0) { test_parallel_nested_tasks_finalize_their_aggregate_alias_only_after_every_child_1(row_0); }
    }
}

[[maybe_unused]] static void test_unexpected_worker_exceptions_stop_successors_and_drain_admitted_tasks_2() {
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
    check((execution.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout));
    check((host->finished.load() == 0));
    host->release.count_down();
    const auto result = execution.get();
    check((!(result.success)));
    check((result.value.at("workflow_status").as<std::string>() == "failed"));
    check((result.error_message.find("non-standard exception") != std::string::npos));
    check((host->active.load() == 0));
    check((host->finished.load() == 1));
    check((host->after_calls.load() == 0));
    check((workflow_completed.load() == 1));
}

spec("unexpected worker exceptions stop successors and drain admitted tasks") {
    it("complete case") { test_unexpected_worker_exceptions_stop_successors_and_drain_admitted_tasks_2(); }
}

[[maybe_unused]] static void test_executor_rejects_a_drained_graph_with_unreachable_nodes_instead_of_waiting_3(std::size_t row_0, std::size_t row_1) {
    const int concurrency = std::array{1, 2}.at(row_0);
    const bool has_runnable = std::array{false, true}.at(row_1);
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
    check((praktor_test::throws_containing([&] { (void)(executor.execute(context, "a")); }, {"collides with a task name"})));
    check((praktor_test::throws_containing([&] { (void)(executor.execute(context)); }, {"cycles or unreachable tasks"})));
    if (has_runnable) check((context.getTaskStatus("standalone") == "success"));
}

spec("executor rejects a drained graph with unreachable nodes instead of waiting") {
    for (std::size_t row_0 = 0; row_0 < 2; ++row_0) {
        for (std::size_t row_1 = 0; row_1 < 2; ++row_1) {
            it("%s / row %zu %zu", "complete case", row_0, row_1) { test_executor_rejects_a_drained_graph_with_unreachable_nodes_instead_of_waiting_3(row_0, row_1); }
        }
    }
}

[[maybe_unused]] static void test_registry_finalization_exceptions_propagate_after_sibling_tasks_drain_4() {
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
    check((execution.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout));
    host->release.count_down();
    check_throws_as(execution.get(), std::logic_error);
    check((host->active.load() == 0));
    check((host->finished.load() == 1));
    check((host->after_calls.load() == 0));
    check((context.getTaskStatus("slow") == "success"));
    // The caller's context remains usable after the drained failure.
    check_nothrow(context.setCurrentTaskOutput("after_error", true));
    check((context.getValueByPath("tasks.__root__.outputs.after_error").as<bool>()));
}

spec("registry finalization exceptions propagate after sibling tasks drain") {
    it("complete case") { test_registry_finalization_exceptions_propagate_after_sibling_tasks_drain_4(); }
}

[[maybe_unused]] static void test_serial_task_scopes_unwind_when_a_backend_throws_an_unknown_exception_5() {
    auto workflow = TaskParser::parseText(
        "tasks:\n  - name: panic\n    tool: panic\n    with: {}\n", "scope-regression");
    auto graph = TaskParser::buildGraph(workflow);
    WorkflowContext context;
    auto host = std::make_shared<ThrowingHost>();
    host->synchronize = false;
    context.setHostToolHost(host);
    WorkflowExecutor executor(graph, workflow.tasks, {}, 1);
    check((praktor_test::throws_containing([&] { (void)(executor.execute(context)); }, {"non-standard exception"})));
    check_nothrow(context.setCurrentTaskOutput("after_error", true));
    check((context.getValueByPath("tasks.__root__.outputs.after_error").as<bool>()));
    check((context.getValueByPath("tasks.panic.outputs.after_error").is_null()));
}

spec("serial task scopes unwind when a backend throws an unknown exception") {
    it("complete case") { test_serial_task_scopes_unwind_when_a_backend_throws_an_unknown_exception_5(); }
}

[[maybe_unused]] static void test_mustache_resolves_flat_namespaces_and_object_members_in_array_iterations_6() {
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

    check((substituteVariables("{{ variables.first }} + {{ variables.second }}", context) ==
          "alpha + beta"));
    check((substituteVariables("{{ env.PRAKTOR_TEMPLATE_TEST }}", context) == "test-value"));
    check((substituteVariables("{{ variables.payload.branch }}", context) == "main"));
    check((substituteVariables("{{ variables.missing }}", context).empty()));
    check((substituteVariables("{{ config.label }}", context) == "flat-value"));
    check((substituteVariables("{{#variables}}{{first}}/{{second}}{{/variables}}", context) ==
          "alpha/beta"));
    check((substituteVariables("{{#items}}{{name}}={{nested.value}};{{/items}}", context) ==
          "one=1;two=2;"));
    check((substituteVariables("{{#items}}{{name}}:{{suffix}};{{/items}}", context) ==
          "one:done;two:done;"));
}

spec("Mustache resolves flat namespaces and object members in array iterations") {
    it("complete case") { test_mustache_resolves_flat_namespaces_and_object_members_in_array_iterations_6(); }
}

[[maybe_unused]] static void test_mustache_sections_distinguish_false_and_empty_arrays_from_iterable_values_7(std::size_t row_0) {
    const std::string json = std::array{"false", "null", "[]", "true", "0", "\"\"", "{}"}.at(row_0);
    WorkflowContext context;
    context.setValue("value", WorkflowValue::parse(json));
    const bool empty = json == "false" || json == "null" || json == "[]";
    check((substituteVariables("{{#value}}present{{/value}}{{^value}}empty{{/value}}", context) ==
          (empty ? "empty" : "present")));
}

spec("Mustache sections distinguish false and empty arrays from iterable values") {
    for (std::size_t row_0 = 0; row_0 < 7; ++row_0) {
        it("%s / row %zu", "complete case", row_0) { test_mustache_sections_distinguish_false_and_empty_arrays_from_iterable_values_7(row_0); }
    }
}

[[maybe_unused]] static void test_a_waiting_pool_worker_assists_only_its_requested_workflow_group_8() {
    pubcxx::ThreadPool pool(1);
    int unrelated_group = 0;
    int nested_group = 0;
    check((!(pool.isWorkerThread())));
    auto outer = pool.enqueue([&]() {
        auto unrelated = pool.enqueueFor(&unrelated_group, []() { return 1; });
        auto nested = pool.enqueueFor(&nested_group, []() { return 2; });
        if (!pool.tryRunOneFor(&nested_group)) return false;
        return nested.get() == 2 &&
               unrelated.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
    });
    check((outer.get()));
}

spec("a waiting pool worker assists only its requested workflow group") {
    it("complete case") { test_a_waiting_pool_worker_assists_only_its_requested_workflow_group_8(); }
}

[[maybe_unused]] static void test_empty_matrix_dimensions_fail_during_parsing_and_executor_admission_9() {
    const std::string source = R"(tasks:
  - name: matrix_task
    each:
      matrix:
        platform: []
    command: echo matrix
)";
    check((praktor_test::throws_containing([&] { (void)(TaskParser::parseText(source, "matrix-regression")); }, {"must not be empty"})));

    // In-memory workflows must get the same rejection before scheduling.
    auto workflow = TaskParser::parseText(
        "tasks:\n  - name: matrix_task\n    command: echo matrix\n", "matrix-regression");
    workflow.tasks.front().each = Each{};
    workflow.tasks.front().each->matrix["platform"] = {};
    auto graph = TaskParser::buildGraph(workflow);
    check((praktor_test::throws_containing([&] { (void)(WorkflowExecutor(graph, workflow.tasks)); }, {"must not be empty"})));
}

spec("empty matrix dimensions fail during parsing and executor admission") {
    it("complete case") { test_empty_matrix_dimensions_fail_during_parsing_and_executor_admission_9(); }
}

[[maybe_unused]] static void test_non_empty_matrices_retain_every_cartesian_product_iteration_10() {
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
    info("%s", praktor_test::message([&](std::ostream& out) { out << result.error_message; }).c_str());
    check((result.success));
    const WorkflowValue iterations = result.value["tasks"]["matrix_task"]["outputs"]["iterations"];
    check((iterations.size() == 4));
    for (const auto& iteration : iterations.array_range()) {
        check((iteration["status"].as<std::string>() == "success"));
    }
}

spec("non-empty matrices retain every Cartesian-product iteration") {
    it("complete case") { test_non_empty_matrices_retain_every_cartesian_product_iteration_10(); }
}

[[maybe_unused]] static void test_saturated_shared_workers_drain_nested_workflows_on_success_failure_and_cancellation_11(std::size_t row_0) {
    const std::string mode = std::array{"success", "failed", "cancelled"}.at(row_0);
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
    info("%s", praktor_test::message([&](std::ostream& out) { out << result.error_message; }).c_str());
    check((parent_starts.load() == worker_count));
    check((parent_completions.load() == worker_count));
    check((leaf_starts.load() > 0));
    check((result.success == (mode == "success")));
    check((result.value["workflow_status"].as<std::string>() == mode));
    if (mode == "success") check((leaf_starts.load() == worker_count));
}

spec("saturated shared workers drain nested workflows on success failure and cancellation") {
    for (std::size_t row_0 = 0; row_0 < 3; ++row_0) {
        it("%s / row %zu", "complete case", row_0) { test_saturated_shared_workers_drain_nested_workflows_on_success_failure_and_cancellation_11(row_0); }
    }
}

[[maybe_unused]] static void test_nested_workflows_retain_parallelism_when_workers_are_available_12() {
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
    info("%s", praktor_test::message([&](std::ostream& out) { out << result.error_message; }).c_str());
    check((result.success));
}

spec("nested workflows retain parallelism when workers are available") {
    it("complete case") { test_nested_workflows_retain_parallelism_when_workers_are_available_12(); }
}

[[maybe_unused]] static void test_concurrent_cache_readers_tolerate_insertions_and_preserve_warm_entries_13() {
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
    char source_hash_buffer[PRAKTOR_FILE_HASH_HEX_SIZE] = {};
    check(praktor_file_hash_file(source_path.string().c_str(), source_hash_buffer,
                                 sizeof(source_hash_buffer)) == PRAKTOR_FILE_HASH_OK);
    const std::string source_hash(source_hash_buffer);
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
    info("%s", praktor_test::message([&](std::ostream& out) { out << first.error_message; }).c_str());
    check((first.success));
    for (std::size_t index = 0; index < task_count; ++index) {
        const WorkflowValue status = first.value["tasks"]["cached_" + std::to_string(index)]["status"];
        check((status.as<std::string>() == (index < task_count / 2 ? "skipped" : "success")));
    }
    const auto warm = WorkflowRunner(path.string()).execute(true, 8);
    check((warm.success));
    for (std::size_t index = 0; index < task_count; ++index) {
        check((warm.value["tasks"]["cached_" + std::to_string(index)]["status"].as<std::string>() == "skipped"));
    }
}

spec("concurrent cache readers tolerate insertions and preserve warm entries") {
    it("complete case") { test_concurrent_cache_readers_tolerate_insertions_and_preserve_warm_entries_13(); }
}

[[maybe_unused]] static void test_every_download_parameter_participates_in_the_cache_fingerprint_14(std::size_t row_0) {
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
    const int parameter = std::array{0, 1, 2, 3, 4}.at(row_0);
    Task changed = baseline;
    auto& params = std::get<DownloadParams>(changed.specifics);
    switch (parameter) {
    case 0: params.url += "-v2"; break;
    case 1: params.path = "package-v2.zip"; break;
    case 2: params.sha256[0] = 'b'; break;
    case 3: params.overwrite = false; break;
    case 4: params.timeout_ms += 1; break;
    }
    check((Praktor::Execution::Internal::computeTaskActionHash(changed) != original));
}

spec("every download parameter participates in the cache fingerprint") {
    for (std::size_t row_0 = 0; row_0 < 5; ++row_0) {
        it("%s / row %zu", "complete case", row_0) { test_every_download_parameter_participates_in_the_cache_fingerprint_14(row_0); }
    }
}

[[maybe_unused]] static void test_changed_download_url_executes_validation_instead_of_reusing_a_stale_cache_entry_15() {
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
    check((cached.success));
    check((cached.value["tasks"]["download"]["status"].as<std::string>() == "skipped"));
    std::get<DownloadParams>(workflow.tasks.front().specifics).url = "http://example.invalid/package";
    const auto changed = WorkflowRunner(workflow, WorkflowInputs{}).execute();
    check((!(changed.success)));
    check((changed.value["tasks"]["download"]["status"].as<std::string>() == "failed"));
    check((changed.value["agent_output"]["failure"]["error"].as<std::string>().find("absolute HTTPS URL") != std::string::npos));
}

spec("changed download URL executes validation instead of reusing a stale cache entry") {
    it("complete case") { test_changed_download_url_executes_validation_instead_of_reusing_a_stale_cache_entry_15(); }
}
