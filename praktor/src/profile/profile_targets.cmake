add_library(praktor_workflow_profile STATIC ${CMAKE_CURRENT_LIST_DIR}/workflow_profile.c)
target_include_directories(praktor_workflow_profile PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_compile_features(praktor_workflow_profile PUBLIC c_std_11)
set_target_properties(praktor_workflow_profile PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
