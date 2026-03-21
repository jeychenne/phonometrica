###################################
##  CMAKE FILE FOR PHONOMETRICA  ##
###################################

cmake_minimum_required(VERSION 3.20)
project(phonometrica LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


###########################
##  COMPILATION OPTIONS  ##
###########################

option(EMBED_SCRIPTS      "Embed scripts as strings"            OFF)
option(ENABLE_LOGGING     "Enable logging (always on in debug)" OFF)
option(WITH_APPLICATION   "Build the application layer"         ON)
option(BUILD_UNIT_TEST    "Build unit tests"                    OFF)


###########################
##  VERSION INFORMATION  ##
###########################

set(PHON_VERSION_MAJOR 0)
set(PHON_VERSION_MINOR 9)
set(PHON_VERSION_MICRO 0)
set(PHON_VERSION_DEVEL 0)

set(PHON_RELEASE_DATE_YEAR  2026)
set(PHON_RELEASE_DATE_MONTH 3)
set(PHON_RELEASE_DATE_DAY   20)

add_definitions(
    -DPHON_VERSION_MAJOR=${PHON_VERSION_MAJOR}
    -DPHON_VERSION_MINOR=${PHON_VERSION_MINOR}
    -DPHON_VERSION_MICRO=${PHON_VERSION_MICRO}
    -DPHON_VERSION_DEVEL=${PHON_VERSION_DEVEL}
    -DPHON_RELEASE_DATE_YEAR=${PHON_RELEASE_DATE_YEAR}
    -DPHON_RELEASE_DATE_MONTH=${PHON_RELEASE_DATE_MONTH}
    -DPHON_RELEASE_DATE_DAY=${PHON_RELEASE_DATE_DAY}
)


########################
##  OPERATING SYSTEM  ##
########################

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE -D_USE_MATH_DEFINES)
    add_definitions(-DPHON_WINDOWS=1 -DPHON_MACOS=0 -DPHON_LINUX=0 -DPHON_UNIX=0)
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
add_subdirectory(phon/third_party/pcre2)
include_directories(${PROJECT_BINARY_DIR}/phon/third_party/pcre2)


###########################
##  SCRIPTING ENGINE LIB ##
###########################

include_directories(${CMAKE_SOURCE_DIR})

file(GLOB_RECURSE RUNTIME_SOURCES
    phon/runtime/*.cpp
    phon/utils/*.cpp
)

set(THIRDPARTY_SOURCES
    phon/third_party/utf8proc/utf8proc.c
)

add_library(phon-runtime STATIC ${RUNTIME_SOURCES} ${THIRDPARTY_SOURCES})
set_property(TARGET phon-runtime PROPERTY POSITION_INDEPENDENT_CODE ON)

target_link_libraries(phon-runtime PRIVATE pcre2-8)

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

    # FFTW3
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FFTW3 QUIET fftw3)
    endif()
    if(NOT FFTW3_FOUND)
        find_library(FFTW3_LIBRARIES NAMES fftw3)
        find_path(FFTW3_INCLUDE_DIRS fftw3.h)
    endif()
    if(FFTW3_INCLUDE_DIRS)
        include_directories(${FFTW3_INCLUDE_DIRS})
    endif()

    add_definitions(-DPHON_USE_FFTW=1)

    # ---- Vendored header-only libraries ----

    add_definitions(-DEIGEN_MPL2_ONLY)  # Eigen already present in third_party/

    include_directories(
        ${CMAKE_SOURCE_DIR}/phon/third_party            # Eigen, LBFGSpp
        ${CMAKE_SOURCE_DIR}/phon/third_party/sptk4/include
        ${CMAKE_SOURCE_DIR}/phon/third_party/world
        ${CMAKE_SOURCE_DIR}/phon/third_party/REAPER
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
        phon/third_party/r8brain/*.cpp
        phon/third_party/sptk4/*.cc
        phon/third_party/REAPER/core/*.cc
        phon/third_party/REAPER/epoch_tracker/*.cc
        phon/third_party/REAPER/wave/*.cc
        phon/third_party/Snack/*.cc
        phon/third_party/swipe/*.c
        phon/third_party/swipe/*.cpp
        phon/third_party/FIR-filter-class/filt.cpp
        phon/third_party/world/*.cc
        phon/third_party/zip/zip.cpp
    )

    add_library(phon-app STATIC ${APP_SOURCES} ${VENDOR_APP_SOURCES})
    set_property(TARGET phon-app PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(phon-app PRIVATE phon-runtime)

    # ---- Platform link libraries ----

    if(WIN32)
        target_link_libraries(phon-app PRIVATE
            ${SNDFILE_LIBRARIES}
            ${FFTW3_LIBRARIES}
            ole32 comctl32 winmm uuid Rpcrt4 User32 dsound
        )
    elseif(APPLE)
        find_library(LIB_ICONV iconv)
        if(NOT LIB_ICONV)
            message(FATAL_ERROR "iconv not found")
        endif()
        target_link_libraries(phon-app PRIVATE
            ${SNDFILE_LIBRARIES}
            ${FFTW3_LIBRARIES}
            pthread dl ${LIB_ICONV}
            "-framework Foundation" "-framework CoreAudio"
        )
    else()
        target_link_libraries(phon-app PRIVATE
            ${SNDFILE_LIBRARIES}
            ${FFTW3_LIBRARIES}
            pthread dl asound
        )
    endif()

endif(WITH_APPLICATION)


####################
##  EXECUTABLE    ##
####################

add_executable(${PROJECT_NAME} phonometrica.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE phon-runtime pcre2-8)

if(WITH_APPLICATION)
    target_link_libraries(${PROJECT_NAME} PRIVATE phon-app)
endif()

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE shlwapi)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
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
