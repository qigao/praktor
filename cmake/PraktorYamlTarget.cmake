# Some supported SDKs export the parser as CYaml. This is a target spelling
# bridge to the same provider, with no alternate parser implementation.
if(NOT TARGET Salts::YamlParser)
    if(NOT TARGET Salts::CYaml)
        message(FATAL_ERROR "The selected SaltsUtils SDK has no YAML parser target")
    endif()
    add_library(Salts::YamlParser ALIAS Salts::CYaml)
endif()
