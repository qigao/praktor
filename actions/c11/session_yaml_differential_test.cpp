#include "tree_executor_session.hpp"
#include "praktor_test.hpp"
#include "native_host.h"
#include "core/executor.hpp"
#include "yml/task_parser.hpp"
#include <cstdio>
#include <string>
namespace {
/* These fixtures deliberately use text ports, including numeric text;
 * the shared YAML parser supplies the actual canonical node names/topology. */
actions::Node textNode(const OrchNode& source) {
  actions::Node node; node.id = source.type;
  for (const auto& entry : source.params) node.params[entry.first] = entry.second;
  for (const auto& child : source.children) node.children.push_back(textNode(child));
  return node;
}
void differential() {
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
  check(workflow.tasks.size() == 1u);
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
    check(praktor_c11_native_host_init(&host, 8u, 64u, 4096u) == SALTS_OK);
    check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
    auto* values = praktor_c11_native_host_values(&host);
    check(praktor_c11_value_store_set(values, "source", "yaml-value") == PRAKTOR_C11_VALUE_OK);
    check(praktor_c11_value_store_set(values, "shell_exit_code", code) == PRAKTOR_C11_VALUE_OK);
    {
      actions::c11::TreeExecutorSession session(tree, {}, config, hooks);
      salts_bt_node_status actual = SALTS_BT_FAILURE;
      const int result = session.tick(&actual);
      check((result != SALTS_OK) == legacy_error);
      if (!legacy_error) check((actual == SALTS_BT_SUCCESS) == (expected == actions::NodeStatus::SUCCESS));
      for (const char* key : {"copied", "duplicated", "after_check"}) {
        int contains = 0; char text[4097]; size_t size = 0u;
        check(praktor_c11_value_store_contains(values, key, &contains) == PRAKTOR_C11_VALUE_OK);
        check((contains != 0) == blackboard.has(key));
        if (contains) {
          check(praktor_c11_value_store_get(values, key, text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
          check(blackboard.get(key) == text);
        }
      }
    }
    praktor_c11_native_host_destroy(&host);
  }
}
uint64_t yamlClock(void* user) { return *static_cast<uint64_t*>(user); }
void sleepDifferential() {
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
  check(workflow.tasks.size() == 1u);
  actions::Tree tree;
  tree.root = textNode(std::get<OrchParams>(workflow.tasks.front().specifics).root);
  actions::Executor legacy; actions::Blackboard blackboard; actions::ExecutionContext context;
  check(legacy.execute(tree, blackboard, context) == actions::NodeStatus::SUCCESS);
  uint64_t now = 0u;
  const praktor_c11_native_host_config host_config{8u, 64u, 4096u, 1u, yamlClock, &now};
  praktor_c11_native_host host{}; praktor_c11_bt_host hooks{};
  check(praktor_c11_native_host_init_with_config(&host, &host_config) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  auto* values = praktor_c11_native_host_values(&host);
  {
    salts_bt_tree_config config{}; salts_bt_tree_config_init(&config);
    actions::c11::TreeExecutorSession session(tree, {}, config, hooks);
    salts_bt_node_status status = SALTS_BT_FAILURE;
    check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    int contains = 0;
    check(praktor_c11_value_store_contains(values, "after_sleep", &contains) == PRAKTOR_C11_VALUE_OK && !contains);
    now = 999999u;
    check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    now = 1000000u;
    check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
    for (const char* key : {"before_sleep", "after_sleep"}) {
      char text[4097]; size_t size = 0u;
      check(praktor_c11_value_store_get(values, key, text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
      check(blackboard.get(key) == text);
    }
    check(praktor_c11_value_store_erase(values, "after_sleep") == PRAKTOR_C11_VALUE_OK);
    check(session.reset() == SALTS_OK);
    check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    check(session.cancel() == SALTS_OK);
    now += 1000000u;
    check(praktor_c11_value_store_contains(values, "after_sleep", &contains) == PRAKTOR_C11_VALUE_OK && !contains);
    check(session.tick(&status) == SALTS_OK && status == SALTS_BT_RUNNING);
    now += 1000000u;
    check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
  }
  praktor_c11_native_host_destroy(&host);
}

void parseLinesDifferential() {
  const auto workflow = TaskParser::parseText(R"(
tasks:
  - name: native_lines_contract
    sequence:
      - parse_lines:
          input_key: "{input_port}"
          filter: "{empty_filter}"
          output_key: "{output_port}"
      - set_variable: after_lines
        value: completed
)", "native-lines-differential.yml");
  check(workflow.tasks.size() == 1u);
  actions::Tree tree;
  tree.root = textNode(std::get<OrchParams>(workflow.tasks.front().specifics).root);
  actions::Executor legacy;
  /* Exhaust all short strings over data/LF/CR, including consecutive blank
   * lines and every trailing separator position. Reuse the parsed topology
   * but admit fresh values and native/Legacy activation state each time. */
  for (size_t length = 0u, combinations = 1u; length <= 6u; ++length, combinations *= 3u) {
    for (size_t row = 0u; row < combinations; ++row) {
      std::string input(length, 'a');
      size_t digits = row;
      for (char& c : input) { c = "a\n\r"[digits % 3u]; digits /= 3u; }
      actions::Blackboard blackboard; actions::ExecutionContext context;
      blackboard.set("input_port", "source"); blackboard.set("output_port", "lines");
      blackboard.set("empty_filter", ""); blackboard.set("source", input);
      check(legacy.execute(tree, blackboard, context) == actions::NodeStatus::SUCCESS);
      praktor_c11_native_host host{}; praktor_c11_bt_host hooks{};
      check(praktor_c11_native_host_init(&host, 8u, 64u, 128u) == SALTS_OK);
      check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
      auto* values = praktor_c11_native_host_values(&host);
      for (const char* key : {"input_port", "output_port", "empty_filter", "source"})
        check(praktor_c11_value_store_set(values, key, blackboard.get(key).c_str()) == PRAKTOR_C11_VALUE_OK);
      {
        salts_bt_tree_config config{}; salts_bt_tree_config_init(&config);
        actions::c11::TreeExecutorSession session(tree, {}, config, hooks);
        salts_bt_node_status status = SALTS_BT_FAILURE;
        check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
        for (const char* key : {"lines", "lines_count", "after_lines", "source"}) {
          char text[129]; size_t size = 0u;
          check(praktor_c11_value_store_get(values, key, text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
          check(blackboard.get(key) == text);
        }
      }
      praktor_c11_native_host_destroy(&host);
    }
  }
  // Missing input is an ordinary FAILURE on both paths; existing outputs and
  // downstream effects stay untouched. This also covers direct AST defaults.
  tree.root.children.front().params.clear();
  actions::Blackboard blackboard; actions::ExecutionContext context;
  blackboard.set("parsed_lines", "old"); blackboard.set("parsed_lines_count", "old-count");
  check(legacy.execute(tree, blackboard, context) == actions::NodeStatus::FAILURE);
  praktor_c11_native_host host{}; praktor_c11_bt_host hooks{};
  check(praktor_c11_native_host_init(&host, 8u, 64u, 128u) == SALTS_OK);
  check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
  auto* values = praktor_c11_native_host_values(&host);
  for (const char* key : {"parsed_lines", "parsed_lines_count"})
    check(praktor_c11_value_store_set(values, key, blackboard.get(key).c_str()) == PRAKTOR_C11_VALUE_OK);
  {
    salts_bt_tree_config config{}; salts_bt_tree_config_init(&config);
    actions::c11::TreeExecutorSession session(tree, {}, config, hooks);
    salts_bt_node_status status = SALTS_BT_SUCCESS;
    check(session.tick(&status) == SALTS_OK && status == SALTS_BT_FAILURE);
    for (const char* key : {"parsed_lines", "parsed_lines_count"}) {
      char text[129]; size_t size = 0u;
      check(praktor_c11_value_store_get(values, key, text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
      check(blackboard.get(key) == text);
    }
    int contains = 1;
    check(!blackboard.has("after_lines"));
    check(praktor_c11_value_store_contains(values, "after_lines", &contains) == PRAKTOR_C11_VALUE_OK && !contains);
  }
  praktor_c11_native_host_destroy(&host);
}

void parseKeyValueDifferential() {
  const auto workflow = TaskParser::parseText(R"(
tasks:
  - name: native_pairs_contract
    sequence:
      - parse_keyvalue:
          input_key: "{input_port}"
          delimiter: "{delimiter_port}"
          line_separator: "{separator_port}"
          output_key: "{output_port}"
      - set_variable: after_pairs
        value: completed
)", "native-pairs-differential.yml");
  check(workflow.tasks.size() == 1u);
  actions::Tree tree;
  tree.root = textNode(std::get<OrchParams>(workflow.tasks.front().specifics).root);
  actions::Executor legacy;
  // Exhaust short data/delimiter/LF/space combinations against Legacy's map
  // and the SDK JSON serializer. Include multi-byte separators and aliases.
  for (const char* delimiter : {"=", "aa", ""}) {
    for (const char* separator : {"\n", "aa"}) {
      for (size_t length = 0u, combinations = 1u; length <= 5u; ++length, combinations *= 4u) {
        for (size_t row = 0u; row < combinations; ++row) {
          std::string input(length, 'a'); size_t digits = row;
          for (char& ch : input) { ch = "a=\n "[digits % 4u]; digits /= 4u; }
          actions::Blackboard blackboard; actions::ExecutionContext context;
          blackboard.set("input_port", "source"); blackboard.set("output_port", "source");
          blackboard.set("delimiter_port", delimiter); blackboard.set("separator_port", separator);
          blackboard.set("source", input);
          praktor_c11_native_host host{}; praktor_c11_bt_host hooks{};
          check(praktor_c11_native_host_init(&host, 8u, 64u, 256u) == SALTS_OK);
          check(praktor_c11_native_host_bind(&host, &hooks) == SALTS_OK);
          auto* values = praktor_c11_native_host_values(&host);
          for (const char* key : {"input_port", "output_port", "delimiter_port", "separator_port", "source"})
            check(praktor_c11_value_store_set(values, key, blackboard.get(key).c_str()) == PRAKTOR_C11_VALUE_OK);
          check(legacy.execute(tree, blackboard, context) == actions::NodeStatus::SUCCESS);
          {
            salts_bt_tree_config config{}; salts_bt_tree_config_init(&config);
            actions::c11::TreeExecutorSession session(tree, {}, config, hooks);
            salts_bt_node_status status = SALTS_BT_FAILURE;
            check(session.tick(&status) == SALTS_OK && status == SALTS_BT_SUCCESS);
            for (const char* key : {"source", "after_pairs"}) {
              char text[257]; size_t size = 0u;
              check(praktor_c11_value_store_get(values, key, text, sizeof(text), &size) == PRAKTOR_C11_VALUE_OK);
              check(blackboard.get(key) == text);
            }
          }
          praktor_c11_native_host_destroy(&host);
        }
      }
    }
  }
}

}
spec("C11 and Legacy YAML differential") {
  it("preserves ParseKeyValue YAML ports, sorted JSON and duplicate semantics") { parseKeyValueDifferential(); }
  it("preserves ParseLines YAML ports and every short newline combination") { parseLinesDifferential(); }
  it("preserves YAML value, copy and exit-code behavior") { differential(); }
  it("preserves terminal Sleep effects and cooperatively cancels") { sleepDifferential(); }
}
