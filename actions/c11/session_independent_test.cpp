#include "tree_executor_session.hpp"
#include "native_host.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)
namespace {
struct Host {
  praktor_c11_native_host state{};
  praktor_c11_bt_host hooks{};
  Host() {
    if (praktor_c11_native_host_init(&state, 8u, 64u, 4096u) != SALTS_OK ||
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
int nativeValuesAndCapacity() {
  Host host;
  CHECK(host.set("source", "42") == PRAKTOR_C11_VALUE_OK);
  actions::Tree tree; tree.root.id = "Sequence";
  tree.root.children.push_back(setVariable("copy", std::string("{source}")));
  tree.root.children.push_back(setVariable("flag", true));
  tree.root.children.push_back(setVariable("number", 7.0));
  actions::Node from; from.id = "SetVariable";
  from.params["key"] = std::string("shell_exit_code"); from.params["from"] = std::string("copy");
  tree.root.children.push_back(from);
  actions::Node check; check.id = "CheckExitCode"; check.params["expected"] = 42.0;
  tree.root.children.push_back(check);
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  CHECK(host.get("copy") == "42" && host.get("flag") == "true" && host.get("number") == "7.000000");
  CHECK(session.reset() == SALTS_OK);
  CHECK(host.set("source", "43") == PRAKTOR_C11_VALUE_OK);
  CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_FAILURE);
  CHECK(host.get("copy") == "43");
  CHECK(host.set("extra1", "one") == PRAKTOR_C11_VALUE_OK);
  CHECK(host.set("extra2", "two") == PRAKTOR_C11_VALUE_OK);
  CHECK(host.set("extra3", "three") == PRAKTOR_C11_VALUE_OK);
  actions::Tree full; full.root = setVariable("rejected", std::string("must-not-publish"));
  actions::c11::TreeExecutorSession capacity(full, {}, limits(), host.hooks);
  CHECK(capacity.tick(&status) == SALTS_ENOBUFS);
  CHECK(praktor_c11_value_store_size(praktor_c11_native_host_values(&host.state)) == 8u);
  CHECK(host.get("source") == "43");
  actions::Tree unknown; unknown.root.id = "Unregistered";
  actions::c11::TreeExecutorSession declined(unknown, {}, limits(), host.hooks);
  CHECK(declined.tick(&status) == SALTS_ENOTSUP);
  return 0;
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
int typedCancellationAndTimeout() {
  Host host; RunningHost running;
  host.hooks.user = &running; host.hooks.tick_typed_leaf = &RunningHost::tick;
  host.hooks.halt_leaf = &RunningHost::halt;
  actions::Tree tree; tree.root.id = "Timeout";
  tree.root.params["timeout_ms"] = std::string("{deadline}");
  actions::Node pending; pending.id = "Pending"; pending.params["enabled"] = true;
  tree.root.children.push_back(pending);
  CHECK(host.set("deadline", "10000") == PRAKTOR_C11_VALUE_OK);
  actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks);
  salts_bt_node_status status = SALTS_BT_FAILURE;
  CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  CHECK(host.set("deadline", "0") == PRAKTOR_C11_VALUE_OK);
  CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_FAILURE);
  CHECK(running.ticks == 1 && running.halts == 1);
  CHECK(session.reset() == SALTS_OK);
  CHECK(host.set("deadline", "10000") == PRAKTOR_C11_VALUE_OK);
  CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  CHECK(session.cancel() == SALTS_OK && running.halts == 2);
  CHECK(session.cancel() == SALTS_OK && running.halts == 2);
  CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
  running.decline = true;
  CHECK(session.tick(&status) == SALTS_ENOTSUP && running.halts == 3);
  return 0;
}
int realProcessAndRetainer() {
  cflow_process_retainer retainer{};
  CHECK(cflow_process_retainer_init(&retainer, 2u) == SALTS_OK);
  {
    Host host;
    actions::Tree tree; tree.root.id = "Shell";
    tree.root.params["cmd"] = std::string("echo independent-session");
    tree.root.params["stream_output"] = false;
    actions::c11::TreeExecutorSession session(tree, {}, limits(), host.hooks, &retainer);
    salts_bt_node_status status = SALTS_BT_RUNNING;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      CHECK(session.tick(&status) == SALTS_OK);
      if (status == SALTS_BT_RUNNING) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (status == SALTS_BT_RUNNING && std::chrono::steady_clock::now() < deadline);
    if (status != SALTS_BT_SUCCESS) {
      std::fprintf(stderr, "independent Shell status=%d error=%s exit=%s stderr=%s\n",
                   static_cast<int>(status), session.lastError().c_str(),
                   host.get("shell_exit_code").c_str(), host.get("shell_stderr").c_str());
    }
    CHECK(status == SALTS_BT_SUCCESS);
    CHECK(host.get("shell_output").find("independent-session") != std::string::npos);
    CHECK(host.get("shell_exit_code") == "0");
    actions::Tree live; live.root.id = "Shell";
#if defined(_WIN32)
    live.root.params["cmd"] = std::string("ping -n 6 127.0.0.1 > NUL");
#else
    live.root.params["cmd"] = std::string("sleep 5");
#endif
    live.root.params["stream_output"] = false;
    actions::c11::TreeExecutorSession canceled(live, {}, limits(), host.hooks, &retainer, 0u);
    CHECK(canceled.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    const int canceled_status = canceled.cancel();
    CHECK(canceled_status == SALTS_OK || canceled_status == SALTS_EBUSY);
  } // Host value storage is gone before late Process completions are drained.
  size_t pending = 1u;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (pending != 0u && std::chrono::steady_clock::now() < deadline) {
    size_t reaped = 0u;
    const int result = cflow_process_retainer_run_ready(&retainer, 64u, &reaped, &pending);
    CHECK(result == SALTS_OK || result == SALTS_EBUSY);
    if (pending != 0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(pending == 0u);
  CHECK(cflow_process_retainer_destroy(&retainer) == SALTS_OK);
  return 0;
}
}
int main() {
  if (nativeValuesAndCapacity() != 0) return 1;
  if (typedCancellationAndTimeout() != 0) return 1;
  return realProcessAndRetainer();
}
