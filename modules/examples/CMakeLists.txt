set(folder_name demo)

add_executable(qjsimp
        qjsimp.c)
target_link_libraries(qjsimp
        libuv::uv qjs)
set_target_properties(qjsimp PROPERTIES FOLDER ${folder_name})

add_executable(qjsuv
        qjsuv.c)
target_include_directories(qjsuv PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(qjsuv quick)
set_target_properties(qjsuv PROPERTIES FOLDER ${folder_name})
