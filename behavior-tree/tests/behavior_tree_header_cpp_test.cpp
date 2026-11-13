#include <salts/behavior_tree.h>

int main() {
  salts_bt_tree tree{};
  salts_bt_tree_config config{};
  salts_bt_tree_config_init(&config);
  if (config.max_nodes == 0u || config.max_depth == 0u) return 1;
  if (salts_bt_tree_init(&tree, &config) != SALTS_OK) return 2;
  if (salts_bt_tree_destroy(&tree) != SALTS_OK) return 3;
  return tree.impl == nullptr ? 0 : 4;
}
