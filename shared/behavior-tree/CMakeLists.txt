add_library(salts_behavior_tree STATIC src/behavior_tree.c)

cmake_config_target(
  salts_behavior_tree
  ALIAS Salts::BehaviorTree
  FOLDER "shared/behavior-tree"
  NO_INSTALL
  EXPORT_NAME BehaviorTree)

target_compile_features(salts_behavior_tree PUBLIC c_std_11)
target_include_directories(salts_behavior_tree PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_behavior_tree PUBLIC Salts::Core)

install(TARGETS salts_behavior_tree EXPORT PraktorTargets)
install(FILES include/behavior_tree.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
