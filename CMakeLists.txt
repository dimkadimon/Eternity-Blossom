# Eternity-Blossom -- Eternity II attacked with Blossom VI perfect matching.
# SPDX-License-Identifier: MIT
#
# NOTE ON LICENSING
# -----------------
# Everything in this repository is MIT, EXCEPT the optional dependency
# "Blossom VI" (github.com/Paul566/blossom-vi, arXiv:2604.20351), which is
# GPL-3.0.  This project does not redistribute it: CMake clones it at configure
# time from upstream.  If you build with -DE2_USE_BLOSSOM_VI=ON (the default)
# the resulting *binary* is a combined work and must be distributed under the
# GPL-3.0; the source code in this repository remains MIT.  Build with
# -DE2_USE_BLOSSOM_VI=OFF for a pure-MIT build (the built-in fallback matching
# engine is then used -- correct but much slower, so only suitable for tiny
# graphs).

cmake_minimum_required(VERSION 3.20)
project(EternityBlossom LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(E2_USE_BLOSSOM_VI "Fetch and compile Blossom VI (GPL-3.0) as the matching engine" ON)
option(E2_NATIVE "Compile with -march=native" OFF)
set(BLOSSOM_VI_SOURCE_DIR "" CACHE PATH "Path to a local checkout of blossom-vi (else it is cloned)")

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Blossom VI (optional, GPL-3.0)
# ---------------------------------------------------------------------------
set(E2_BLOSSOM_SOURCES "")
set(E2_BLOSSOM_INCLUDE_DIRS "")
if(E2_USE_BLOSSOM_VI)
    if(BLOSSOM_VI_SOURCE_DIR STREQUAL "")
        set(BLOSSOM_VI_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/blossom-vi")
    endif()
    if(NOT EXISTS "${BLOSSOM_VI_SOURCE_DIR}/MWPMSolver.cpp")
        find_package(Git QUIET REQUIRED)
        message(STATUS "Cloning Blossom VI into ${BLOSSOM_VI_SOURCE_DIR} ...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} clone --depth 1 https://github.com/Paul566/blossom-vi.git
                    "${BLOSSOM_VI_SOURCE_DIR}"
            RESULT_VARIABLE _clone_result
            OUTPUT_QUIET ERROR_QUIET)
        if(NOT _clone_result EQUAL 0)
            message(WARNING "Could not clone Blossom VI; falling back to the built-in matcher.")
            set(E2_USE_BLOSSOM_VI OFF)
        endif()
    endif()
endif()

if(E2_USE_BLOSSOM_VI AND EXISTS "${BLOSSOM_VI_SOURCE_DIR}/MWPMSolver.cpp")
    message(STATUS "Using Blossom VI from ${BLOSSOM_VI_SOURCE_DIR} (GPL-3.0)")
    list(APPEND E2_BLOSSOM_SOURCES
            "${BLOSSOM_VI_SOURCE_DIR}/MWPMSolver.cpp"
            "${BLOSSOM_VI_SOURCE_DIR}/DualUpdater.cpp")
    list(APPEND E2_BLOSSOM_INCLUDE_DIRS "${BLOSSOM_VI_SOURCE_DIR}")
else()
    message(STATUS "Using the built-in fallback matching engine (MIT only)")
endif()

# ---------------------------------------------------------------------------
# Boost (Blossom VI needs boost/container/small_vector.hpp).  If Boost is not
# installed we fall back to a two-line shim in third_party/, so the project
# still builds in a bare container.
# ---------------------------------------------------------------------------
find_package(Boost QUIET)
if(Boost_FOUND)
    message(STATUS "Boost headers: ${Boost_INCLUDE_DIRS}")
    list(APPEND E2_BLOSSOM_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
else()
    message(STATUS "Boost not found -- using the bundled small_vector shim")
    list(APPEND E2_BLOSSOM_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boost_shim")
endif()

# ---------------------------------------------------------------------------
add_library(e2_core STATIC
        src/matching.cpp
        src/operators.cpp
        src/search.cpp
        src/assign.cpp
        src/coarse.cpp
        src/dissect.cpp
        src/rotation.cpp
        src/subsolve.cpp
        src/selftest_extra.cpp
        ${E2_BLOSSOM_SOURCES})

target_include_directories(e2_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(e2_core SYSTEM PRIVATE ${E2_BLOSSOM_INCLUDE_DIRS})
if(E2_BLOSSOM_SOURCES)
    # Blossom VI sources include each other by relative path.
    target_include_directories(e2_core PRIVATE ${E2_BLOSSOM_INCLUDE_DIRS})
    target_compile_definitions(e2_core PUBLIC E2_HAVE_BLOSSOM_VI=1)
endif()
target_link_libraries(e2_core PUBLIC Threads::Threads)

add_executable(eternity-blossom src/main.cpp)
target_link_libraries(eternity-blossom PRIVATE e2_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(e2_core PRIVATE -O3 -Wall -Wextra)
    target_compile_options(eternity-blossom PRIVATE -O3 -Wall -Wextra)
    if(E2_NATIVE)
        target_compile_options(e2_core PRIVATE -march=native)
        target_compile_options(eternity-blossom PRIVATE -march=native)
    endif()
endif()

enable_testing()
add_test(NAME selftest COMMAND eternity-blossom selftest --pieces "${CMAKE_CURRENT_SOURCE_DIR}/data/e2pieces.txt" --trials 10)
