# SaltsUtils 4.3.0-rc.2 exports CYaml. Keep that supported SDK usable while
# consumers adopt the canonical YamlParser spelling. Remove this bridge when
# Praktor's minimum SDK version exports YamlParser itself.
if(NOT TARGET Salts::YamlParser)
    if(NOT TARGET Salts::CYaml)
        message(FATAL_ERROR "The selected SaltsUtils SDK has no YAML parser target")
    endif()
    add_library(Salts::YamlParser ALIAS Salts::CYaml)
endif()
