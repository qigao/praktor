#include "tree_executor_session.hpp"
#include "native_host.h"
#include "core/executor.hpp"
#include "yml/task_parser.hpp"
#include <cstdio>
#include <string>
#define CHECK(e) do { if (!(e)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #e); return 1; } } while (0)
namespace {
/* These fixtures deliberately use text ports, including numeric text;
 * the shared YAML parser supplies the actual canonical node names/topology. */
actions::Node textNode(const OrchNode& source) {
  actions::Node node; node.id = source.type;
  for (const auto& entry : source.params) node.params[entry.first] = entry.second;
  for (const auto& child : source.children) node.children.push_back(textNode(child));
  return node;
}
int differential() {
  const auto workflow = TaskParser::parseText(R"(
tasks:
  - name: native_contract
    sequence:
      - set_variable: copied
        value: "{source}"
      - set_variable: duplicated
        from: copied
      - check_exit_code:
          expected: "0"
      - set_variable: after_check
        value: admitted
)", "native-session-differential.yml");
  CHECK(workflow.tasks.size() == 1u);
  actions::Tree tree;
  tree.root = textNode(std::get<OrchParams>(workflow.tasks.front().specifics).root);
  salts_bt_tree_config config{}; salts_bt_tree_config_init(&config);
  for (const char* code : {"0", "7", "invalid"}) {
    actions::Executor legacy; actions::Blackboard blackboard; actions::ExecutionContext context;
    blackboard.set("source", "yaml-value"); blackboard.set("shell_exit_code", code);
    actions::NodeStatus expected = actions::NodeStatus::FAILURE;
    bool legacy_error = false;
    try { expected = legacy.execute(tree, blackboard, context); }
    catch (const std::exception&) { legacy_error = true; }
    praktor_c11_native_host host{}; praktor_c11_bt_host hooks{};
    CHECK(praktor_c11_native_host_init(&host, 8u, 64u, 4096u) == SALTS_OK);
    CHECK(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
    auto* values = praktor_c11_native_host_values(&host);
    CHECK(praktor_c11_value_store_set(values, "source", "yaml-value") == PRAKTOR_C11_VALUE_OK);
    CHECK(praktor_c11_value_store_set(values, "shell_exit_code", code) == PRAKTOR_C11_VALUE_OK);
    {
      actions::c11::TreeExecutorSession session(tree, {}, config, hooks);
      salts_bt_node_status actual = SALTS_BT_FAILURE;
      const int result = session.tick(&actual);
      CHECK((result != SALTS_OK) == legacy_error);
      if (!legacy_error) CHECK((actual == SALTS_BT_SUCCESS) == (expected == actions::NodeStatus::SUCCESS));
      for (const char* key : {"copied", "duplicated", "after_check"}) {
        int contains = 0; char text[4097]; size_t size = 0u;
        CHECK(praktor_c11_value_store_contains(values, key, &contains) == PRAKTOR_C11_VALUE_OK);
        CHECK((contains != 0) == blackboard.has(key));
        if (contains) {
          CHECK(praktor_c11_value_store_get(values, key, text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
          CHECK(blackboard.get(key) == text);
        }
      }
    }
    praktor_c11_native_host_destroy(&host);
  }
  return 0;
}
uint64_t yamlClock(void* user) { return *static_cast<uint64_t*>(user); }
int sleepDifferential() {
  const auto workflow = TaskParser::parseText(R"(
tasks:
  - name: native_sleep_contract
    sequence:
      - set_variable: before_sleep
        value: started
      - sleep:
          duration: 1ms
      - set_variable: after_sleep
        value: completed
)", "native-sleep-differential.yml");
  CHECK(workflow.tasks.size() == 1u);
  actions::Tree tree;
  tree.root = textNode(std::get<OrchParams>(workflow.tasks.front().specifics).root);
  actions::Executor legacy; actions::Blackboard blackboard; actions::ExecutionContext context;
  CHECK(legacy.execute(tree, blackboard, context) == actions::NodeStatus::SUCCESS);
  uint64_t now = 0u;
  const praktor_c11_native_host_config host_config{8u, 64u, 4096u, 1u, yamlClock, &now};
  praktor_c11_native_host host{}; praktor_c11_bt_host hooks{};
  CHECK(praktor_c11_native_host_init_with_config(&host, &host_config) == SALTS_OK);
  CHECK(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  auto* values = praktor_c11_native_host_values(&host);
  {
    salts_bt_tree_config config{}; salts_bt_tree_config_init(&config);
    actions::c11::TreeExecutorSession session(tree, {}, config, hooks);
    salts_bt_node_status status = SALTS_BT_FAILURE;
    CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    int contains = 0;
    CHECK(praktor_c11_value_store_contains(values, "after_sleep", &contains) == PRAKTOR_C11_VALUE_OK && !contains);
    now = 999999u;
    CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    now = 1000000u;
    CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
    for (const char* key : {"before_sleep", "after_sleep"}) {
      char text[4097]; size_t size = 0u;
      CHECK(praktor_c11_value_store_get(values, key, text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
      CHECK(blackboard.get(key) == text);
    }
    CHECK(praktor_c11_value_store_erase(values, "after_sleep") == PRAKTOR_C11_VALUE_OK);
    CHECK(session.reset() == SALTS_OK);
    CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    CHECK(session.cancel() == SALTS_OK);
    now += 1000000u;
    CHECK(praktor_c11_value_store_contains(values, "after_sleep", &contains) == PRAKTOR_C11_VALUE_OK && !contains);
    CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    now += 1000000u;
    CHECK(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  }
  praktor_c11_native_host_destroy(&host);
  return 0;
}

}
int main() {
  if (differential() != 0) return 1;
  return sleepDifferential();
}
