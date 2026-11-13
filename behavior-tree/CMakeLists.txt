add_library(salts_behavior_tree STATIC src/behavior_tree.c)

cmake_config_target(
  salts_behavior_tree
  ALIAS Salts::BehaviorTree
  FOLDER "behavior-tree"
  EXPORT_NAME BehaviorTree)

target_compile_features(salts_behavior_tree PUBLIC c_std_11)
target_include_directories(salts_behavior_tree PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(salts_behavior_tree PUBLIC Salts::Core)

install(TARGETS salts_behavior_tree EXPORT SaltsUtilsTargets)
install(DIRECTORY include/salts DESTINATION include FILES_MATCHING PATTERN "*.h")

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()
