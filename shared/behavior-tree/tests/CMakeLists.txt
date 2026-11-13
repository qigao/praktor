cmake_add_test(
  SOURCES behavior_tree_test.c
  LIBS Salts::BehaviorTree Salts::TinyTest
  FOLDER "shared/behavior-tree/tests")

set_target_properties(behavior_tree_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

enable_language(CXX)
cmake_add_test(
  SOURCES behavior_tree_header_cpp_test.cpp
  LIBS Salts::BehaviorTree Salts::TinyTest
  FOLDER "shared/behavior-tree/tests")

set_target_properties(behavior_tree_header_cpp_test PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
