/* Source-only application differential qualification for PR #79.
 * One literal YAML input is run by the production C++ WorkflowRunner and
 * independently by C11 Snapshot -> DAG -> Session -> CFlow Owner.
 * No backend switch, implicit legacy fallback, or production registration. */
#include "praktor_test.hpp"
#include "workflow_runner.hpp"
#include "orch_session_plan.h"
#include "native_host.h"
#include "workflow_snapshot_values.h"
#include "workflow_parallel_process_guard.h"
#include "yml/workflow_dag_state.h"
#include "yml/workflow_task_registry.h"
#include <cflow/executor.h>
#include <salts/error_codes.h>
#include <json_parser.h>
#include <vstr.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr const char *kSuccessfulYaml = R"(
variables: {seed: ready}
tasks:
  - name: prepare
    actions:
      Sequence:
        - set_variable: observed
          from: seed
  - name: verify
    depends_on: [prepare]
    actions:
      Sequence:
        - set_variable: finished
          value: done
)";

constexpr const char *kFailedYaml = R"(
tasks:
  - name: reject
    actions:
      Sequence:
        - CheckExitCode: {input_key: never_set, expected: 7}
  - name: downstream
    depends_on: [reject]
    set_variable: should_not_publish
    value: forbidden
)";

constexpr const char *kRealShellYaml = R"(
tasks:
  - name: echo
    shell:
      cmd: echo c11-owner-differential
      stream_output: false
)";

constexpr const char *kFailedShellYaml = R"(
tasks:
  - name: fail_shell
    shell:
      cmd: exit 7
      stream_output: false
  - name: forbidden
    depends_on: [fail_shell]
    set_variable: forbidden_output
    value: never
)";

constexpr const char *kTypedYaml = R"(
# Structured text is loaded through the canonical workflow variables owner.
# Literal {..} in SetVariable.value has special Legacy {blackboard_key}
# semantics and would NOT be an apples-to-apples JSON-output fixture.
variables:
  NUMERIC_INPUT_TEXT: '7'
  BOOLEAN_INPUT_TEXT: 'false'
  JSON_OBJECT_TEXT: '{"count":7,"ok":true}'
  JSON_ARRAY_TEXT: '["ready",2,false]'
  INVALID_JSON_TEXT: '{invalid'
tasks:
  - name: typed
    actions:
      Sequence:
        - set_variable: plain
          value: 'alpha beta'
        - set_variable: numeric_text
          from: NUMERIC_INPUT_TEXT
        - set_variable: boolean_text
          from: BOOLEAN_INPUT_TEXT
        - set_variable: object_value
          from: JSON_OBJECT_TEXT
        - set_variable: array_value
          from: JSON_ARRAY_TEXT
        - set_variable: malformed_json
          from: INVALID_JSON_TEXT
        - set_variable: escaped_text
          value: 'say "hi"'
)";

void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

/* Independent source-only native admission: a real external Shell writes a
 * marker before blocking. Waiting for the marker proves the child actually
 * started, unlike a TaskStarted observer event or a successful CFlow callback.
 * This fixture never switches the production WorkflowExecutor. */
struct ActiveShellFixture {
    std::filesystem::path directory;
    std::filesystem::path started;

    ActiveShellFixture() {
        static std::atomic<unsigned> sequence{0u};
        const auto tick = std::chrono::steady_clock::now()
                              .time_since_epoch().count();
        directory = std::filesystem::temp_directory_path() /
            ("praktor-owner-cancel-" + std::to_string(tick) + "-" +
             std::to_string(sequence.fetch_add(1u)));
        require(std::filesystem::create_directory(directory),
                "Could not create a unique running-Shell test directory");
        started = directory / "child-started.txt";
    }
    ~ActiveShellFixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    std::string yaml() const {
        std::string command;
#ifdef _WIN32
        std::string path = started.string();
        for (std::size_t i = 0u; (i = path.find('\'', i)) != std::string::npos;
             i += 2u)
            path.insert(i, 1u, '\'');
        command =
            "powershell.exe -NoProfile -NonInteractive -Command \""
            "[System.IO.File]::WriteAllText('" + path +
            "', 'started'); Start-Sleep -Seconds 30\"";
#else
        command = "printf started > '" + started.string() +
                  "'; exec sleep 30";
#endif
        return "variables: {baseline: safe}\n"
               "tasks:\n"
               "  - name: long_shell\n"
               "    shell:\n"
               "      cmd: |-\n"
               "        " + command + "\n"
               "      stream_output: false\n"
               "  - name: never_started\n"
               "    depends_on: [long_shell]\n"
               "    set_variable: forbidden\n"
               "    value: visible\n";
    }
};

enum class StartCondition { Normal, PreCancelled, ExpiredDeadline, CancelActiveShell };
enum class NativePlanMode { PlainLeaves, OneShellRoot };

struct LegacyTrace {
    WorkflowExecutionResult result;
    std::vector<std::string> started;
    std::vector<std::string> settled;
};

LegacyTrace runLegacy(const std::string& source, StartCondition start) {
    LegacyTrace trace;
    std::mutex mutex;
    auto observer = std::make_shared<Praktor::Execution::ExecutionObserver>(
        [&](const Praktor::Execution::ExecutionEvent& event) {
            if (event.type == Praktor::Execution::ExecutionEventType::TaskStarted ||
                event.type == Praktor::Execution::ExecutionEventType::TaskCompleted ||
                event.type == Praktor::Execution::ExecutionEventType::TaskFailed) {
                std::lock_guard<std::mutex> lock(mutex);
                if (event.type == Praktor::Execution::ExecutionEventType::TaskStarted)
                    trace.started.push_back(event.task_name);
                else
                    trace.settled.push_back(event.task_name + ":" + event.status);
            }
        });
    using Control = Praktor::Execution::ExecutionControl;
    auto control = start == StartCondition::ExpiredDeadline
        ? std::make_shared<Control>(Control::Clock::now() -
                                    std::chrono::milliseconds(1))
        : std::make_shared<Control>();
    if (start == StartCondition::PreCancelled) control->requestCancel();
    auto workflow = TaskParser::parseText(source, "c11-owner-differential.yml");
    trace.result = WorkflowRunner(std::move(workflow), WorkflowInputs{})
                       .executeObserved(control, observer, false, 1);
    return trace;
}

struct NativeTrace {
    std::vector<std::string> started;
    std::vector<std::string> settled;
    std::map<std::string, std::string> outputs;
    // Serialized values from the actual C11 TaskRegistry (not guessed
    // from private Host text). This records exact JSON types after commit.
    std::map<std::string, std::string> typed_outputs;
    std::map<std::string, std::string> private_failure_ports;
    std::map<std::string, std::string> typed_failure_outputs;
    std::map<std::string, std::string> failure_diagnostics;
    std::map<std::string, std::string> states;
    std::string terminal;
    size_t completed = 0;
    size_t callbacks = 0;
    size_t finalized = 0;
    bool child_marker_observed = false;
    int native_cancel_status = SALTS_EINVAL;
};

struct NativeTask {
    praktor_c11_native_host host{};
    praktor_c11_bt_host hooks{};
    praktor_c11_session_config config{};
    praktor_c11_session session{};
    praktor_c11_orch_session_plan *plan = nullptr;
    bool host_ready = false;
    bool session_ready = false;

    ~NativeTask() {
        if (plan) praktor_c11_orch_session_plan_destroy(plan);
        if (session_ready) check(praktor_c11_session_destroy(&session) == SALTS_OK);
        if (host_ready) praktor_c11_native_host_destroy(&host);
    }
};

struct OwnerCallback {
    NativeTask *task = nullptr;
    salts_bt_node_status outcome = SALTS_BT_FAILURE;
    int status = SALTS_EINVAL;
    unsigned ran = 0, cancelled = 0, finalized = 0;
    bool drive_until_terminal = false;
};

void runNative(void *user) {
    auto& callback = *static_cast<OwnerCallback *>(user);
    ++callback.ran;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(8);
    do {
        callback.status = praktor_c11_session_tick(
            &callback.task->session, &callback.outcome);
        if (!callback.drive_until_terminal || callback.status != SALTS_OK ||
            callback.outcome != SALTS_BT_RUNNING)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
}
void cancelNative(void *user) {
    ++static_cast<OwnerCallback *>(user)->cancelled;
}
void finalizeNative(void *user) {
    ++static_cast<OwnerCallback *>(user)->finalized;
}

struct OwnerExecutor {
    cflow_executor executor{};
    cflow_executor_control control{};
    bool initialized = false;

    OwnerExecutor() = default;
    void init() {
        require(cflow_executor_manual_init_with_capacity(&executor, 1u),
                "C11 CFlow ManualExecutor admission failed");
        initialized = true;
        require(cflow_executor_as_control(&executor, &control),
                "C11 CFlow control acquisition failed");
    }
    ~OwnerExecutor() {
        if (initialized) cflow_executor_destroy(&executor);
    }

    void run(OwnerCallback& callback) {
        cflow_executor_task descriptor{};
        descriptor.run = runNative;
        descriptor.cancel = cancelNative;
        descriptor.finalize = finalizeNative;
        descriptor.user = &callback;
        require(cflow_executor_try_post_task(&executor, &descriptor) ==
                    CFLOW_ADMISSION_ACCEPTED, "C11 callback admission failed");
        require(cflow_executor_run_ready(&executor) == 1u,
                "C11 callback was not executed");
        require(cflow_executor_control_shutdown(
                    &control, CFLOW_EXECUTOR_SHUTDOWN_DRAIN),
                "C11 CFlow shutdown failed");
        require(cflow_executor_control_wait_idle(&control) ==
                    CFLOW_EXECUTOR_WAIT_IDLE,
                "C11 CFlow did not finish/finalize");
        require(callback.ran == 1u && callback.cancelled == 0u &&
                    callback.finalized == 1u, "C11 callback settlement mismatch");
        require(callback.status == SALTS_OK, "C11 Session tick failed");
    }
};

struct NativeOwner {
    praktor_workflow_snapshot *snapshot = nullptr;
    praktor_workflow_dag_state *dag = nullptr;
    praktor_workflow_task_registry *registry = nullptr;
    praktor_c11_value_store root{};
    cflow_process_retainer retainer{};
    bool root_ready = false, retainer_ready = false;

    NativeOwner() = default;
    // Separate initialization from construction: exceptions preserve the
    // cleanup owner, including a partially admitted Snapshot or retainer.
    void init(const std::string& source) {
        char error[256]{};
        require(praktor_workflow_snapshot_create_inline(
                    vstr_from_cstr(source.c_str()), &snapshot,
                    error, sizeof(error)) == SALTS_OK,
                "C11 canonical YAML snapshot rejected parity input");
        require(praktor_workflow_task_registry_create(snapshot, &registry) ==
                    SALTS_OK, "C11 TaskRegistry init failed");
        require(praktor_workflow_dag_state_create(
                    praktor_workflow_snapshot_dependencies(snapshot),
                    1u, &dag) == SALTS_OK, "C11 DAG init failed");
        require(praktor_c11_value_store_init(&root, 24u, 64u, 128u) ==
                    PRAKTOR_C11_VALUE_OK, "C11 root init failed");
        root_ready = true;
        require(praktor_c11_workflow_seed_root_text_variables(snapshot, &root) ==
                    PRAKTOR_C11_VALUE_OK, "C11 input variable admission failed");
        require(cflow_process_retainer_init(&retainer, 2u) == SALTS_OK,
                "C11 Process recovery retainer init failed");
        retainer_ready = true;
    }
    void drainRetainerAfterFinalizers() {
        require(retainer_ready, "C11 Process retainer missing");
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(8);
        int status = SALTS_EBUSY;
        size_t pending = SIZE_MAX;
        do {
            size_t reaped = 0u;
            status = cflow_process_retainer_run_ready(
                &retainer, 64u, &reaped, &pending);
            require(status == SALTS_OK || status == SALTS_EBUSY,
                    "C11 native Process recovery error");
            if (status == SALTS_OK && pending == 0u) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(
            "C11 native Process lease did not settle before owner terminal");
    }

    ~NativeOwner() {
        if (retainer_ready)
            check(cflow_process_retainer_destroy(&retainer) == SALTS_OK);
        if (root_ready) praktor_c11_value_store_destroy(&root);
        if (dag) praktor_workflow_dag_state_destroy(dag);
        if (registry) praktor_workflow_task_registry_destroy(registry);
        if (snapshot) praktor_workflow_snapshot_destroy(snapshot);
    }

    NativeTrace run(const std::vector<std::string>& names,
                    const std::vector<std::vector<std::string>>& output_keys,
                    StartCondition start,
                    NativePlanMode mode = NativePlanMode::PlainLeaves,
                    std::filesystem::path active_marker = {}) {
        require(names.size() == praktor_workflow_snapshot_task_count(snapshot),
                "C11 task count differs from shared fixture");
        require(names.size() == output_keys.size(), "Invalid differential fixture");
        NativeTrace trace;
        if (start == StartCondition::CancelActiveShell)
            require(mode == NativePlanMode::OneShellRoot &&
                    !active_marker.empty() && names.size() == 2u,
                    "In-flight cancellation requires one Shell and a marker");
        if (start == StartCondition::PreCancelled ||
            start == StartCondition::ExpiredDeadline)
            require(praktor_workflow_dag_stop(
                        dag, start == StartCondition::PreCancelled
                            ? PRAKTOR_WORKFLOW_DAG_CANCELLED
                            : PRAKTOR_WORKFLOW_DAG_DEADLINE) == SALTS_OK,
                    "C11 pre-start stop failed");
        for (;;) {
            size_t index = SIZE_MAX, count = 0u;
            const int claim = praktor_workflow_dag_claim_ready(
                dag, &index, 1u, &count);
            if (claim == SALTS_EBUSY) {
                require(count == 0u, "Stopped C11 DAG admitted task");
                break;
            }
            require(claim == SALTS_OK, "C11 DAG claim error");
            if (count == 0u) break;
            require(count == 1u && index < names.size(), "C11 DAG invalid claim");
            vstr observed_name{};
            require(praktor_workflow_task_registry_name(
                        registry, index, &observed_name) == SALTS_OK &&
                        vstr_eq(observed_name, vstr_from_cstr(names[index].c_str())),
                    "C11 task name/index mismatch");
            trace.started.push_back(names[index]);
            NativeTask task;
            praktor_c11_native_host_config hc{};
            praktor_c11_native_host_config_init(&hc, 24u, 64u, 128u);
            require(praktor_c11_native_host_init_with_config(
                        &task.host, &hc) == SALTS_OK, "C11 Host init failed");
            task.host_ready = true;
            require(praktor_c11_native_host_bind(&task.host, &task.hooks) ==
                        SALTS_OK, "C11 Host hook bind failed");
            auto *task_values = praktor_c11_native_host_values(&task.host);
            require(praktor_c11_workflow_load_task_text_scope(
                        snapshot, index, &root, task_values) ==
                        PRAKTOR_C11_VALUE_OK, "C11 task scope admission failed");
            praktor_c11_session_config_init(&task.config);
            task.config.host = task.hooks;
            if (mode == NativePlanMode::OneShellRoot)
                task.config.retainer = &retainer;
            if (start == StartCondition::CancelActiveShell)
                task.config.shell_close_budget_ms = 0u;
            char error[256]{};
            const int plan_status = mode == NativePlanMode::OneShellRoot
                ? praktor_c11_orch_session_plan_create_native_shell(
                      praktor_workflow_snapshot_task_tree(snapshot, index),
                      &task.config.tree, &task.plan, error, sizeof(error))
                : praktor_c11_orch_session_plan_create(
                      praktor_workflow_snapshot_task_tree(snapshot, index),
                      &task.config.tree, &task.plan, error, sizeof(error));
            require(plan_status == SALTS_OK && task.plan != nullptr,
                    "C11 native Session plan refused actual YAML");
            require(praktor_c11_orch_session_plan_init_session(
                        task.plan, &task.config, &task.session) == SALTS_OK,
                    "C11 Session init failed");
            task.session_ready = true;
            praktor_c11_orch_session_plan_destroy(task.plan);
            task.plan = nullptr;

            OwnerExecutor executor;
            executor.init();
            OwnerCallback callback{};
            callback.task = &task;
            callback.drive_until_terminal =
                mode == NativePlanMode::OneShellRoot &&
                start != StartCondition::CancelActiveShell;
            executor.run(callback);
            ++trace.callbacks;
            trace.finalized += callback.finalized;

            if (start == StartCondition::CancelActiveShell) {
                // The callback is FINALIZED but the actual native Process and
                // Session remain RUNNING. No next task or output may publish.
                require(callback.outcome == SALTS_BT_RUNNING,
                        "Cancel probe did not admit a real RUNNING Shell");
                int visible = 1;
                require(praktor_c11_value_store_contains(
                            &root, "shell_exit_code", &visible) ==
                            PRAKTOR_C11_VALUE_OK && visible == 0,
                        "C11 running Shell published an output prematurely");
                const auto deadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(7);
                std::error_code error_code;
                while (std::chrono::steady_clock::now() < deadline) {
                    if (std::filesystem::exists(active_marker, error_code)) {
                        trace.child_marker_observed = true;
                        break;
                    }
                    require(!error_code, "Could not inspect native child marker");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                require(trace.child_marker_observed,
                        "Real native Shell did not reach its child marker");

                // Cancel-first application admission barrier: no dependent
                // task can start after this owner turn is marked cancelled.
                require(praktor_workflow_dag_stop(
                            dag, PRAKTOR_WORKFLOW_DAG_CANCELLED) == SALTS_OK,
                        "C11 cancellation failed to stop DAG admission");
                trace.native_cancel_status =
                    praktor_c11_session_cancel(&task.session);
                require(trace.native_cancel_status == SALTS_OK ||
                        trace.native_cancel_status == SALTS_EBUSY,
                        "C11 Session rejected in-flight cancellation");

                // Session destroy is the actual Process-lease transfer. It
                // MUST happen before the sole Owner drives the retainer;
                // callback settlement alone is not Process settlement.
                require(praktor_c11_session_destroy(&task.session) == SALTS_OK,
                        "C11 Session failed to hand off live Process owner");
                task.session_ready = false;
                drainRetainerAfterFinalizers();
                require(praktor_workflow_task_registry_fail(
                            registry, index, vstr_from_cstr("cancelled")) ==
                            SALTS_OK, "C11 cancelled task finalization failed");
                require(praktor_workflow_dag_finish(dag, index, false) ==
                            SALTS_OK, "C11 cancelled DAG settlement failed");
                trace.states[names[index]] = "failed";
                trace.settled.push_back(names[index] + ":failed");
                continue;
            }

            // A completed CFlow callback does not imply a terminal Session.
            require(callback.outcome != SALTS_BT_RUNNING,
                    "C11 Session still running after one CFlow callback");
            if (callback.outcome != SALTS_BT_SUCCESS) {
                require(callback.outcome == SALTS_BT_FAILURE,
                        "C11 Session returned an unknown state");
                // A failed callback may still have asynchronous Process close
                // evidence. Do not finalize the app DAG or task registry
                // while any native lease remains owned by the retainer.
                drainRetainerAfterFinalizers();
                if (mode == NativePlanMode::OneShellRoot) {
                    char exit_text[64]{};
                    size_t exit_len = 0u;
                    require(praktor_c11_value_store_get(
                                task_values, "shell_exit_code",
                                exit_text, sizeof(exit_text), &exit_len) ==
                                PRAKTOR_C11_VALUE_OK,
                            "C11 failed Shell lost its private exit status");
                    trace.private_failure_ports[names[index]] =
                        std::string(exit_text, exit_len);
                    const char *detail = praktor_c11_session_error(&task.session);
                    trace.failure_diagnostics[names[index]] =
                        detail ? detail : "";
                    // The production Legacy TaskResult publishes exit_code
                    // even after an unsuccessful Shell. C11 must preserve
                    // that typed task diagnostic but NEVER commit a failed
                    // task's private text ports into the canonical root.
                    json_value_t *typed_exit = json_create_string_n(
                        exit_text, exit_len);
                    require(typed_exit != nullptr,
                            "C11 failure output allocation failed");
                    const int set_status =
                        praktor_workflow_task_registry_set_output(
                            registry, index, vstr_from_cstr("exit_code"),
                            typed_exit);
                    json_free(typed_exit);
                    require(set_status == SALTS_OK,
                            "C11 failed Shell diagnostic publication failed");
                }
                require(praktor_workflow_task_registry_fail(
                            registry, index, vstr_from_cstr("Action execution failed")) ==
                            SALTS_OK, "C11 failed TaskRegistry settlement failed");
                require(praktor_workflow_dag_finish(dag, index, false) == SALTS_OK,
                        "C11 failed DAG settlement failed");
                if (mode == NativePlanMode::OneShellRoot) {
                    const json_value_t *typed_exit = nullptr;
                    require(praktor_workflow_task_registry_get_output(
                                registry, index, vstr_from_cstr("exit_code"),
                                &typed_exit) == SALTS_OK && typed_exit != nullptr,
                            "C11 failed Shell lost its terminal TaskRegistry output");
                    char *encoded = json_serialize(typed_exit, nullptr);
                    require(encoded != nullptr,
                            "C11 failed Shell output serialization failed");
                    trace.typed_failure_outputs[names[index]] = encoded;
                    json_serialize_free(encoded);
                } else {
                    const json_value_t *leaked_outputs = nullptr;
                    require(praktor_workflow_task_registry_get_outputs(
                                registry, index, &leaked_outputs) == SALTS_ENOENT,
                            "Ordinary failed C11 task published an output");
                }
                trace.states[names[index]] = "failed";
                trace.settled.push_back(names[index] + ":failed");
                continue;
            }

            const auto& keys = output_keys[index];
            require(!keys.empty(), "Successful native case lacks selected outputs");
            std::vector<std::string> values;
            std::vector<const char *> selected;
            values.reserve(keys.size());
            selected.reserve(keys.size());
            // Every native text value originates from the actual Session Host;
            // the adapter may not manufacture a success output or guess its
            // type without the published typed JSON evidence.
            for (const auto& key : keys) {
                require(!key.empty(), "C11 selected output name is empty");
                char buffer[256]{};
                size_t length = 0u;
                require(praktor_c11_value_store_get(
                            task_values, key.c_str(), buffer, sizeof(buffer),
                            &length) == PRAKTOR_C11_VALUE_OK,
                        "C11 successful leaf did not produce selected output");
                values.emplace_back(buffer, length);
                selected.push_back(key.c_str());
            }
            struct JsonCleanup {
                void operator()(json_value_t *value) const noexcept {
                    json_free(value);
                }
            };
            std::unique_ptr<json_value_t, JsonCleanup> typed(json_create_object());
            require(typed != nullptr, "C11 output JSON owner allocation failed");
            for (size_t n = 0u; n < keys.size(); ++n) {
                const auto& raw = values[n];
                // Match the established Legacy parseWorkflowValue() rule:
                // only object/array prefixes attempt structured JSON; a
                // failed parse, scalar JSON text, or an empty text is a string.
                json_value_t *item = nullptr;
                if (!raw.empty() && (raw.front() == '{' || raw.front() == '['))
                    item = json_parse(raw.data(), raw.size());
                if (!item)
                    item = json_create_string_n(raw.data(), raw.size());
                require(item != nullptr, "C11 typed output projection failed");
                if (!json_object_add_n(typed.get(), keys[n].data(),
                                       keys[n].size(), item)) {
                    json_free(item);
                    throw std::runtime_error("C11 typed output insertion failed");
                }
            }
            struct TxnCleanup {
                praktor_c11_value_txn value{};
                ~TxnCleanup() { praktor_c11_value_txn_abort(&value); }
            } transaction;
            require(praktor_c11_workflow_prepare_task_text_outputs(
                        &root, task_values, selected.data(), selected.size(),
                        &transaction.value) == PRAKTOR_C11_VALUE_OK,
                    "C11 output txn preparation failed");
            praktor_workflow_task_output_update update{};
            update.task_index = index;
            update.updates = typed.get();
            size_t pending = SIZE_MAX;
            int commit = PRAKTOR_C11_PARALLEL_BUSY;
            const auto close_deadline = std::chrono::steady_clock::now() +
                                        std::chrono::seconds(8);
            // Recovery sweeps are permitted; this NEVER reruns the Shell
            // action or retries a failed workflow. The same exclusive
            // application Owner holds the transaction until all real leases
            // are reaped. A native recovery error remains a hard failure.
            do {
                commit = praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
                    registry, &update, &transaction.value, 1u,
                    &executor.control, &retainer, 64u, &pending);
                if (commit != PRAKTOR_C11_PARALLEL_BUSY) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (std::chrono::steady_clock::now() < close_deadline);
            require(commit == PRAKTOR_C11_PARALLEL_OK && pending == 0u,
                    "C11 publication before full CFlow and native Process settlement");
            require(praktor_workflow_task_registry_complete(
                        registry, index) == SALTS_OK, "C11 registry complete failed");
            require(praktor_workflow_dag_finish(dag, index, true) == SALTS_OK,
                    "C11 DAG finish failed");
            for (size_t n = 0u; n < keys.size(); ++n) {
                const json_value_t *published = nullptr;
                require(praktor_workflow_task_registry_get_output(
                            registry, index, vstr_from_cstr(keys[n].c_str()),
                            &published) == SALTS_OK && published != nullptr,
                        "C11 typed output not published");
                char *json_text = json_serialize(published, nullptr);
                require(json_text != nullptr, "C11 published JSON could not serialize");
                trace.typed_outputs[keys[n]] = json_text;
                json_serialize_free(json_text);
                trace.outputs[keys[n]] = values[n];
            }
            trace.states[names[index]] = "success";
            trace.settled.push_back(names[index] + ":success");
        }
        // Final quiescence check covers both successful and failed native
        // tasks; failures never manufacture a completed close acknowledgment.
        drainRetainerAfterFinalizers();
        praktor_workflow_dag_stats stats{};
        require(praktor_workflow_dag_get_stats(dag, &stats) == SALTS_OK,
                "C11 DAG stats unavailable");
        require(stats.active == 0u && stats.ready == 0u && stats.quiescent,
                "C11 DAG not quiescent after settlement");
        require(stats.completed == trace.started.size(),
                "C11 DAG completed count does not match actual executions");
        require(trace.callbacks == trace.finalized &&
                    trace.finalized == trace.started.size(),
                "C11 run/finalize count mismatch");
        trace.completed = stats.completed;
        if (stats.terminal == PRAKTOR_WORKFLOW_DAG_CANCELLED) {
            trace.terminal = "cancelled";
        } else if (stats.terminal == PRAKTOR_WORKFLOW_DAG_DEADLINE) {
            trace.terminal = "timed_out";
        } else if (stats.terminal == PRAKTOR_WORKFLOW_DAG_FAILED) {
            trace.terminal = "failed";
        } else if (stats.terminal == PRAKTOR_WORKFLOW_DAG_CONTINUE &&
                   stats.all_succeeded) {
            trace.terminal = "success";
        } else {
            throw std::runtime_error("C11 DAG reached an unexpected terminal state");
        }
        return trace;
    }
};

std::string legacyTerminal(const LegacyTrace& trace) {
    return trace.result.value.at("workflow_status").as<std::string>();
}

} // namespace

spec("WorkflowRunner vs C11 Owner: identical YAML and input differential") {
    it("compares dependency order, selected string outputs and quiescent success") {
        const std::string yaml(kSuccessfulYaml);
        const auto legacy = runLegacy(yaml, StartCondition::Normal);
        NativeOwner owner;
        owner.init(yaml);
        const auto native = owner.run({"prepare", "verify"},
                                      {{"observed"}, {"finished"}}, StartCondition::Normal);
        check(legacy.result.success);
        check(legacyTerminal(legacy) == "success");
        check(native.terminal == legacyTerminal(legacy));
        check((legacy.started == native.started));
        check((legacy.settled == native.settled));
        check((native.started == std::vector<std::string>{"prepare", "verify"}));
        check(native.completed == 2u);
        for (const auto& entry : std::vector<std::pair<std::string, std::string>>{
                 {"prepare", "observed"}, {"verify", "finished"}}) {
            check((native.states.at(entry.first) ==
                legacy.result.value.at("tasks").at(entry.first)
                    .at("status").as<std::string>()));
            check((native.outputs.at(entry.second) ==
                legacy.result.value.at("tasks").at(entry.first)
                    .at("outputs").at(entry.second).as<std::string>()));
        }
    }

    it("compares structured values, text scalars, invalid JSON and escaped output") {
        const std::string yaml(kTypedYaml);
        const auto legacy = runLegacy(yaml, StartCondition::Normal);
        NativeOwner owner;
        owner.init(yaml);
        const std::vector<std::string> names{
            "plain", "numeric_text", "boolean_text", "object_value",
            "array_value", "malformed_json", "escaped_text"};
        const auto native = owner.run({"typed"}, {names}, StartCondition::Normal);
        check(legacy.result.success);
        check(legacyTerminal(legacy) == "success");
        check(native.terminal == legacyTerminal(legacy));
        check((legacy.started == native.started));
        check((legacy.settled == native.settled));
        check((native.started == std::vector<std::string>{"typed"}));
        check(native.completed == 1u);
        const auto& outputs = legacy.result.value.at("tasks")
                                  .at("typed").at("outputs");
        for (const auto& name : names) {
            // Compare canonical JSON serialization so "7" never passes as 7
            // and structured/nested output never passes as an opaque string.
            const auto& actual = native.typed_outputs.at(name);
            const std::string expected = outputs.at(name).to_string();
            if (actual != expected) {
                info("Typed Owner differential: key=%s C11=%s Legacy=%s",
                     name.c_str(), actual.c_str(), expected.c_str());
            }
            check((actual == expected));
        }
        check(outputs.at("plain").is_string());
        check(outputs.at("numeric_text").is_string());
        check(outputs.at("boolean_text").is_string());
        check(outputs.at("object_value").is_object());
        check(outputs.at("array_value").is_array());
        check(outputs.at("malformed_json").is_string());
        check(outputs.at("escaped_text").is_string());
        check(native.callbacks == 1u && native.finalized == 1u);
    }

    it("compares real Shell stdout, exit and close-before-publication") {
        const std::string yaml(kRealShellYaml);
        const auto legacy = runLegacy(yaml, StartCondition::Normal);
        NativeOwner owner;
        owner.init(yaml);
        const auto native = owner.run({"echo"},
                                     {{"shell_output", "shell_exit_code"}},
                                     StartCondition::Normal,
                                     NativePlanMode::OneShellRoot);
        check(legacy.result.success);
        check(legacyTerminal(legacy) == "success");
        check(native.terminal == legacyTerminal(legacy));
        check((legacy.started == native.started));
        check((legacy.settled == native.settled));
        check(native.completed == 1u);
        check(native.callbacks == 1u && native.finalized == 1u);
        const auto& legacy_outputs = legacy.result.value.at("tasks")
                                         .at("echo").at("outputs");
        const auto legacy_stdout = legacy_outputs.at("stdout").as<std::string>();
        const auto legacy_exit = legacy_outputs.at("exit_code").as<std::string>();
        if (native.outputs.at("shell_output") != legacy_stdout)
            info("Shell stdout differential C11 bytes=%zu Legacy bytes=%zu",
                 native.outputs.at("shell_output").size(), legacy_stdout.size());
        check((native.outputs.at("shell_output") == legacy_stdout));
        check((native.outputs.at("shell_exit_code") == legacy_exit));
        check(native.outputs.at("shell_exit_code") == "0");
        // The C11 output keys are explicit private Session ports, not a
        // fabricated proof that the production WorkflowContext is replaced.
        check((native.typed_outputs.at("shell_output") ==
               legacy_outputs.at("stdout").to_string()));
    }

    it("compares real failed Shell exit code and suppresses successors") {
        const std::string yaml(kFailedShellYaml);
        const auto legacy = runLegacy(yaml, StartCondition::Normal);
        NativeOwner owner;
        owner.init(yaml);
        const auto native = owner.run({"fail_shell", "forbidden"},
                                     {{}, {}}, StartCondition::Normal,
                                     NativePlanMode::OneShellRoot);
        check(!legacy.result.success);
        check(legacyTerminal(legacy) == "failed");
        check(native.terminal == legacyTerminal(legacy));
        check((legacy.started == native.started));
        check((legacy.settled == native.settled));
        check((native.started == std::vector<std::string>{"fail_shell"}));
        check(native.completed == 1u);
        check(native.callbacks == 1u && native.finalized == 1u);
        check(native.private_failure_ports.at("fail_shell") == "7");
        check(!native.failure_diagnostics.at("fail_shell").empty());
        check(native.outputs.empty() && native.typed_outputs.empty());
        int forbidden = 0;
        check(praktor_c11_value_store_contains(
                  &owner.root, "forbidden_output", &forbidden) ==
                  PRAKTOR_C11_VALUE_OK && forbidden == 0);
        praktor_workflow_task_state blocked = PRAKTOR_WORKFLOW_TASK_SUCCESS;
        check(praktor_workflow_task_registry_state(
                  owner.registry, 1u, &blocked) == SALTS_OK &&
                  blocked == PRAKTOR_WORKFLOW_TASK_PENDING);
        // The Legacy output is only read here; the C11 Owner has not yet
        // qualified failure-output publication parity. Do not invent it.
        const auto& legacy_outputs = legacy.result.value.at("tasks")
                                         .at("fail_shell").at("outputs");
        check(legacy_outputs.at("exit_code").as<std::string>() ==
              native.private_failure_ports.at("fail_shell"));
        check((legacy_outputs.at("exit_code").to_string() ==
               native.typed_failure_outputs.at("fail_shell")));
        int leaked_root_exit = 0;
        check(praktor_c11_value_store_contains(
                  &owner.root, "shell_exit_code", &leaked_root_exit) ==
                  PRAKTOR_C11_VALUE_OK && leaked_root_exit == 0);
    }

    it("compares failure admission and blocks the dependent task") {
        const std::string yaml(kFailedYaml);
        const auto legacy = runLegacy(yaml, StartCondition::Normal);
        NativeOwner owner;
        owner.init(yaml);
        const auto native = owner.run({"reject", "downstream"}, {{}, {}}, StartCondition::Normal);
        check(!legacy.result.success);
        check(legacyTerminal(legacy) == "failed");
        check(native.terminal == legacyTerminal(legacy));
        check((legacy.started == native.started));
        check((legacy.settled == native.settled));
        check((native.started == std::vector<std::string>{"reject"}));
        check(native.states.at("reject") ==
              legacy.result.value.at("tasks").at("reject")
                  .at("status").as<std::string>());
        check(native.outputs.empty());
        check(native.typed_outputs.empty());
        int leaked = 0;
        check(praktor_c11_value_store_contains(
                  &owner.root, "should_not_publish", &leaked) ==
                  PRAKTOR_C11_VALUE_OK && leaked == 0);
        praktor_workflow_task_state blocked = PRAKTOR_WORKFLOW_TASK_SUCCESS;
        check(praktor_workflow_task_registry_state(owner.registry, 1u, &blocked) ==
                  SALTS_OK && blocked == PRAKTOR_WORKFLOW_TASK_PENDING);
        check(native.completed == 1u);
        check(!legacy.result.error_message.empty());
    }

    it("cancels a genuinely running native Shell before DAG publication") {
        // The Legacy WorkflowRunner's blocking ShellExecutor::execute() does
        // not have an independently qualified in-flight control handoff.
        // This is explicitly a C11 application Owner lifecycle gate, NOT
        // a claim of Legacy application-level mid-flight cancellation parity.
        ActiveShellFixture source;
        NativeOwner owner;
        owner.init(source.yaml());
        const auto native = owner.run({"long_shell", "never_started"},
                                      {{}, {}},
                                      StartCondition::CancelActiveShell,
                                      NativePlanMode::OneShellRoot,
                                      source.started);
        check(native.terminal == "cancelled");
        check(native.child_marker_observed);
        check(native.native_cancel_status == SALTS_OK ||
              native.native_cancel_status == SALTS_EBUSY);
        check((native.started == std::vector<std::string>{"long_shell"}));
        check((native.settled == std::vector<std::string>{"long_shell:failed"}));
        check(native.completed == 1u);
        check(native.callbacks == 1u && native.finalized == 1u);
        check(native.outputs.empty() && native.typed_outputs.empty() &&
              native.typed_failure_outputs.empty());
        int published = 1;
        check(praktor_c11_value_store_contains(
                  &owner.root, "forbidden", &published) ==
                  PRAKTOR_C11_VALUE_OK && published == 0);
        check(praktor_c11_value_store_contains(
                  &owner.root, "baseline", &published) ==
                  PRAKTOR_C11_VALUE_OK && published == 1);
        praktor_workflow_task_state dependent =
            PRAKTOR_WORKFLOW_TASK_SUCCESS;
        check(praktor_workflow_task_registry_state(owner.registry, 1u,
                 &dependent) == SALTS_OK &&
              dependent == PRAKTOR_WORKFLOW_TASK_PENDING);
    }

    it("compares pre-start cancellation without running any leaf") {
        const std::string yaml(kSuccessfulYaml);
        const auto legacy = runLegacy(yaml, StartCondition::PreCancelled);
        NativeOwner owner;
        owner.init(yaml);
        const auto native = owner.run({"prepare", "verify"},
                                      {{"observed"}, {"finished"}}, StartCondition::PreCancelled);
        check(!legacy.result.success);
        check(legacyTerminal(legacy) == "cancelled");
        check(native.terminal == legacyTerminal(legacy));
        check(legacy.started.empty());
        check(native.started.empty());
        check(native.outputs.empty());
        check(native.completed == 0u);
        check((legacy.result.value.at("tasks").size() == 0u));
        praktor_workflow_task_state untouched = PRAKTOR_WORKFLOW_TASK_SUCCESS;
        check(praktor_workflow_task_registry_state(owner.registry, 0u, &untouched) ==
                  SALTS_OK && untouched == PRAKTOR_WORKFLOW_TASK_PENDING);
    }

    it("compares pre-expired deadline without task admission or publication") {
        const std::string yaml(kSuccessfulYaml);
        const auto legacy = runLegacy(yaml, StartCondition::ExpiredDeadline);
        NativeOwner owner;
        owner.init(yaml);
        const auto native = owner.run({"prepare", "verify"},
                                      {{"observed"}, {"finished"}},
                                      StartCondition::ExpiredDeadline);
        check(!legacy.result.success);
        check(legacyTerminal(legacy) == "timed_out");
        check(legacy.result.error_message == "Workflow execution timed out");
        check(native.terminal == legacyTerminal(legacy));
        check(legacy.started.empty());
        check((legacy.settled == native.settled));
        check(native.started.empty());
        check(native.completed == 0u);
        check(native.outputs.empty() && native.typed_outputs.empty());
        check(native.callbacks == 0u && native.finalized == 0u);
        check((legacy.result.value.at("tasks").size() == 0u));
        praktor_workflow_task_state untouched = PRAKTOR_WORKFLOW_TASK_SUCCESS;
        check(praktor_workflow_task_registry_state(owner.registry, 0u, &untouched) ==
                  SALTS_OK && untouched == PRAKTOR_WORKFLOW_TASK_PENDING);
    }
}
