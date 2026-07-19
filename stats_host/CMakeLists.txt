# Headless statistics host (MIGRATION_NOTES step 4b): the phon/analysis layer compiled
# against the NEW engine, plus a script host (phon_stats) that registers the statistics
# natives and runs .phon files. Only built when PHON_WITH_NEW_ENGINE is ON.
#
# Include-order contract (load-bearing):
#   1. shim/               — shadows <phon/application/data_table.hpp> with the headless table
#   2. ${PHON_ENGINE_DIR}  — provides the <phon/string.hpp>/<phon/array.hpp>/... facades,
#                            so the analysis sources bind to the NEW engine's base types
#   3. repository root     — everything else (<phon/analysis/...>, <phon/utils/matrix.hpp>,
#                            vendored Eigen/LBFGSpp)
# The old engine (phon-runtime) is never on this target's include path or link line: the
# two engines share type names and must not meet in one translation unit (coexistence
# rule, MIGRATION_NOTES "Coexistence").

add_executable(phon_stats
    main.cpp
    bindings.cpp
    data_table.cpp
    printers.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/formula.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/formula_engine.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/fitting.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/mixed_model.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/regression.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/smooth.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/statistics.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/bayesian.cpp
    ${CMAKE_SOURCE_DIR}/phon/analysis/model_comparison.cpp
    ${CMAKE_SOURCE_DIR}/phon/utils/matrix.cpp
)

target_include_directories(phon_stats BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${PHON_ENGINE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/phon/third_party # bare <Eigen/Dense> includes in the analysis layer
)

target_link_libraries(phon_stats PRIVATE phon_engine)
set_target_properties(phon_stats PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
