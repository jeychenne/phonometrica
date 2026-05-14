###################################
##  CMAKE FILE FOR PHONOMETRICA  ##
###################################

cmake_minimum_required(VERSION 3.20)
if(APPLE)
    project(Phonometrica LANGUAGES C CXX)
else()
    project(phonometrica LANGUAGES C CXX)
endif(APPLE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


###########################
##  COMPILATION OPTIONS  ##
###########################

option(EMBED_SCRIPTS      "Embed scripts as strings"            ON)
option(ENABLE_LOGGING     "Enable logging (always on in debug)" OFF)
option(WITH_APPLICATION   "Build the application layer"         ON)
option(WITH_GUI           "Build the user interface"            ON)
option(BUILD_UNIT_TEST    "Build unit tests"                    OFF)
option(PHON_BUILD_DOCS    "Build and embed documentation (requires sphinx-build)" ON)
option(WITH_WHISPER       "Enable audio transcription via whisper.cpp" ON)


###########################
##  VERSION INFORMATION  ##
###########################

set(PHON_VERSION_MAJOR 0)
set(PHON_VERSION_MINOR 9)
set(PHON_VERSION_MICRO 4)
set(PHON_VERSION_DEVEL 0)

set(PHON_RELEASE_DATE_YEAR  2026)
set(PHON_RELEASE_DATE_MONTH 5)
set(PHON_RELEASE_DATE_DAY   14)

add_definitions(
    -DPHON_VERSION_MAJOR=${PHON_VERSION_MAJOR}
    -DPHON_VERSION_MINOR=${PHON_VERSION_MINOR}
    -DPHON_VERSION_MICRO=${PHON_VERSION_MICRO}
    -DPHON_VERSION_DEVEL=${PHON_VERSION_DEVEL}
    -DPHON_RELEASE_DATE_YEAR=${PHON_RELEASE_DATE_YEAR}
    -DPHON_RELEASE_DATE_MONTH=${PHON_RELEASE_DATE_MONTH}
    -DPHON_RELEASE_DATE_DAY=${PHON_RELEASE_DATE_DAY}
)


###########
## DEBUG ##
###########

#add_definitions(-DPHON_INLA_DEBUG=1)
#add_definitions(-DPHON_INLA_BAYES_DIAG=1)

########################
##  OPERATING SYSTEM  ##
########################

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -D_USE_MATH_DEFINES)
    add_definitions(-DPHON_WINDOWS=1 -DPHON_MACOS=0 -DPHON_LINUX=0 -DPHON_UNIX=0)
    # Set this variable before calling cmake on Windows
    include_directories(${BOOST_ROOT})
    # Undefine macros for std::min and std::max
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

    add_compile_options(/bigobj)

    if(MINGW)
        add_definitions(-DPHON_POSIX=1 -DPHON_MINGW=1)
    else()
        add_definitions(-DPHON_POSIX=0 -DPHON_MINGW=0)
    endif()
    if(MSVC)
        add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    endif()
else()
    add_definitions(-DPHON_POSIX=1 -DPHON_UNIX=1 -DPHON_WINDOWS=0 -DPHON_MINGW=0)
    if(APPLE)
        add_definitions(-DPHON_MACOS=1 -DPHON_LINUX=0)
    else()
        add_definitions(-DPHON_MACOS=0 -DPHON_LINUX=1)
    endif()
endif()


###########################
##  COMPILE DEFINITIONS  ##
###########################

if(EMBED_SCRIPTS)
    add_definitions(-DPHON_EMBED_SCRIPTS=1)
endif()

if(ENABLE_LOGGING)
    add_definitions(-DPHON_ENABLE_LOGGING=1)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DPHON_DEBUG")

if(CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()


########################
##  THIRD-PARTY LIBS  ##
########################

# utf8proc (static)
add_definitions(-DUTF8PROC_STATIC)

# PCRE2 (UTF-8, static)
add_definitions(-DPCRE2_CODE_UNIT_WIDTH=8 -DPCRE2_STATIC=1)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(phon/third_party/pcre2 EXCLUDE_FROM_ALL)
if(WIN32)
    include_directories(${PROJECT_BINARY_DIR}/phon/third_party/pcre2/interface)
else()
    include_directories(${PROJECT_BINARY_DIR}/phon/third_party/pcre2)
endif()

###########################
##  SCRIPTING ENGINE LIB ##
###########################

if(WITH_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core)
endif()

include_directories(${CMAKE_SOURCE_DIR})

file(GLOB_RECURSE RUNTIME_SOURCES
        phon/runtime/*.cpp
        phon/utils/*.cpp
)

set(THIRDPARTY_SOURCES
        phon/third_party/utf8proc/utf8proc.c
        phon/third_party/zip/zip.cpp
)

add_library(phon-runtime STATIC ${RUNTIME_SOURCES} ${THIRDPARTY_SOURCES})
set_property(TARGET phon-runtime PROPERTY POSITION_INDEPENDENT_CODE ON)
target_link_libraries(phon-runtime PRIVATE pcre2-8)

if(WITH_GUI)
    target_link_libraries(phon-runtime PUBLIC Qt6::Core)
    #add_definitions(-DPHON_WITH_QT=1)
    target_compile_definitions(phon-runtime PUBLIC PHON_WITH_QT=1)
endif()

if(WIN32)
    target_link_libraries(phon-runtime PRIVATE shlwapi)
else()
    target_link_libraries(phon-runtime PRIVATE m)
endif()

#############################
##  APPLICATION LAYER LIB  ##
#############################

if(WITH_APPLICATION)

    # ---- System libraries ----

    # libsndfile
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SNDFILE QUIET sndfile)
    endif()
    if(NOT SNDFILE_FOUND)
        find_library(SNDFILE_LIBRARIES NAMES sndfile libsndfile-1)
        find_path(SNDFILE_INCLUDE_DIRS sndfile.h)
    endif()
    if(SNDFILE_INCLUDE_DIRS)
        include_directories(${SNDFILE_INCLUDE_DIRS})
    endif()

    # ---- Vendored header-only libraries ----

    add_definitions(-DEIGEN_MPL2_ONLY)  # Eigen already present in third_party/

    include_directories(
        ${CMAKE_SOURCE_DIR}/phon/third_party            # Eigen, LBFGSpp
        ${CMAKE_SOURCE_DIR}/phon/third_party/sptk4/include
        ${CMAKE_SOURCE_DIR}/phon/third_party/world
        ${CMAKE_SOURCE_DIR}/phon/third_party/REAPER
        ${CMAKE_SOURCE_DIR}/phon/third_party/CppAD
    )

    # ---- Platform-specific audio backend ----

    if(WIN32)
        add_definitions(-D__WINDOWS_DS__=1)
        add_definitions(-DENABLE_SNDFILE_WINDOWS_PROTOTYPES=1)
    elseif(APPLE)
        add_definitions(-D__MACOSX_CORE__=1)
    else()
        add_definitions(-D__LINUX_ALSA__=1)
    endif()

    # ---- Application sources ----

    file(GLOB APP_SOURCES
        phon/application/*.cpp
        phon/application/conc/*.cpp
        phon/analysis/*.cpp
    )

    # ---- Vendored compiled sources ----

    file(GLOB VENDOR_APP_SOURCES
        phon/third_party/pugixml/pugixml.cpp
        phon/third_party/rtaudio/RtAudio.cpp
        phon/third_party/sqlite/sqlite3.c
        phon/third_party/sptk4/*.cc
        phon/third_party/REAPER/core/*.cc
        phon/third_party/REAPER/epoch_tracker/*.cc
        phon/third_party/REAPER/wave/*.cc
        phon/third_party/Snack/*.cc
        phon/third_party/swipe/*.c
        phon/third_party/swipe/*.cpp
        phon/third_party/FIR-filter-class/filt.cpp
        phon/third_party/world/*.cc
    )

    add_library(phon-app STATIC ${APP_SOURCES} ${VENDOR_APP_SOURCES})
    set_property(TARGET phon-app PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(phon-app PRIVATE phon-runtime)

    # ---- whisper.cpp (optional, for transcription) ----

    if(WITH_WHISPER)
        # Vendored at phon/third_party/whisper (whisper.cpp v1.7.4, CPU backend only).
        # Builds four static libs: ggml-base, ggml-cpu, ggml, whisper.
        add_subdirectory(phon/third_party/whisper EXCLUDE_FROM_ALL)

        target_compile_definitions(phon-app PRIVATE PHON_WITH_WHISPER=1)
        target_link_libraries(phon-app PRIVATE whisper)
    else()
        target_compile_definitions(phon-app PRIVATE PHON_WITH_WHISPER=0)
    endif()

    # ---- Platform link libraries ----

    if(WIN32)
        target_link_libraries(phon-app PRIVATE
            ${SNDFILE_LIBRARIES}
            ole32 comctl32 winmm uuid Rpcrt4 User32 dsound
        )
    elseif(APPLE)
        find_library(LIB_ICONV iconv)
        if(NOT LIB_ICONV)
            message(FATAL_ERROR "iconv not found")
        endif()
        target_link_libraries(phon-app PRIVATE
            ${SNDFILE_LIBRARIES}
            pthread dl ${LIB_ICONV}
            "-framework Foundation" "-framework CoreAudio"
        )
    else()
        target_link_libraries(phon-app PRIVATE
            ${SNDFILE_LIBRARIES}
            pthread dl asound
        )
    endif()

endif(WITH_APPLICATION)


#############################
##  GUI (Qt)               ##
#############################



if(WITH_GUI)
    add_definitions(-DPHON_GUI=1)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)

    find_package(Qt6 REQUIRED COMPONENTS Widgets Svg PrintSupport)

    # QScintilla (Qt wrapper for the Scintilla editor component).
    # Try pkg-config first, then fall back to a manual search.
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(QSCINTILLA QUIET qscintilla2-qt6)
    endif()
    if(NOT QSCINTILLA_FOUND)
        find_library(QSCINTILLA_LIBRARIES NAMES qscintilla2_qt6 qscintilla2-qt6 qscintilla2)
        find_path(QSCINTILLA_INCLUDE_DIRS Qsci/qsciscintilla.h
            PATH_SUFFIXES qt6)
        if(QSCINTILLA_LIBRARIES AND QSCINTILLA_INCLUDE_DIRS)
            set(QSCINTILLA_FOUND TRUE)
        endif()
    endif()
    if(NOT QSCINTILLA_FOUND)
        message(FATAL_ERROR "QScintilla not found. Install libqscintilla2-qt6-dev (Debian/Ubuntu), "
                            "qscintilla2 (Homebrew), or qscintilla (vcpkg).")
    endif()
    if(QSCINTILLA_INCLUDE_DIRS)
        include_directories(${QSCINTILLA_INCLUDE_DIRS})
    endif()

    file(GLOB_RECURSE GUI_SOURCES
        phon/gui/*.cpp
        phon/gui/conc/*.cpp
    )

    add_library(phon-gui STATIC ${GUI_SOURCES})

    set_property(TARGET phon-gui PROPERTY POSITION_INDEPENDENT_CODE ON)

    target_link_libraries(phon-gui PRIVATE phon-app phon-runtime Qt6::Widgets Qt6::Svg Qt6::PrintSupport ${QSCINTILLA_LIBRARIES})

    if(WIN32)
        target_compile_definitions(phon-gui PRIVATE QSCINTILLA_DLL)
    endif()

endif()


####################
##  EXECUTABLE    ##
####################

if (APPLE)
    # Mark the .icns as a resource so CMake copies it into the bundle
    set(MACOS_ICON "${CMAKE_SOURCE_DIR}/icons/phonometrica.icns")
    set_source_files_properties(${MACOS_ICON} PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources"
    )

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE phonometrica.cpp ${QRC_SOURCES} ${MACOS_ICON})
    set_target_properties(${PROJECT_NAME} PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER    "org.phonometrica-ling.phonometrica"
            MACOSX_BUNDLE_BUNDLE_NAME       "Phonometrica"
            MACOSX_BUNDLE_BUNDLE_VERSION    "${PHON_VERSION_MAJOR}.${PHON_VERSION_MINOR}.${PHON_VERSION_MICRO}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PHON_VERSION_MAJOR}.${PHON_VERSION_MINOR}.${PHON_VERSION_MICRO}"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
            MACOSX_BUNDLE_ICON_FILE "phonometrica.icns"
    )
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND /Users/julieneychenne/Qt/6.11.0/macos/bin/macdeployqt
            ${CMAKE_BINARY_DIR}/phonometrica.app
            COMMENT "Deploying Qt frameworks into bundle...")
elseif (WIN32)
    add_executable(${PROJECT_NAME} WIN32 phonometrica.cpp ${QRC_SOURCES} windows.rc)
else ()
    add_executable(${PROJECT_NAME} phonometrica.cpp ${QRC_SOURCES})
endif ()
target_link_libraries(${PROJECT_NAME} PRIVATE phon-runtime pcre2-8)

if(WITH_APPLICATION)
    target_link_libraries(${PROJECT_NAME} PRIVATE phon-app)
endif()

if(WITH_GUI)
    target_link_libraries(${PROJECT_NAME} PRIVATE phon-gui Qt6::Widgets)

    qt6_add_resources(${PROJECT_NAME} "icons" FILES
        icons/database.svg
        icons/search.svg
        icons/file-text.svg
        icons/square-terminal.svg
        icons/sheet.svg
        icons/book-marked.svg
        icons/file-search-corner.svg
        icons/file-spreadsheet.svg
        icons/file-terminal.svg
        icons/file-volume.svg
        icons/file-bookmarked.svg
        icons/layers.svg
        icons/waves.svg
        icons/play.svg
        icons/pause.svg
        icons/square.svg
        icons/arrow-left.svg
        icons/arrow-right.svg
        icons/zoom-in.svg
        icons/zoom-out.svg
        icons/minimize.svg
        icons/maximize.svg
        icons/mouse.svg
        icons/play-selection.svg
        icons/pause-selection.svg
        icons/spectrum.svg
        icons/save.svg
        icons/info.svg
        icons/binary.svg
        icons/arrow-left-to-line.svg
        icons/arrow-right-to-line.svg
        icons/toggle-left.svg
        icons/toggle-right.svg
        icons/ear.svg
        icons/pitch.svg
        icons/waveform.svg
        icons/select-window.svg
        icons/clipboard-copy.svg
        icons/eraser.svg
        icons/anchor.svg
        icons/link.svg
        icons/phonometrica.svg
        icons/circle-help.svg
        icons/circle-minus.svg
        icons/circle-plus.svg
        icons/display.svg
        icons/unlink.svg
        icons/tag.svg
        icons/trash-2.svg
        icons/pencil-line.svg
        icons/set-complement.svg
        icons/set-intersection.svg
        icons/set-union.svg
        icons/circle-x.svg
        icons/scan-eye.svg
        icons/ruler.svg
        icons/statistics.svg
        icons/tex.svg
        icons/check.svg
        icons/text-cursor.svg
        icons/merge-tables.svg
        icons/filter-x.svg
        icons/filter.svg
        icons/scissors.svg
        icons/sigma.svg
        icons/grid-2x2-x.svg
        icons/list-x.svg
        icons/concordance.svg
        icons/notebook-pen.svg
        icons/bold.svg
        icons/underline.svg
        icons/italic.svg
        icons/list-ordered.svg
        icons/list.svg
        icons/vowel-space.svg
        icons/settings.svg
    )

    qt6_add_resources(${PROJECT_NAME} "std" FILES
        std/signal.phon
        std/initialize.phon
        std/speech_analysis.phon
        std/read_settings.phon
        std/write_settings.phon
        std/transphon.phon
    )

    ###################################
    ##  BUNDLED DOCUMENTATION        ##
    ###################################

    # If PHON_BUILD_DOCS is ON and sphinx-build is available, the Sphinx html
    # builder is run at build time. The output is shipped on disk next to (or
    # inside, on macOS) the executable and opened in the user's default browser
    # by HelpBrowser::showPage(). No more rcc, no more QTextBrowser CSS-2.1
    # ceiling — the user gets the full Sphinx theme with working search and
    # sidebar navigation.

    if(PHON_BUILD_DOCS)
        find_program(SPHINX_BUILD sphinx-build)
        if(SPHINX_BUILD)
            set(DOC_HTML_DIR "${CMAKE_BINARY_DIR}/docs_html")
            set(DOC_STAMP    "${DOC_HTML_DIR}/.stamp")

            file(GLOB_RECURSE DOC_SOURCES
                "${CMAKE_SOURCE_DIR}/docs/*.rst"
                "${CMAKE_SOURCE_DIR}/docs/conf.py"
                "${CMAKE_SOURCE_DIR}/docs/phon_lexer/phon/lexer.py"
            )

            # Run Sphinx with the standard html builder so the default theme,
            # JS search index and sidebar TOC all work in a real browser.
            add_custom_command(
                OUTPUT  "${DOC_STAMP}"
                COMMAND "${SPHINX_BUILD}" -q -b html
                        "${CMAKE_SOURCE_DIR}/docs" "${DOC_HTML_DIR}"
                COMMAND ${CMAKE_COMMAND} -E touch "${DOC_STAMP}"
                DEPENDS ${DOC_SOURCES}
                COMMENT "Building documentation with Sphinx..."
            )

            add_custom_target(phon-docs ALL DEPENDS "${DOC_STAMP}")
            add_dependencies(${PROJECT_NAME} phon-docs)

            # Place the docs where HelpBrowser::resolveDocsRoot() will find
            # them when run from the build tree, and where the Inno Setup
            # deploy workflow / macOS bundle can pick them up.
            if(APPLE)
                # Inside the .app bundle: Contents/Resources/docs/
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${DOC_HTML_DIR}"
                        "$<TARGET_BUNDLE_CONTENT_DIR:${PROJECT_NAME}>/Resources/docs"
                    COMMENT "Copying documentation into .app bundle...")
            else()
                # Linux build dir / Windows build dir: <binary>/docs/
                add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${DOC_HTML_DIR}"
                        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/docs"
                    COMMENT "Copying documentation alongside the binary...")
            endif()

            # Linux install: ship docs to <prefix>/share/phonometrica/docs/
            # so HelpBrowser finds them via ../share/phonometrica/docs.
            if(UNIX AND NOT APPLE)
                include(GNUInstallDirs)
                install(DIRECTORY "${DOC_HTML_DIR}/"
                        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/phonometrica/docs"
                        PATTERN ".stamp" EXCLUDE
                        PATTERN ".buildinfo" EXCLUDE)
            endif()

            message(STATUS "Documentation will be built and bundled (sphinx-build found at ${SPHINX_BUILD})")
        else()
            message(STATUS "sphinx-build not found -- documentation will not be embedded. "
                           "Install Sphinx (pip install sphinx) or set PHON_BUILD_DOCS=OFF to silence this.")
        endif()
    endif()

endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE shlwapi)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()



####################
##  INSTALL       ##
####################

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)

    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    install(FILES ${CMAKE_SOURCE_DIR}/dist/linux/phonometrica.desktop
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

    install(FILES ${CMAKE_SOURCE_DIR}/dist/linux/org.phonometrica.Phonometrica.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)

    install(FILES ${CMAKE_SOURCE_DIR}/icons/phonometrica.svg
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
            RENAME org.phonometrica.Phonometrica.svg)
endif()



####################
##  UNIT TESTS    ##
####################

if(BUILD_UNIT_TEST)
    file(GLOB TEST_SOURCES unit_test/*.cpp)
    add_executable(test_phon ${TEST_SOURCES})
    target_link_libraries(test_phon PRIVATE phon-runtime pcre2-8)

    if(WITH_APPLICATION)
        target_link_libraries(test_phon PRIVATE phon-app)
    endif()
endif()
