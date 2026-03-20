###################################
##  CMAKE FILE FOR PHONOMETRICA  ##
###################################

cmake_minimum_required(VERSION 3.17)
project(phonometrica LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


###########################
##  COMPILATION OPTIONS  ##
###########################

option(EMBED_SCRIPTS   "Embed scripts as strings"         OFF)
option(ENABLE_LOGGING  "Enable logging (always on in debug)" OFF)
option(BUILD_UNIT_TEST "Build unit tests"                  OFF)


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


####################
##  EXECUTABLE    ##
####################

add_executable(${PROJECT_NAME} phonometrica.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE phon-runtime pcre2-8)

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
endif()
