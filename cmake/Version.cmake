# Version.cmake - Generate version information header
#
# This module generates a version.h file with:
# - Semantic version (MAJOR.MINOR.PATCH)
# - Git commit hash
# - Build timestamp
# - Build type (Debug/Release)

# Get git information
find_package(Git QUIET)

set(ORPHEUS_GIT_HASH "unknown")
set(ORPHEUS_GIT_HASH_SHORT "unknown")
set(ORPHEUS_GIT_BRANCH "unknown")
set(ORPHEUS_GIT_DIRTY "false")

if(GIT_FOUND)
    # Get full commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ORPHEUS_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Get short commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ORPHEUS_GIT_HASH_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Get current branch
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE ORPHEUS_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    # Check if working tree is dirty
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_STATUS_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT "${GIT_STATUS_OUTPUT}" STREQUAL "")
        set(ORPHEUS_GIT_DIRTY "true")
    endif()
endif()

# Build timestamp (ISO 8601 format)
string(TIMESTAMP ORPHEUS_BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
string(TIMESTAMP ORPHEUS_BUILD_DATE "%Y-%m-%d" UTC)

# Version string with optional dirty marker
set(ORPHEUS_VERSION_FULL "${PROJECT_VERSION}")
if(ORPHEUS_GIT_DIRTY STREQUAL "true")
    set(ORPHEUS_VERSION_FULL "${ORPHEUS_VERSION_FULL}-dirty")
endif()

# Configure version header
set(VERSION_HEADER_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${VERSION_HEADER_DIR})

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/version.h.in"
    "${VERSION_HEADER_DIR}/version.h"
    @ONLY
)

# Add to include path
include_directories(${VERSION_HEADER_DIR})

# Log version info
message(STATUS "")
message(STATUS "=== Version Information ===")
message(STATUS "Version: ${ORPHEUS_VERSION_FULL}")
message(STATUS "Git hash: ${ORPHEUS_GIT_HASH_SHORT}")
message(STATUS "Git branch: ${ORPHEUS_GIT_BRANCH}")
message(STATUS "Build date: ${ORPHEUS_BUILD_DATE}")
if(ORPHEUS_GIT_DIRTY STREQUAL "true")
    message(STATUS "WARNING: Working tree has uncommitted changes")
endif()
