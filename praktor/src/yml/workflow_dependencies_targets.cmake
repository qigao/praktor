# Qualification-only native task dependency graph, built from the canonical
# C-owned JSON metadata projection. No C++/executor/scheduler dependency.
add_library(praktor_workflow_dependencies STATIC
    ${CMAKE_CURRENT_LIST_DIR}/workflow_dependencies.c)
target_include_directories(praktor_workflow_dependencies PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_workflow_dependencies PUBLIC Salts::JsonParser)
target_compile_features(praktor_workflow_dependencies PUBLIC c_std_11)
set_target_properties(praktor_workflow_dependencies PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
