add_library(praktor_yaml_contract STATIC ${CMAKE_CURRENT_LIST_DIR}/yaml_contract.c)
target_include_directories(praktor_yaml_contract PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_yaml_contract PUBLIC praktor_yaml_value praktor_workflow_contract_value)
target_compile_features(praktor_yaml_contract PUBLIC c_std_11)
set_target_properties(praktor_yaml_contract PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
