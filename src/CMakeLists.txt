find_package(Git REQUIRED)
execute_process(
  COMMAND ${GIT_EXECUTABLE} tag --points-at HEAD
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  OUTPUT_VARIABLE git_tag
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if ("${git_tag}" STREQUAL "")
  set(git_tag "vX.X.X")
endif()

execute_process(
  COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  OUTPUT_VARIABLE git_revision
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if ("${git_revision}" STREQUAL "")
  set(git_revision "unknown")
endif()

add_custom_target(generate_version_h COMMAND ${CMAKE_COMMAND}
  -Dinput_file="${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
  -Doutput_file="${CMAKE_CURRENT_BINARY_DIR}/version.h"
  -Dnewline="LF"
  -Dgit_tag="${git_tag}"
  -Dgit_revision="${git_revision}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/replace.cmake"
)
add_custom_target(generate_readme COMMAND ${CMAKE_COMMAND}
  -Dinput_file="${PROJECT_SOURCE_DIR}/README.md"
  -Doutput_file="${PROJECT_BINARY_DIR}/bin/bridge.txt"
  -Dnewline="CRLF"
  -Dgit_tag="${git_tag}"
  -Dgit_revision="${git_revision}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/replace.cmake"
)
find_program(LUA51DLL lua51.dll CMAKE_FIND_ROOT_PATH_BOTH)
add_custom_target(generate_importlib COMMAND
  ${CMAKE_COMMAND}
  -Dlocal_dir="${CMAKE_CURRENT_BINARY_DIR}"
  -Ddll="${LUA51DLL}"
  -Ddef="${CMAKE_CURRENT_BINARY_DIR}/lua51.def"
  -Dlib="${CMAKE_CURRENT_BINARY_DIR}/liblua51.a"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/makelib-llvm.cmake"
)

add_library(bridge_dll SHARED)
set_target_properties(bridge_dll PROPERTIES
  OUTPUT_NAME "bridge.dll"
  PREFIX ""
  SUFFIX ""
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)
target_sources(bridge_dll PRIVATE
  luamain.c
  process.c
  bridge.c
  ods.c
)
target_link_libraries(bridge_dll PRIVATE
  lua51
)
target_include_directories(bridge_dll PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" # for version.h
)
target_link_directories(bridge_dll PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}" # for liblua51.a
)
add_dependencies(bridge_dll generate_version_h generate_importlib generate_readme)
set(targets bridge_dll)

foreach(target ${targets})
  target_compile_definitions(${target} PRIVATE
    _WIN32_WINNT=0x0601
    _WINDOWS
    $<$<CONFIG:Release>:NDEBUG>
  )
  target_compile_options(${target} PRIVATE
    --rtlib=compiler-rt
    -mstackrealign
    -Wall
    -Wextra
    -Werror
    -Weverything
    -Wshadow
    -Werror=return-type
    -pedantic-errors
    -ffunction-sections
    -fdata-sections
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Release>:-O2>
    -flto
  )
  target_link_options(${target} PRIVATE
    -fuse-ld=lld
    -Wl,-delayload,lua51.dll
    -Wl,--gc-sections
    # -Wl,--print-gc-sections
    --rtlib=compiler-rt
    -no-pthread
    -static
    -Wl,--kill-at
    $<$<CONFIG:Release>:-s>
  )
endforeach(target)
