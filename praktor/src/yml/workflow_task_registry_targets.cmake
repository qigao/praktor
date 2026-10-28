# Experimental C11 typed status/output ownership. Reuses canonical
# Workflow Snapshot task identity and SaltsUtils JsonParser JSON owners.
# No additional C++ parser/TaskRegistry or installed SDK surface.
add_library(praktor_workflow_task_registry STATIC
    ${CMAKE_CURRENT_LIST_DIR}/workflow_task_registry.c)
target_include_directories(praktor_workflow_task_registry PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_workflow_task_registry PUBLIC
    praktor_workflow_snapshot Salts::JsonParser)
target_compile_features(praktor_workflow_task_registry PUBLIC c_std_11)
set_target_properties(praktor_workflow_task_registry PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
