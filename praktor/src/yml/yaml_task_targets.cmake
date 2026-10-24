add_library(praktor_yaml_task_fields STATIC ${CMAKE_CURRENT_LIST_DIR}/yaml_task_fields.c)
target_include_directories(praktor_yaml_task_fields PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_yaml_task_fields PUBLIC praktor_yaml_document PRIVATE Salts::CSTL)
target_compile_features(praktor_yaml_task_fields PUBLIC c_std_11)
set_target_properties(praktor_yaml_task_fields PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
