# Dependencies.cmake
# Uses FetchContent to download and build third-party libraries automatically

include(FetchContent)

# Configure FetchContent
set(FETCHCONTENT_QUIET OFF)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

message(STATUS "Fetching dependencies...")

# ============================================================================
# GLFW - Window management and OpenGL context
# ============================================================================
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.9
    GIT_SHALLOW    TRUE
)

# ============================================================================
# ImGui - Immediate mode GUI
# ============================================================================
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.90.1-docking
    GIT_SHALLOW    TRUE
)

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

# Fetch GLFW
FetchContent_GetProperties(glfw)
if(NOT glfw_POPULATED)
    message(STATUS "Fetching GLFW...")
    FetchContent_Populate(glfw)
    add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR})
endif()

# Fetch ImGui (needs manual setup as it's not CMake based)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    message(STATUS "Fetching ImGui...")
    FetchContent_Populate(imgui)

    # Create ImGui library manually
    set(IMGUI_DIR ${imgui_SOURCE_DIR})
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
    )
    target_link_libraries(imgui PUBLIC glfw)

    # Platform-specific OpenGL linking
    if(WIN32)
        target_link_libraries(imgui PUBLIC opengl32)
    elseif(UNIX AND NOT APPLE)
        find_package(OpenGL REQUIRED)
        target_link_libraries(imgui PUBLIC OpenGL::GL)
    elseif(APPLE)
        find_library(OPENGL_LIBRARY OpenGL)
        target_link_libraries(imgui PUBLIC ${OPENGL_LIBRARY})
    endif()

    target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
endif()

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
# stb (header-only image loading library)
# ============================================================================
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    message(STATUS "Fetching stb...")
    FetchContent_Populate(stb)
endif()

set(STB_INCLUDE_DIR ${stb_SOURCE_DIR})

message(STATUS "All dependencies fetched successfully")
