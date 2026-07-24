# Phonometrica engine — build system.
# Copyright (C) 2019-2026 Julien Eychenne. GPLv3 (see LICENSE).
#
# The engine is an embeddable library with no third-party runtime dependencies
# beyond PCRE2 and a thread library (design/architecture.md §0). It lived in a
# separate repository until roadmap A8; it is now a subdirectory of the
# application build (add_subdirectory(phon/engine) from the root CMakeLists),
# and its history is preserved here via git subtree.
#
# Header paths are unchanged by the move: the engine's public headers are
# included as <phon/engine/...>, so the include root is the REPOSITORY root,
# not this directory. Everything else below (sources, tests, golden corpora,
# Unicode data, examples, benchmarks) stays relative to this directory.

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
    base/alloc.cpp
    base/heap.cpp
    base/unicode.cpp
    base/unicode_tables.cpp
    base/file_system.cpp
    object/class.cpp
    object/instance.cpp
    object/value_ops.cpp
    object/generic.cpp
    core/cell_memory.cpp
    core/cycle_collector.cpp
    types/atom.cpp
    types/string.cpp
    types/list.cpp
    types/table.cpp
    types/array.cpp
    types/file.cpp
    types/regex.cpp
    lib/array_kernels.cpp
    lib/array.cpp
    lib/file.cpp
    lib/json.cpp
    lib/list.cpp
    lib/math.cpp
    lib/regex.cpp
    lib/string.cpp
    lib/system.cpp
    lib/table.cpp
    concurrency/transfer.cpp
    concurrency/parallel.cpp
    concurrency/channel.cpp
    concurrency/spawn.cpp
    concurrency/thread_pool.cpp
    compile/token.cpp
    compile/source.cpp
    compile/scanner.cpp
    compile/ast.cpp
    compile/parser.cpp
    compile/ast_printer.cpp
    compile/disassembler.cpp
    compile/lower.cpp
    vm/opcode.cpp
    vm/function.cpp
    vm/isolate.cpp
    vm/interpreter.cpp
    runtime/bootstrap.cpp
    runtime/runtime.cpp
)
# <phon/engine/...> resolves from the repository root (see the header note above).
target_include_directories(phon_engine PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(phon_engine PUBLIC phon_flags)

find_package(Threads REQUIRED)
target_link_libraries(phon_engine PUBLIC Threads::Threads)

# PCRE2 (8-bit code units) backs the Regex engine, JIT enabled. The repository
# vendors it (phon/third_party/pcre2) and the root CMakeLists adds that
# subdirectory before this one, so the target is already defined; the
# find_library fallback keeps this file usable on its own.
if(TARGET pcre2-8)
    target_link_libraries(phon_engine PUBLIC pcre2-8)
else()
    find_library(PCRE2_LIBRARY NAMES pcre2-8 REQUIRED)
    find_path(PCRE2_INCLUDE_DIR NAMES pcre2.h REQUIRED)
    target_include_directories(phon_engine PUBLIC ${PCRE2_INCLUDE_DIR})
    target_link_libraries(phon_engine PUBLIC ${PCRE2_LIBRARY})
endif()

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
    test/unit/test_generic_array.cpp
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
target_include_directories(phon_unit_tests PRIVATE ${CMAKE_SOURCE_DIR} test/unit)
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
