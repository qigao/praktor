
add_executable(praktor main.c)
target_link_libraries(praktor PRIVATE Praktor)
target_compile_features(praktor PRIVATE c_std_11)
set_target_properties(praktor PROPERTIES FOLDER "app")
install(TARGETS praktor EXPORT PraktorTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
