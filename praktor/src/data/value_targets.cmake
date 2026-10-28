add_library(praktor_workflow_value STATIC ${CMAKE_CURRENT_LIST_DIR}/workflow_value.c)
target_include_directories(praktor_workflow_value PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_workflow_value PUBLIC Salts::JsonParser Salts::Core)
if(UNIX)
    target_link_libraries(praktor_workflow_value PRIVATE m)
endif()
target_compile_features(praktor_workflow_value PUBLIC c_std_11)
set_target_properties(praktor_workflow_value PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
