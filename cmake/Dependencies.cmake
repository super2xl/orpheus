# Dependencies.cmake
# Uses FetchContent to download and build third-party libraries automatically

include(FetchContent)

# Configure FetchContent
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

message(STATUS "Fetching dependencies...")

# ============================================================================
# spdlog - Fast logging library
# ============================================================================
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.13.0
    GIT_SHALLOW    TRUE
)

# ============================================================================
# Zydis - x86/x64 disassembler
# ============================================================================
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    GIT_TAG        v4.0.0
    GIT_SHALLOW    TRUE
)

# ============================================================================
# Make dependencies available
# ============================================================================

# Fetch spdlog
FetchContent_GetProperties(spdlog)
if(NOT spdlog_POPULATED)
    message(STATUS "Fetching spdlog...")
    FetchContent_Populate(spdlog)
    add_subdirectory(${spdlog_SOURCE_DIR} ${spdlog_BINARY_DIR})
endif()

# Fetch Zydis
FetchContent_GetProperties(zydis)
if(NOT zydis_POPULATED)
    message(STATUS "Fetching Zydis...")
    FetchContent_Populate(zydis)
    add_subdirectory(${zydis_SOURCE_DIR} ${zydis_BINARY_DIR})
endif()

# ============================================================================
# Unicorn Engine - CPU Emulator
# ============================================================================
set(UNICORN_ARCH "x86" CACHE STRING "" FORCE)  # Only x86/x64
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(UNICORN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UNICORN_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    unicorn
    GIT_REPOSITORY https://github.com/unicorn-engine/unicorn.git
    GIT_TAG        2.1.4
    GIT_SHALLOW    TRUE
)

FetchContent_GetProperties(unicorn)
if(NOT unicorn_POPULATED)
    message(STATUS "Fetching Unicorn Engine...")
    FetchContent_Populate(unicorn)
    add_subdirectory(${unicorn_SOURCE_DIR} ${unicorn_BINARY_DIR})
endif()

# ============================================================================
# cpp-httplib (header-only HTTP server library)
# ============================================================================
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)

FetchContent_GetProperties(httplib)
if(NOT httplib_POPULATED)
    message(STATUS "Fetching cpp-httplib...")
    FetchContent_Populate(httplib)
endif()

# Header-only library, just need to add include directory
set(HTTPLIB_INCLUDE_DIR ${httplib_SOURCE_DIR})

# ============================================================================
# nlohmann/json (header-only JSON library)
# ============================================================================
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_GetProperties(json)
if(NOT json_POPULATED)
    message(STATUS "Fetching nlohmann/json...")
    FetchContent_Populate(json)
endif()

set(JSON_INCLUDE_DIR ${json_SOURCE_DIR}/include)

# ============================================================================
# GoogleTest (for unit tests only)
# ============================================================================
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_GetProperties(googletest)
if(NOT googletest_POPULATED)
    message(STATUS "Fetching GoogleTest...")
    FetchContent_Populate(googletest)
    add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
endif()

message(STATUS "All dependencies fetched successfully")
