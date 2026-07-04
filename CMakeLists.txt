# Phonometrica engine — build system.
# Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
#
# Standalone, embeddable library with no third-party runtime dependencies
# (design/architecture.md §0). Milestone M0: base layer + core containers.

cmake_minimum_required(VERSION 3.16)
project(phonometrica_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for clangd / tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(PHON_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(PHON_WERROR "Treat warnings as errors" OFF)

# ---------------------------------------------------------------------------
# Warnings and sanitizers (applied to all engine targets)
# ---------------------------------------------------------------------------

add_library(phon_flags INTERFACE)

if(MSVC)
    target_compile_options(phon_flags INTERFACE /W4 /permissive-)
    if(PHON_WERROR)
        target_compile_options(phon_flags INTERFACE /WX)
    endif()
else()
    target_compile_options(phon_flags INTERFACE -Wall -Wextra)
    if(PHON_WERROR)
        target_compile_options(phon_flags INTERFACE -Werror)
    endif()
    if(PHON_SANITIZE)
        target_compile_options(phon_flags INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(phon_flags INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------

add_library(phon_engine STATIC
    src/base/alloc.cpp
    src/base/heap.cpp
)
target_include_directories(phon_engine PUBLIC src)
target_link_libraries(phon_engine PUBLIC phon_flags)

find_package(Threads REQUIRED)
target_link_libraries(phon_engine PUBLIC Threads::Threads)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

add_executable(phon_unit_tests
    test/unit/main.cpp
    test/unit/test_bits.cpp
    test/unit/test_heap.cpp
    test/unit/test_vector.cpp
    test/unit/test_small_vector.cpp
    test/unit/test_flat_hash_map.cpp
    test/unit/test_flat_hash_set.cpp
)
target_include_directories(phon_unit_tests PRIVATE src test)
target_link_libraries(phon_unit_tests PRIVATE phon_engine phon_flags)

add_test(NAME unit COMMAND phon_unit_tests)
