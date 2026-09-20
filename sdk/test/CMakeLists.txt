set(TEST_SOURCES
    praktor_api_test.cpp
    template_contract_test.cpp
)

cmake_add_test(
    SOURCES ${TEST_SOURCES}
    LIBS Catch2::Catch2WithMain ${TARGET_NAME_SDK} praktor_lib
    TARGET_SUFFIX ".${TARGET_NAME_SDK}"
    FOLDER "sdk/test"
)
