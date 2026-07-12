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
option(PHON_TSAN "Build with ThreadSanitizer (M7 concurrency acceptance bar)" OFF)
option(PHON_WERROR "Treat warnings as errors" OFF)

if(PHON_SANITIZE AND PHON_TSAN)
    message(FATAL_ERROR "PHON_SANITIZE (ASan) and PHON_TSAN are mutually exclusive")
endif()

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
    if(PHON_TSAN)
        target_compile_options(phon_flags INTERFACE
            -fsanitize=thread -fno-omit-frame-pointer -g)
        target_link_options(phon_flags INTERFACE -fsanitize=thread)
    endif()
endif()

# ---------------------------------------------------------------------------
# Engine library
# ---------------------------------------------------------------------------

add_library(phon_engine STATIC
    phon/engine/base/alloc.cpp
    phon/engine/base/heap.cpp
    phon/engine/base/unicode.cpp
    phon/engine/base/unicode_tables.cpp
    phon/engine/base/file_system.cpp
    phon/engine/object/class.cpp
    phon/engine/object/instance.cpp
    phon/engine/object/value_ops.cpp
    phon/engine/object/generic.cpp
    phon/engine/core/cell_memory.cpp
    phon/engine/core/cycle_collector.cpp
    phon/engine/types/atom.cpp
    phon/engine/types/string.cpp
    phon/engine/types/list.cpp
    phon/engine/types/table.cpp
    phon/engine/types/array.cpp
    phon/engine/types/file.cpp
    phon/engine/types/regex.cpp
    phon/engine/lib/array_kernels.cpp
    phon/engine/lib/array.cpp
    phon/engine/lib/file.cpp
    phon/engine/lib/json.cpp
    phon/engine/lib/list.cpp
    phon/engine/lib/math.cpp
    phon/engine/lib/regex.cpp
    phon/engine/lib/string.cpp
    phon/engine/lib/system.cpp
    phon/engine/concurrency/transfer.cpp
    phon/engine/concurrency/parallel.cpp
    phon/engine/concurrency/channel.cpp
    phon/engine/concurrency/spawn.cpp
    phon/engine/concurrency/thread_pool.cpp
    phon/engine/compile/token.cpp
    phon/engine/compile/source.cpp
    phon/engine/compile/scanner.cpp
    phon/engine/compile/ast.cpp
    phon/engine/compile/parser.cpp
    phon/engine/compile/ast_printer.cpp
    phon/engine/compile/disassembler.cpp
    phon/engine/compile/lower.cpp
    phon/engine/vm/opcode.cpp
    phon/engine/vm/function.cpp
    phon/engine/vm/isolate.cpp
    phon/engine/vm/interpreter.cpp
    phon/engine/runtime/bootstrap.cpp
    phon/engine/runtime/runtime.cpp
)
target_include_directories(phon_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(phon_engine PUBLIC phon_flags)

find_package(Threads REQUIRED)
target_link_libraries(phon_engine PUBLIC Threads::Threads)

# PCRE2 (8-bit code units) backs the Regex engine, JIT enabled.
find_library(PCRE2_LIBRARY NAMES pcre2-8 REQUIRED)
find_path(PCRE2_INCLUDE_DIR NAMES pcre2.h REQUIRED)
target_include_directories(phon_engine PUBLIC ${PCRE2_INCLUDE_DIR})
target_link_libraries(phon_engine PUBLIC ${PCRE2_LIBRARY})

# ---------------------------------------------------------------------------
# Example host application (architecture §15 — M8 acceptance deliverable)
# ---------------------------------------------------------------------------

add_executable(phon_repl examples/repl.cpp)
target_link_libraries(phon_repl PRIVATE phon_engine phon_flags)

# ---------------------------------------------------------------------------
# Benchmarks (architecture §14)
# ---------------------------------------------------------------------------

add_executable(phon_bench bench/bench.cpp)
target_link_libraries(phon_bench PRIVATE phon_engine phon_flags)
target_compile_definitions(phon_bench PRIVATE
    PHON_BENCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}/bench/scripts")

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
    test/unit/test_cycle_collector.cpp
    test/unit/test_class.cpp
    test/unit/test_instance.cpp
    test/unit/test_dispatch.cpp
    test/unit/test_unicode.cpp
    test/unit/test_string.cpp
    test/unit/test_atom.cpp
    test/unit/test_list.cpp
    test/unit/test_table.cpp
    test/unit/test_set.cpp
    test/unit/test_array.cpp
    test/unit/test_grapheme_conformance.cpp
    test/unit/test_scanner.cpp
    test/unit/test_ast.cpp
    test/unit/test_parser.cpp
    test/unit/test_golden_ast.cpp
    test/unit/test_vm.cpp
    test/unit/test_golden_disasm.cpp
    test/unit/test_scripts.cpp
    test/unit/test_concurrency.cpp
    test/unit/test_modules.cpp
    test/unit/test_embed.cpp
    test/unit/test_file_encoding.cpp
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

# The example host app is an acceptance deliverable: both the worker-thread demo and a
# script-file run must exit 0 (the demo checks its own result sum; file mode returns
# nonzero on any script error). These also run under the ASan/TSan build configs.
add_test(NAME example_workers COMMAND phon_repl --workers)
add_test(NAME example_file COMMAND phon_repl ${CMAKE_CURRENT_SOURCE_DIR}/examples/demo.phon)
