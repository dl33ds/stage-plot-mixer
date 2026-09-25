cmake_minimum_required(VERSION 3.22)

project(StagePlotMixer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Statically link the MSVC runtime so testers don't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# All sources are UTF-8.
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SPM_BUILD_TESTS "Build unit tests" ON)

# Short commit hash, embedded in diagnostics reports so results can be matched to a build.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE SPM_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT SPM_GIT_HASH)
    set(SPM_GIT_HASH "unknown")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

if(SPM_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src)
add_subdirectory(tools/spm-diag)
add_subdirectory(tools/spm-soak)

if(SPM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
