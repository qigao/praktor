# Salts CMake Utilities

function(cmake_config_target target_name)
    set(options NO_INSTALL NO_VERSION)
    set(oneValueArgs FOLDER VERSION SOVERSION EXPORT_NAME ALIAS OUTPUT_NAME)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_target_property(target_type ${target_name} TYPE)
    if(NOT target_type)
        message(FATAL_ERROR "cmake_config_target: target '${target_name}' does not exist")
    endif()

    if(ARG_ALIAS)
        if(target_type STREQUAL "EXECUTABLE")
            add_executable(${ARG_ALIAS} ALIAS ${target_name})
        else()
            add_library(${ARG_ALIAS} ALIAS ${target_name})
        endif()
    endif()

    if(ARG_FOLDER)
        set_target_properties(${target_name} PROPERTIES FOLDER ${ARG_FOLDER})
    endif()

    if(ARG_EXPORT_NAME)
        set_target_properties(${target_name} PROPERTIES EXPORT_NAME ${ARG_EXPORT_NAME})
    endif()

    if(ARG_OUTPUT_NAME)
        set_target_properties(${target_name} PROPERTIES OUTPUT_NAME ${ARG_OUTPUT_NAME})
    endif()

    if(NOT ARG_NO_VERSION AND
       (target_type STREQUAL "SHARED_LIBRARY" OR target_type STREQUAL "STATIC_LIBRARY"))
        if(NOT ARG_VERSION AND PROJECT_VERSION)
            set(ARG_VERSION ${PROJECT_VERSION})
        endif()
        if(NOT ARG_SOVERSION AND PROJECT_VERSION_MAJOR)
            set(ARG_SOVERSION ${PROJECT_VERSION_MAJOR})
        endif()
        
        if(ARG_VERSION)
          set_target_properties(${target_name} PROPERTIES VERSION ${ARG_VERSION})
        endif()
        if(ARG_SOVERSION)
          set_target_properties(${target_name} PROPERTIES SOVERSION ${ARG_SOVERSION})
        endif()
    endif()

    if(NOT ARG_NO_INSTALL)
        if(NOT CMAKE_CONFIG_TARGET_EXPORT_SET)
            message(FATAL_ERROR
                "cmake_config_target: CMAKE_CONFIG_TARGET_EXPORT_SET must name the export set")
        endif()

        install(
            TARGETS ${target_name}
            EXPORT ${CMAKE_CONFIG_TARGET_EXPORT_SET}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

endfunction()

function(cmake_install_headers)
    set(options)
    set(oneValueArgs DIRECTORY DESTINATION)
    set(multiValueArgs PATTERNS EXCLUDES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(ARG_DIRECTORY)
        set(match_args FILES_MATCHING PATTERN "*.h")
        foreach(p ${ARG_PATTERNS})
            list(APPEND match_args PATTERN "${p}")
        endforeach()
        foreach(e ${ARG_EXCLUDES})
            list(APPEND match_args PATTERN "${e}" EXCLUDE)
        endforeach()

        if(NOT ARG_DESTINATION)
            set(ARG_DESTINATION "include")
        endif()

        install(DIRECTORY ${ARG_DIRECTORY}
            DESTINATION ${ARG_DESTINATION}
            ${match_args}
        )
    endif()
endfunction()

function(cmake_add_source VAR)
  set(options RECURSE)
  set(oneValueArgs)
  set(multiValueArgs DIRS EXCLUDES PATTERNS)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(glob_mode GLOB)
  if(ARG_RECURSE)
    set(glob_mode GLOB_RECURSE)
  endif()

  if(NOT ARG_DIRS)
    set(ARG_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/include")
  endif()

  if(NOT ARG_PATTERNS)
    set(ARG_PATTERNS "*.c" "*.cpp" "*.h" "*.hpp" "*.cc" "*.hh")
  endif()

  set(patterns)
  foreach(dir ${ARG_DIRS})
    foreach(pat ${ARG_PATTERNS})
      list(APPEND patterns "${dir}/${pat}")
    endforeach()
  endforeach()

  file(${glob_mode} collected ${patterns})

  if(ARG_EXCLUDES)
    list(REMOVE_ITEM collected ${ARG_EXCLUDES})
  endif()

  set(${VAR} ${collected} PARENT_SCOPE)
endfunction()

function(cmake_add_test)
  set(options)
  set(oneValueArgs FOLDER TARGET_SUFFIX OUT_TARGETS)
  set(multiValueArgs SOURCES LIBS DEFS INCLUDES FEATURES)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(created_targets)
  foreach(src ${ARG_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(target_name "${name}${ARG_TARGET_SUFFIX}")
    if(NOT TARGET ${target_name})
      add_executable(${target_name} ${src})
      target_link_libraries(${target_name} PRIVATE ${ARG_LIBS})
      if(ARG_DEFS)
        target_compile_definitions(${target_name} PRIVATE ${ARG_DEFS})
      endif()
      if(ARG_INCLUDES)
        target_include_directories(${target_name} PRIVATE ${ARG_INCLUDES})
      endif()
      if(ARG_FEATURES)
        target_compile_features(${target_name} PRIVATE ${ARG_FEATURES})
      endif()
      add_test(NAME ${target_name} COMMAND ${target_name})
      
      if(ARG_FOLDER)
        set_target_properties(${target_name} PROPERTIES FOLDER ${ARG_FOLDER})
      endif()

      list(APPEND created_targets ${target_name})
    endif()
  endforeach()

  if(ARG_OUT_TARGETS)
    set(${ARG_OUT_TARGETS} ${created_targets} PARENT_SCOPE)
  endif()
endfunction()

function(cmake_add_benchmark)
  set(options)
  set(oneValueArgs FOLDER)
  set(multiValueArgs SOURCES LIBS DEFS INCLUDES)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  foreach(src ${ARG_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    if(NOT TARGET ${name})
      add_executable(${name} ${src})
      target_link_libraries(${name} PRIVATE ${ARG_LIBS})
      target_compile_definitions(${name} PRIVATE ${ARG_DEFS})
      target_include_directories(${name} PRIVATE ${ARG_INCLUDES})

      if(ARG_FOLDER)
        set_target_properties(${name} PROPERTIES FOLDER ${ARG_FOLDER})
      endif()
    endif()
  endforeach()
endfunction()
