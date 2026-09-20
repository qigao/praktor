if(ENABLE_SCRIPT_ENGINE)
    set(TEST_SOURCES
        praktor_api_test.cpp
        template_contract_test.cpp
    )
else()
    set(TEST_SOURCES core_only_api_test.cpp)
endif()

cmake_add_test(
    SOURCES ${TEST_SOURCES}
    LIBS Catch2::Catch2WithMain ${TARGET_NAME_SDK} praktor_lib
    TARGET_SUFFIX ".${TARGET_NAME_SDK}"
    FOLDER "sdk/test"
)
