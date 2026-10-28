include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/PraktorYamlTarget.cmake)
add_library(praktor_document_query STATIC ${CMAKE_CURRENT_LIST_DIR}/structured_document_query.c)
target_include_directories(praktor_document_query PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(praktor_document_query PUBLIC Salts::JsonParser PRIVATE
    Salts::Core Salts::YamlParser Salts::CYamlJsonAdapter Salts::XmlParser Salts::CsvParser)
target_compile_features(praktor_document_query PUBLIC c_std_11)
set_target_properties(praktor_document_query PROPERTIES C_STANDARD 11
    C_STANDARD_REQUIRED ON C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
