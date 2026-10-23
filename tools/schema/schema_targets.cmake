add_library(praktor_schema_projection STATIC ${CMAKE_CURRENT_LIST_DIR}/projection.c)
target_include_directories(praktor_schema_projection PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(praktor_schema_projection PUBLIC Salts::JsonParser PRIVATE Salts::IDL Salts::Schema Salts::Core)
target_compile_features(praktor_schema_projection PUBLIC c_std_11)
set_target_properties(praktor_schema_projection PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON FOLDER tools/schema)

add_executable(praktor-schema ${CMAKE_CURRENT_LIST_DIR}/main.c)
target_link_libraries(praktor-schema PRIVATE praktor_schema_projection Salts::Core)
set_target_properties(praktor-schema PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF LINKER_LANGUAGE C FOLDER tools/schema)
