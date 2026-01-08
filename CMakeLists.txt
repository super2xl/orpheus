cmake_minimum_required(VERSION 3.20)
project(Orpheus VERSION 1.0.0 LANGUAGES CXX)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Platform detection
if(WIN32)
    set(PLATFORM_WINDOWS TRUE)
    add_definitions(-DPLATFORM_WINDOWS)
    add_definitions(-DNOMINMAX)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
elseif(UNIX AND NOT APPLE)
    set(PLATFORM_LINUX TRUE)
    add_definitions(-DPLATFORM_LINUX)
elseif(APPLE)
    set(PLATFORM_MACOS TRUE)
    add_definitions(-DPLATFORM_MACOS)
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include custom CMake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(EmbedResources)

# Embed all DLLs at configure time
message(STATUS "Embedding DLL resources...")
embed_all_dlls()

# Add generated headers to include path
include_directories(${EMBEDDED_RESOURCE_INCLUDE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/src)

# ============================================================================
# Third-party Dependencies (fetched automatically)
# ============================================================================
include(Dependencies)

# ============================================================================
# Main Executable
# ============================================================================

# Collect source files (exclude installer directory)
file(GLOB_RECURSE ORPHEUS_SOURCES
    "src/*.cpp"
    "src/**/*.cpp"
)
# Remove installer sources from main executable
list(FILTER ORPHEUS_SOURCES EXCLUDE REGEX ".*installer.*")

# Windows resource file (for .exe icon)
if(WIN32)
    set(ORPHEUS_RESOURCES "${CMAKE_SOURCE_DIR}/resources/orpheus.rc")
endif()

# Create executable
add_executable(orpheus ${ORPHEUS_SOURCES} ${ORPHEUS_RESOURCES})

# Include directories
target_include_directories(orpheus PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${EMBEDDED_RESOURCE_INCLUDE_DIR}
    ${HTTPLIB_INCLUDE_DIR}
    ${JSON_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(orpheus PRIVATE
    imgui
    glfw
    spdlog::spdlog
    Zydis
    unicorn
)

# Platform-specific configuration
if(PLATFORM_WINDOWS)
    # Link Windows libraries
    target_link_libraries(orpheus PRIVATE
        opengl32
        dwmapi
    )

    # Set Windows subsystem (GUI app, no console window)
    set_target_properties(orpheus PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    # MSVC-specific flags
    if(MSVC)
        target_compile_options(orpheus PRIVATE
            /W4             # Warning level 4
            /MP             # Multi-processor compilation
            /utf-8          # UTF-8 source and execution charset
        )
        # Use main() as entry point for Windows GUI app
        # Set UAC elevation to require administrator
        target_link_options(orpheus PRIVATE
            /ENTRY:mainCRTStartup
            /MANIFESTUAC:level='requireAdministrator'
        )
        # Disable some noisy warnings
        target_compile_definitions(orpheus PRIVATE
            _CRT_SECURE_NO_WARNINGS
        )
    endif()
elseif(PLATFORM_LINUX)
    # Link Linux libraries
    target_link_libraries(orpheus PRIVATE
        dl              # For dlopen
        pthread
        GL
    )

    # GCC/Clang flags
    target_compile_options(orpheus PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# ============================================================================
# MCPinstaller - Standalone MCP Bridge Installer
# ============================================================================

set(MCPINSTALLER_SOURCES
    "src/installer/installer_main.cpp"
    "src/installer/installer_app.cpp"
)

# Windows resource file (for .exe icon)
if(WIN32)
    set(MCPINSTALLER_RESOURCES "${CMAKE_SOURCE_DIR}/resources/orpheus.rc")
endif()

add_executable(MCPinstaller ${MCPINSTALLER_SOURCES} ${MCPINSTALLER_RESOURCES})

target_include_directories(MCPinstaller PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${EMBEDDED_RESOURCE_INCLUDE_DIR}
    ${JSON_INCLUDE_DIR}
    ${STB_INCLUDE_DIR}
)

target_link_libraries(MCPinstaller PRIVATE
    imgui
    glfw
)

if(PLATFORM_WINDOWS)
    target_link_libraries(MCPinstaller PRIVATE
        opengl32
    )

    set_target_properties(MCPinstaller PROPERTIES
        WIN32_EXECUTABLE TRUE
    )

    if(MSVC)
        target_compile_options(MCPinstaller PRIVATE
            /W4 /MP /utf-8
        )
        target_link_options(MCPinstaller PRIVATE
            /ENTRY:mainCRTStartup
            /MANIFESTUAC:level='requireAdministrator'
        )
        target_compile_definitions(MCPinstaller PRIVATE
            _CRT_SECURE_NO_WARNINGS
        )
    endif()
elseif(PLATFORM_LINUX)
    target_link_libraries(MCPinstaller PRIVATE
        dl pthread GL
    )
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS orpheus MCPinstaller
    RUNTIME DESTINATION bin
)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "=== Orpheus Build Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Embedded resources: ${EMBEDDED_RESOURCE_INCLUDE_DIR}")
message(STATUS "")
