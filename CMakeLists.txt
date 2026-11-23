cmake_minimum_required(VERSION 3.20)
project(PCScript VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Explicit output directories so CI and the installer never guess where binaries land.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} ${CMAKE_BINARY_DIR}/bin)
endforeach()

if(MSVC)
    add_compile_options(/W4 /utf-8 /EHsc /permissive-)
    add_link_options(/STACK:134217728)    # 128 MB stack for deep PCScript recursion
else()
    add_compile_options(-Wall -Wextra)
endif()

add_library(pcscript_core STATIC
    src/common/diagnostics.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/semantic/semantic.cpp
    src/runtime/value.cpp
    src/runtime/interpreter.cpp
    src/stdlib/builtins.cpp)
target_include_directories(pcscript_core PUBLIC src)

add_executable(pcscript src/pcscript.cpp)
target_link_libraries(pcscript PRIVATE pcscript_core)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(pcscript PRIVATE Threads::Threads)
endif()

enable_testing()
add_subdirectory(tests)
add_subdirectory(studio)

if(WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/editor/CMakeLists.txt)
    add_subdirectory(editor)
endif()
