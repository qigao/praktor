include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PraktorYamlTarget.cmake)
add_library(praktor_yaml_document STATIC ${CMAKE_CURRENT_LIST_DIR}/yaml_document.c
    ${CMAKE_CURRENT_LIST_DIR}/yaml_fields.c)
target_include_directories(praktor_yaml_document PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_yaml_document PUBLIC Salts::CYaml Salts::Core)
target_compile_features(praktor_yaml_document PUBLIC c_std_11)
set_target_properties(praktor_yaml_document PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
