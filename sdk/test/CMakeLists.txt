set(TEST_SOURCES
    workflow_plan_api_test.cpp
    workflow_contract_api_test.cpp
    workflow_effect_manifest_test.cpp
    workflow_profile_test.cpp
    workflow_event_api_test.cpp
    workflow_agent_output_test.cpp
    host_tool_api_test.cpp
)

if(ENABLE_SCRIPT_ENGINE)
    list(APPEND TEST_SOURCES
        praktor_api_test.cpp
        template_contract_test.cpp
    )
else()
    list(APPEND TEST_SOURCES core_only_api_test.cpp)
endif()

cmake_add_test(
    SOURCES ${TEST_SOURCES}
    LIBS Catch2::Catch2WithMain ${TARGET_NAME_SDK} praktor_lib
    TARGET_SUFFIX ".${TARGET_NAME_SDK}"
    FOLDER "sdk/test"
)
