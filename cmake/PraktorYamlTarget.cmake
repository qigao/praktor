# Canonical SaltsUtils CYaml owner only. No target spelling shim or fallback.
if(NOT TARGET Salts::CYaml)
    message(FATAL_ERROR "Native C11 Praktor requires canonical Salts::CYaml")
endif()
