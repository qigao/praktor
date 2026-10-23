#include "tree_executor_session.hpp"
#include "praktor_test.hpp"
#include "native_host.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace {
struct Host {
  praktor_c11_native_host state{};
  praktor_c11_bt_host hooks{};
  static uint64_t readClock(void* user) { return *static_cast<uint64_t*>(user); }
  explicit Host(size_t max_running = 0u, uint64_t* clock = nullptr) {
    const praktor_c11_native_host_config config{
        8u, 64u, 4096u, max_running, clock != nullptr ? readClock : nullptr, clock};
    if (praktor_c11_native_host_init_with_config(&state, &config) != SALTS_OK ||
        praktor_c11_native_host_bind(&state, &hooks) != SALTS_OK) throw std::runtime_error("host init");
  }
  ~Host() { praktor_c11_native_host_destroy(&state); }
  int set(const char* key, const char* value) { return praktor_c11_value_store_set(praktor_c11_native_host_values(&state), key, value); }
  std::string get(const char* key) {
    char value[4097]; size_t size = 0u;
    if (praktor_c11_value_store_get(praktor_c11_native_host_values(&state), key, value, sizeof(value), &size) != PRAKTOR_C11_VALUE_OK) throw std::runtime_error("missing key");
    return value;
  }
};
salts_bt_tree_config limits() {
  salts_bt_tree_config config{}; salts_bt_tree_config_init(&config);
  config.max_nodes = 16u; config.max_depth = 8u; config.max_control_iterations = 32u;
  return config;
}
actions::Node setVariable(const char* key, actions::Value value) {
  actions::Node node; node.id = "SetVariable";
  node.params["key"] = std::string(key); node.params["value"] = std::move(value); return node;
}
void nativeValuesAndCapacity() {
  Host host;
  check(host.set("source", "42") == PRAKTOR_C11_VALUE_OK);
  actions::Tree tree; tree.root.id = "Sequence";
  tree.root.children.push_back(setVariable("copy", std::string("{source}")));
  tree.root.children.push_back(setVariable("flag", true));
  tree.root.children.push_back(setVariable("number", 7.0));
  actions::Node from; from.id = "SetVariable";
  from.params["key"] = std::string("shell_exit_code"); from.params["from"] = std::string("copy");
  tree.root.children.push_back(from);
  actions::Node check; check.id = "CheckExitCode"; check.params["expected"] = 42.0;
  tree.root.children.push_back(check);
  const auto plan = actions::c11::compileTreePlan(tree, limits());
  check(plan.source_nodes[0] == &tree.root && plan.source_nodes[1] == &tree.root.children[0]);
  check(plan.nodes[1].parent_index == 0u && plan.source_nodes.size() == 6u);
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(host.get("copy") == "42" && host.get("flag") == "true" && host.get("number") == "7.000000");
  check(session.reset() == SALTS_OK);
  check(host.set("source", "43") == PRAKTOR_C11_VALUE_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_FAILURE);
  check(host.get("copy") == "43");
  check(host.set("extra1", "one") == PRAKTOR_C11_VALUE_OK);
  check(host.set("extra2", "two") == PRAKTOR_C11_VALUE_OK);
  check(host.set("extra3", "three") == PRAKTOR_C11_VALUE_OK);
  actions::Tree full; full.root = setVariable("rejected", std::string("must-not-publish"));
  actions::c11::TreeExecutorSession capacity(full, {}, limits(), host.hooks);
  check(capacity.tick(&status) == SALTS_ENOBUFS);
  check(praktor_c11_value_store_size(praktor_c11_native_host_values(&host.state)) == 8u);
  check(host.get("source") == "43");
  actions::Tree unknown; unknown.root.id = "Unregistered";
  actions::c11::TreeExecutorSession declined(unknown, {}, limits(), host.hooks);
  check(declined.tick(&status) == SALTS_ENOTSUP);
}
void parallelScopeFailsClosed() {
  Host host;
  host.hooks.parallel_enter = [](void*, size_t, size_t, bool) -> int { return SALTS_OK; };
  host.hooks.parallel_step = [](void*, size_t, size_t, salts_bt_node_status) -> int { return SALTS_OK; };
  host.hooks.parallel_join = [](void*, size_t, salts_bt_node_status) -> int { return SALTS_OK; };
  host.hooks.parallel_finish = [](void*, size_t, size_t, salts_bt_node_status) -> int { return SALTS_OK; };
  host.hooks.parallel_abort = [](void*, size_t, size_t) {};
  actions::Tree tree; tree.root.id = "Parallel";
  tree.root.children.push_back(setVariable("root-must-stay-empty", std::string("branch-value")));
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  check(session.tick(&status) == SALTS_ENOTSUP);
  check(praktor_c11_value_store_size(praktor_c11_native_host_values(&host.state)) == 0u);
}
void nativeParseLinesAfterCancellation() {
  uint64_t now = 0u;
  Host host(1u, &now);
  check(host.set("shell_output", "\nfirst\n\nsecond\n") == PRAKTOR_C11_VALUE_OK);
  actions::Tree tree; tree.root.id = "Sequence";
  actions::Node sleep; sleep.id = "Sleep"; sleep.params["duration"] = 1.0;
  actions::Node lines; lines.id = "ParseLines";
  tree.root.children = {sleep, lines, setVariable("after", std::string("done"))};
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(session.cancel() == SALTS_OK);
  int contains = 1;
  auto* values = praktor_c11_native_host_values(&host.state);
  for (const char* key : {"parsed_lines", "parsed_lines_count", "after"}) {
    check(praktor_c11_value_store_contains(values, key, &contains) == PRAKTOR_C11_VALUE_OK && !contains);
  }
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  now = 1000000u;
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(host.get("parsed_lines") == "first\n\nsecond");
  check(host.get("parsed_lines_count") == "4" && host.get("after") == "done");
  // A claimed but unsupported regex may not run a compatibility fallback or
  // publish a new count. The independent binary contains no such fallback.
  tree.root.children[1].params["filter"] = std::string("^first$");
  tree.root.children.erase(tree.root.children.begin());
  check(praktor_c11_value_store_erase(values, "after") == PRAKTOR_C11_VALUE_OK);
  actions::c11::TreeExecutorSession rejected(tree, {}, limits(), host.hooks);
  check(rejected.tick(&status) == SALTS_ENOTSUP);
  check(praktor_c11_value_store_contains(values, "after", &contains) == PRAKTOR_C11_VALUE_OK && !contains);
  check(host.get("parsed_lines_count") == "4");
}

void nativeParseLinesCapacityFailure() {
  Host host;
  check(host.set("shell_output", "new\nlines") == PRAKTOR_C11_VALUE_OK);
  check(host.set("parsed_lines", "old") == PRAKTOR_C11_VALUE_OK);
  for (const char* key : {"a", "b", "c", "d", "e", "f"})
    check(host.set(key, "occupied") == PRAKTOR_C11_VALUE_OK);
  actions::Tree tree; tree.root.id = "Sequence";
  actions::Node lines; lines.id = "ParseLines";
  tree.root.children = {lines, setVariable("after", std::string("done"))};
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  check(session.tick(&status) == SALTS_ENOBUFS);
  check(host.get("parsed_lines") == "old");
  auto* values = praktor_c11_native_host_values(&host.state);
  int contains = 1;
  for (const char* key : {"parsed_lines_count", "after"})
    check(praktor_c11_value_store_contains(values, key, &contains) == PRAKTOR_C11_VALUE_OK && !contains);
  check(praktor_c11_value_store_erase(values, "e") == PRAKTOR_C11_VALUE_OK);
  check(praktor_c11_value_store_erase(values, "f") == PRAKTOR_C11_VALUE_OK);
  check(session.reset() == SALTS_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(host.get("parsed_lines") == "new\nlines" && host.get("parsed_lines_count") == "2");
  check(host.get("after") == "done");
}
struct RunningHost {
  int ticks = 0, halts = 0;
  bool decline = false;
  static int tick(void* user, const praktor_c11_leaf_request* request, salts_bt_node_status* status) {
    auto& self = *static_cast<RunningHost*>(user);
    if (std::strcmp(request->action, "Pending") != 0 || request->parameter_count != 1u ||
        request->parameters[0].kind != PRAKTOR_C11_LEAF_BOOLEAN || !request->parameters[0].boolean) return SALTS_EINVAL;
    ++self.ticks;
    if (self.decline) return SALTS_ENOTSUP;
    *status = SALTS_BT_RUNNING; return SALTS_OK;
  }
  static void halt(void* user, size_t) { ++static_cast<RunningHost*>(user)->halts; }
};
void typedCancellationAndTimeout() {
  Host host; RunningHost running;
  host.hooks.user = &running; host.hooks.tick_typed_leaf = &RunningHost::tick;
  host.hooks.halt_typed_leaf = nullptr;
  host.hooks.halt_leaf = &RunningHost::halt;
  actions::Tree tree; tree.root.id = "Timeout";
  tree.root.params["timeout_ms"] = std::string("{deadline}");
  actions::Node pending; pending.id = "Pending"; pending.params["enabled"] = true;
  tree.root.children.push_back(pending);
  check(host.set("deadline", "10000") == PRAKTOR_C11_VALUE_OK);
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(host.set("deadline", "0") == PRAKTOR_C11_VALUE_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_FAILURE);
  check(running.ticks == 1 && running.halts == 1);
  check(session.reset() == SALTS_OK);
  check(host.set("deadline", "10000") == PRAKTOR_C11_VALUE_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(session.cancel() == SALTS_OK && running.halts == 2);
  check(session.cancel() == SALTS_OK && running.halts == 2);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  running.decline = true;
  check(session.tick(&status) == SALTS_ENOTSUP && running.halts == 3);
}
void nativeSleepLifecycleAndOwnership() {
  uint64_t now = 0u;
  Host host(1u, &now);
  check(host.set("duration", "100ms") == PRAKTOR_C11_VALUE_OK);
  actions::Tree tree; tree.root.id = "Sleep";
  tree.root.params["duration"] = std::string("{duration}");
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(host.set("duration", "0ms") == PRAKTOR_C11_VALUE_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(session.cancel() == SALTS_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(host.set("duration", "100ms") == PRAKTOR_C11_VALUE_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(session.reset() == SALTS_OK);
  check(host.set("duration", "0ms") == PRAKTOR_C11_VALUE_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);

  actions::c11::TreeExecutorSession second(tree, {}, limits(), host.hooks);
  {
    actions::c11::TreeExecutorSession first(tree, {}, limits(), host.hooks);
    check(host.set("duration", "100ms") == PRAKTOR_C11_VALUE_OK);
    check(first.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    check(second.tick(&status) == SALTS_ENOBUFS);
    check(host.set("duration", "0ms") == PRAKTOR_C11_VALUE_OK);
    check(first.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    // The rejected second Session's halt cannot erase the first's activation.
  }
  check(second.reset() == SALTS_OK);
  check(second.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  check(host.set("duration", "1s") == PRAKTOR_C11_VALUE_OK);
  check(second.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(second.cancel() == SALTS_OK);
  now = 100u;
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  now = 99u;
  check(session.tick(&status) == SALTS_EINVAL);
  check(second.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(second.cancel() == SALTS_OK);
}

void nativeSleepUsesRealMonotonicClock() {
  Host host(1u);
  actions::Tree tree; tree.root.id = "Sleep"; tree.root.params["duration"] = 2.0;
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(session.tick(&status) == SALTS_OK);
  } while (status == SALTS_BT_RUNNING && std::chrono::steady_clock::now() < deadline);
  check(status == SALTS_BT_SUCCESS);
}

void timeoutReleasesNativeSleepSlot() {
  uint64_t now = 0u; Host host(1u, &now);
  check(host.set("duration", "1h") == PRAKTOR_C11_VALUE_OK);
  check(host.set("deadline", "10000") == PRAKTOR_C11_VALUE_OK);
  actions::Tree tree; tree.root.id = "Timeout";
  tree.root.params["timeout_ms"] = std::string("{deadline}");
  actions::Node sleep; sleep.id = "Sleep"; sleep.params["duration"] = std::string("{duration}");
  tree.root.children.push_back(sleep);
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(host.set("deadline", "0") == PRAKTOR_C11_VALUE_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_FAILURE);
  check(host.set("deadline", "10000") == PRAKTOR_C11_VALUE_OK);
  check(host.set("duration", "0ms") == PRAKTOR_C11_VALUE_OK);
  check(session.reset() == SALTS_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  // Positive admission proves Timeout actually returned the sole slot.
  check(host.set("duration", "1s") == PRAKTOR_C11_VALUE_OK);
  check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  check(session.cancel() == SALTS_OK);
}

void rejectAmbiguousHaltCallbacks() {
  Host host;
  host.hooks.halt_leaf = [](void*, size_t) {};
  actions::Tree tree; tree.root = setVariable("not-written", std::string("no"));
  bool rejected = false;
  try { actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks); }
  catch (const std::invalid_argument&) { rejected = true; }
  check(rejected);
  check(praktor_c11_value_store_size(praktor_c11_native_host_values(&host.state)) == 0u);
}

void realProcessAndRetainer() {
  cflow_process_retainer retainer{};
  check(cflow_process_retainer_init(&retainer, 2u) == SALTS_OK);
  {
    Host host;
    actions::Tree tree; tree.root.id = "Shell";
    tree.root.params["cmd"] = std::string("echo independent-session");
    tree.root.params["stream_output"] = false;
    actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks, &retainer);
    salts_bt_node_status status = SALTS_BT_RUNNING;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      check(session.tick(&status) == SALTS_OK);
      if (status == SALTS_BT_RUNNING) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (status == SALTS_BT_RUNNING && std::chrono::steady_clock::now() < deadline);
    if (status != SALTS_BT_SUCCESS) {
      std::fprintf(stderr, "independent Shell status=%d error=%s exit=%s stderr=%s\n",
                   static_cast<int>(status), session.lastError().c_str(),
                   host.get("shell_exit_code").c_str(), host.get("shell_stderr").c_str());
    }
    check(status == SALTS_BT_SUCCESS);
    check(host.get("shell_output").find("independent-session") != std::string::npos);
    check(host.get("shell_exit_code") == "0");
    actions::Tree live; live.root.id = "Shell";
#if defined(_WIN32)
    live.root.params["cmd"] = std::string("ping -n 6 127.0.0.1 > NUL");
#else
    live.root.params["cmd"] = std::string("sleep 5");
#endif
    live.root.params["stream_output"] = false;
    actions::c11::TreeExecutorSession canceled(live, {}, limits(), host.hooks, &retainer, 0u);
    check(canceled.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    const int canceled_status = canceled.cancel();
    check(canceled_status == SALTS_OK || canceled_status == SALTS_EBUSY);
  } // Host value storage is gone before late Process completions are drained.
  size_t pending = 1u;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (pending != 0u && std::chrono::steady_clock::now() < deadline) {
    size_t reaped = 0u;
    const int result = cflow_process_retainer_run_ready(&retainer, 64u, &reaped, &pending);
    check(result == SALTS_OK || result == SALTS_EBUSY);
    if (pending != 0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(pending == 0u);
  check(cflow_process_retainer_destroy(&retainer) == SALTS_OK);
}
}
spec("Independent C11 Session") {
  it("parses lines after fresh admission and rejects unsupported filters") { nativeParseLinesAfterCancellation(); }
  it("stops a Sequence before partial ParseLines publication") { nativeParseLinesCapacityFailure(); }
  it("executes typed leaves with bounded values") { nativeValuesAndCapacity(); }
  it("rejects root-store dispatch inside Parallel") { parallelScopeFailsClosed(); }
  it("halts typed activations on cancel and dynamic Timeout") { typedCancellationAndTimeout(); }
  it("scopes native Sleep lifecycle to its Session") { nativeSleepLifecycleAndOwnership(); }
  it("uses a real monotonic clock for native Sleep") { nativeSleepUsesRealMonotonicClock(); }
  it("returns native activation capacity after Timeout") { timeoutReleasesNativeSleepSlot(); }
  it("rejects ambiguous halt ownership") { rejectAmbiguousHaltCallbacks(); }
  it("executes Salts Process and retains late completions after Host destruction") { realProcessAndRetainer(); }
}
