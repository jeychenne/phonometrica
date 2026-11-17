# Post-build guard: fail the build if the generated documentation did not land
# where the application looks for it. Invoked as a POST_BUILD step with
#   cmake -DDOCS_INDEX=<deployed>/index.html -P cmake/verify_docs.cmake
# This is the last line of defence against silently shipping a bundle without
# help (e.g. an empty Sphinx output or a failed copy).

if(NOT DEFINED DOCS_INDEX OR DOCS_INDEX STREQUAL "")
    message(FATAL_ERROR "verify_docs.cmake: DOCS_INDEX was not provided.")
endif()

if(NOT EXISTS "${DOCS_INDEX}")
    message(FATAL_ERROR
        "Documentation was not bundled: '${DOCS_INDEX}' is missing.\n"
        "Sphinx may have produced no output, or the copy step failed. "
        "Reconfigure with a working Sphinx (see PHON_BUILD_DOCS) and rebuild, "
        "or set -DPHON_BUILD_DOCS=OFF to build without documentation on purpose.")
endif()
