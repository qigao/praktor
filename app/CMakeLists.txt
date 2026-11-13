
add_executable(praktor main.c)
target_link_libraries(praktor PRIVATE Praktor)
set_target_properties(praktor PROPERTIES FOLDER "app")
install(TARGETS praktor EXPORT PraktorTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
