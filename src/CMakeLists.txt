add_library(aviutl_plugin SHARED)
set_target_properties(aviutl_plugin PROPERTIES OUTPUT_NAME "bridge.auf" PREFIX "" SUFFIX "")
target_sources(aviutl_plugin PRIVATE
  main.c
  process.c
  bridge.c
  ods.c
)

add_library(lua_plugin SHARED)
set_target_properties(lua_plugin PROPERTIES OUTPUT_NAME "bridge.dll" PREFIX "" SUFFIX "")
target_sources(lua_plugin PRIVATE
  luamain.c
)
target_link_libraries(lua_plugin PRIVATE
  lua5.1
)

set(targets aviutl_plugin lua_plugin)
foreach(target ${targets})

if (("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU") OR
    ("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang"))

    target_compile_definitions(${target} PRIVATE
        _WIN32_WINNT=0x0601
        $<$<CONFIG:Release>:NDEBUG=1>
    )
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -pedantic-errors
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Release>:-O3>
    )
    target_link_options(${target} PRIVATE
        $<$<CONFIG:Release>:-s>
    )

elseif("${CMAKE_C_COMPILER_ID}" STREQUAL "MSVC")

    target_compile_options(${target} INTERFACE
        -W4
    )
    target_compile_definitions(${target} INTERFACE
        _WIN32_WINNT=0x0600
    )

endif()

endforeach(target)
