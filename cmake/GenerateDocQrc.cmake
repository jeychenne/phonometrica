# GenerateDocQrc.cmake
#
# Generates a Qt resource file (.qrc) listing every embeddable file produced
# by the Sphinx qthelp builder.  Called at build time by add_custom_command.
#
# Expected variables (passed with -D):
#   DOC_DIR  — absolute path to the Sphinx qthelp output directory
#   QRC_FILE — absolute path to the .qrc file to generate

file(GLOB_RECURSE ALL_FILES RELATIVE "${DOC_DIR}" "${DOC_DIR}/*")

# Filter out files we don't want in the binary.
list(FILTER ALL_FILES EXCLUDE REGEX "\\.js$")          # QTextBrowser can't run JS
list(FILTER ALL_FILES EXCLUDE REGEX "\\.qhp$")         # Qt Help project file — not needed
list(FILTER ALL_FILES EXCLUDE REGEX "\\.buildinfo$")    # Sphinx build metadata
list(FILTER ALL_FILES EXCLUDE REGEX "objects\\.inv$")   # intersphinx inventory
list(FILTER ALL_FILES EXCLUDE REGEX "\\.stamp$")        # our own stamp file

set(QRC "<RCC>\n<qresource prefix=\"/docs\">\n")
foreach(F IN LISTS ALL_FILES)
    string(APPEND QRC "    <file alias=\"${F}\">${DOC_DIR}/${F}</file>\n")
endforeach()
string(APPEND QRC "</qresource>\n</RCC>\n")

file(WRITE "${QRC_FILE}" "${QRC}")
