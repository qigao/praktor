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
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
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

void require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

struct LegacyTrace {
    WorkflowExecutionResult result;
    std::vector<std::string> started;
};

LegacyTrace runLegacy(const std::string& source, bool precancel) {
    LegacyTrace trace;
    std::mutex mutex;
    auto observer = std::make_shared<Praktor::Execution::ExecutionObserver>(
        [&](const Praktor::Execution::ExecutionEvent& event) {
            if (event.type == Praktor::Execution::ExecutionEventType::TaskStarted) {
                std::lock_guard<std::mutex> lock(mutex);
                trace.started.push_back(event.task_name);
            }
        });
    auto control = std::make_shared<Praktor::Execution::ExecutionControl>();
    if (precancel) control->requestCancel();
    auto workflow = TaskParser::parseText(source, "c11-owner-differential.yml");
    trace.result = WorkflowRunner(std::move(workflow), WorkflowInputs{})
                       .executeObserved(control, observer, false, 1);
    return trace;
}

struct NativeTrace {
    std::vector<std::string> started;
    std::map<std::string, std::string> outputs;
    std::map<std::string, std::string> states;
    std::string terminal;
    size_t completed = 0;
    size_t callbacks = 0;
    size_t finalized = 0;
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
};

void runNative(void *user) {
    auto& callback = *static_cast<OwnerCallback *>(user);
    ++callback.ran;
    callback.status = praktor_c11_session_tick(
        &callback.task->session, &callback.outcome);
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

    OwnerExecutor() {
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

    explicit NativeOwner(const std::string& source) {
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
        require(praktor_c11_value_store_init(&root, 8u, 64u, 128u) ==
                    PRAKTOR_C11_VALUE_OK, "C11 root init failed");
        root_ready = true;
        require(praktor_c11_workflow_seed_root_text_variables(snapshot, &root) ==
                    PRAKTOR_C11_VALUE_OK, "C11 input variable admission failed");
        require(cflow_process_retainer_init(&retainer, 2u) == SALTS_OK,
                "C11 Process recovery retainer init failed");
        retainer_ready = true;
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
                    const std::vector<std::string>& output_keys,
                    bool precancel) {
        require(names.size() == praktor_workflow_snapshot_task_count(snapshot),
                "C11 task count differs from shared fixture");
        require(names.size() == output_keys.size(), "Invalid differential fixture");
        NativeTrace trace;
        if (precancel)
            require(praktor_workflow_dag_stop(
                        dag, PRAKTOR_WORKFLOW_DAG_CANCELLED) == SALTS_OK,
                    "C11 pre-start cancel failed");
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
            praktor_c11_native_host_config_init(&hc, 8u, 64u, 128u);
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
            char error[256]{};
            require(praktor_c11_orch_session_plan_create(
                        praktor_workflow_snapshot_task_tree(snapshot, index),
                        &task.config.tree, &task.plan, error, sizeof(error)) ==
                        SALTS_OK, "C11 native leaf plan rejected parity input");
            require(praktor_c11_orch_session_plan_init_session(
                        task.plan, &task.config, &task.session) == SALTS_OK,
                    "C11 Session init failed");
            task.session_ready = true;
            praktor_c11_orch_session_plan_destroy(task.plan);
            task.plan = nullptr;

            OwnerExecutor executor;
            OwnerCallback callback{};
            callback.task = &task;
            executor.run(callback);
            ++trace.callbacks;
            trace.finalized += callback.finalized;
            // A completed CFlow callback does not imply a terminal Session.
            require(callback.outcome != SALTS_BT_RUNNING,
                    "C11 Session still running after one CFlow callback");
            if (callback.outcome != SALTS_BT_SUCCESS) {
                require(callback.outcome == SALTS_BT_FAILURE,
                        "C11 Session returned an unknown state");
                require(praktor_workflow_task_registry_fail(
                            registry, index, vstr_from_cstr("Action execution failed")) ==
                            SALTS_OK, "C11 failed TaskRegistry settlement failed");
                require(praktor_workflow_dag_finish(dag, index, false) == SALTS_OK,
                        "C11 failed DAG settlement failed");
                trace.states[names[index]] = "failed";
                continue;
            }

            const std::string& key = output_keys[index];
            require(!key.empty(), "Successful native case lacks selected output");
            char buffer[256]{};
            size_t length = 0u;
            require(praktor_c11_value_store_get(
                        task_values, key.c_str(), buffer, sizeof(buffer),
                        &length) == PRAKTOR_C11_VALUE_OK,
                    "C11 successful leaf did not produce selected output");
            const std::string value(buffer, length);
            // This first corpus admits only the unescaped ASCII word subset.
            // Do not reinterpret arbitrary text/typed values as matching.
            require(!value.empty() && std::all_of(value.begin(), value.end(),
                [](unsigned char c) {
                    return (c >= 'a' && c <= 'z') ||
                           (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '_';
                }), "Native output needs typed/escaped parity admission");
            praktor_c11_value_txn transaction{};
            const char *selected[] = {key.c_str()};
            require(praktor_c11_workflow_prepare_task_text_outputs(
                        &root, task_values, selected, 1u, &transaction) ==
                        PRAKTOR_C11_VALUE_OK, "C11 output txn preparation failed");
            const std::string json = "{\"" + key + "\":\"" + value + "\"}";
            json_value_t *typed = json_parse(json.c_str(), json.size());
            require(typed != nullptr, "C11 typed output construction failed");
            praktor_workflow_task_output_update update{};
            update.task_index = index;
            update.updates = typed;
            size_t pending = SIZE_MAX;
            const int commit =
                praktor_c11_workflow_parallel_commit_after_cflow_and_process_drain(
                    registry, &update, &transaction, 1u, &executor.control,
                    &retainer, 8u, &pending);
            json_free(typed);
            require(commit == PRAKTOR_C11_PARALLEL_OK && pending == 0u,
                    "C11 publication without finalized CFlow/Process barriers");
            require(praktor_workflow_task_registry_complete(
                        registry, index) == SALTS_OK, "C11 registry complete failed");
            require(praktor_workflow_dag_finish(dag, index, true) == SALTS_OK,
                    "C11 DAG finish failed");
            const json_value_t *published = nullptr;
            require(praktor_workflow_task_registry_get_output(
                        registry, index, vstr_from_cstr(key.c_str()),
                        &published) == SALTS_OK && published != nullptr,
                    "C11 typed output not published");
            trace.states[names[index]] = "success";
            trace.outputs[key] = value;
        }
        size_t reaped = 0u, pending = SIZE_MAX;
        require(cflow_process_retainer_run_ready(
                    &retainer, 8u, &reaped, &pending) == SALTS_OK &&
                    pending == 0u && reaped == 0u,
                "C11 source-only run retained a native Process lease");
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
        const auto legacy = runLegacy(yaml, false);
        NativeOwner owner(yaml);
        const auto native = owner.run({"prepare", "verify"},
                                      {"observed", "finished"}, false);
        check(legacy.result.success);
        check(legacyTerminal(legacy) == "success");
        check(native.terminal == legacyTerminal(legacy));
        check((legacy.started == native.started));
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

    it("compares failure admission and blocks the dependent task") {
        const std::string yaml(kFailedYaml);
        const auto legacy = runLegacy(yaml, false);
        NativeOwner owner(yaml);
        const auto native = owner.run({"reject", "downstream"}, {"", ""}, false);
        check(!legacy.result.success);
        check(legacyTerminal(legacy) == "failed");
        check(native.terminal == legacyTerminal(legacy));
        check((legacy.started == native.started));
        check((native.started == std::vector<std::string>{"reject"}));
        check(native.states.at("reject") ==
              legacy.result.value.at("tasks").at("reject")
                  .at("status").as<std::string>());
        check(native.outputs.empty());
        check(native.completed == 1u);
        check(!legacy.result.error_message.empty());
    }

    it("compares pre-start cancellation without running any leaf") {
        const std::string yaml(kSuccessfulYaml);
        const auto legacy = runLegacy(yaml, true);
        NativeOwner owner(yaml);
        const auto native = owner.run({"prepare", "verify"},
                                      {"observed", "finished"}, true);
        check(!legacy.result.success);
        check(legacyTerminal(legacy) == "cancelled");
        check(native.terminal == legacyTerminal(legacy));
        check(legacy.started.empty());
        check(native.started.empty());
        check(native.outputs.empty());
        check(native.completed == 0u);
        check((legacy.result.value.at("tasks").size() == 0u));
    }
}
