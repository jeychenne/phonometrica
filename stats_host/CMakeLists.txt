# Headless statistics host (MIGRATION_NOTES step 4b): the phon/analysis layer plus a script
# host (phon_stats) that registers the statistics natives and runs .phon files. It is the
# headless test runner for test/statistics (Debug fits through the GUI binary take hours).
#
# Include-order contract (load-bearing):
#   1. shim/            — shadows <phon/application/data_table.hpp> with the headless table
#   2. repository root  — the app's phon/*.hpp forwarders (phon/error.hpp's error(...) helper
#                         in particular), the engine's <phon/engine/...> headers they resolve
#                         onto, <phon/analysis/...>, <phon/utils/matrix.hpp>, vendored
#                         Eigen/LBFGSpp
# Since A8 the engine lives under the repository root too, so this is one root, not two.
# The one remaining shim is the headless DataTable: the real one derives from
# Document/Element and would drag the entire VFS, project and audio layer into this target
# (MIGRATION_NOTES "A6").

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
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/phon/third_party # bare <Eigen/Dense> includes in the analysis layer
)

target_link_libraries(phon_stats PRIVATE phon_engine)
set_target_properties(phon_stats PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
