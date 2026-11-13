cmake_add_test(
  SOURCES behavior_tree_test.c
  LIBS Salts::BehaviorTree Salts::TinyTest
  FOLDER "shared/behavior-tree/tests")

cmake_add_test(
  SOURCES behavior_tree_header_cpp_test.cpp
  LIBS Salts::BehaviorTree Salts::TinyTest
  FOLDER "shared/behavior-tree/tests")
