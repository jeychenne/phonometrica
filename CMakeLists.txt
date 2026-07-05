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
    phon/base/alloc.cpp
    phon/base/heap.cpp
    phon/base/unicode.cpp
    phon/base/unicode_tables.cpp
    phon/object/class.cpp
    phon/object/instance.cpp
    phon/object/value_ops.cpp
    phon/dispatch/generic.cpp
    phon/memory/cell_memory.cpp
    phon/types/atom.cpp
    phon/types/string.cpp
    phon/types/list.cpp
    phon/types/table.cpp
    phon/compile/token.cpp
    phon/compile/source.cpp
    phon/compile/scanner.cpp
    phon/compile/ast.cpp
    phon/compile/parser.cpp
    phon/compile/ast_printer.cpp
    phon/compile/disassembler.cpp
    phon/compile/lower.cpp
    phon/vm/opcode.cpp
    phon/vm/function.cpp
    phon/vm/isolate.cpp
    phon/vm/interpreter.cpp
    phon/runtime/bootstrap.cpp
    phon/runtime/runtime.cpp
)
target_include_directories(phon_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
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
    test/unit/test_hash.cpp
    test/unit/test_heap.cpp
    test/unit/test_vector.cpp
    test/unit/test_small_vector.cpp
    test/unit/test_flat_hash_map.cpp
    test/unit/test_flat_hash_set.cpp
    test/unit/test_value.cpp
    test/unit/test_cell.cpp
    test/unit/test_class.cpp
    test/unit/test_instance.cpp
    test/unit/test_dispatch.cpp
    test/unit/test_unicode.cpp
    test/unit/test_string.cpp
    test/unit/test_atom.cpp
    test/unit/test_list.cpp
    test/unit/test_table.cpp
    test/unit/test_set.cpp
    test/unit/test_grapheme_conformance.cpp
    test/unit/test_scanner.cpp
    test/unit/test_ast.cpp
    test/unit/test_parser.cpp
    test/unit/test_golden_ast.cpp
    test/unit/test_vm.cpp
    test/unit/test_golden_disasm.cpp
    test/unit/test_scripts.cpp
)
target_include_directories(phon_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} test/unit)
target_link_libraries(phon_unit_tests PRIVATE phon_engine phon_flags)
# Path to the vendored Unicode data, for the UAX #29 conformance test.
target_compile_definitions(phon_unit_tests PRIVATE
    PHON_UNICODE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tools/unicode/data")
# Path to the golden AST-dump corpus (test/golden/ast).
target_compile_definitions(phon_unit_tests PRIVATE
    PHON_GOLDEN_AST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/golden/ast")
# Path to the golden disassembly corpus (test/golden/disasm).
target_compile_definitions(phon_unit_tests PRIVATE
    PHON_GOLDEN_DISASM_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/golden/disasm")
# Path to the .phon acceptance scripts (test/scripts).
target_compile_definitions(phon_unit_tests PRIVATE
    PHON_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/scripts")

add_test(NAME unit COMMAND phon_unit_tests)
