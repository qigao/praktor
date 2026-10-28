add_library(praktor_yaml_value STATIC ${CMAKE_CURRENT_LIST_DIR}/yaml_value.c)
target_include_directories(praktor_yaml_value PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_yaml_value PUBLIC praktor_yaml_document Salts::JsonParser)
target_compile_features(praktor_yaml_value PUBLIC c_std_11)
set_target_properties(praktor_yaml_value PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
