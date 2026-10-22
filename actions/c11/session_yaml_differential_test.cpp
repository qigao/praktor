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
}
int main() { return differential(); }
