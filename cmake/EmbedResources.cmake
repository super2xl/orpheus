# EmbedResources.cmake
# Converts binary files (DLLs, etc.) to C++ byte arrays at build time
# This allows embedding all dependencies into a single executable

# Function to embed a single resource file as a C++ header
function(embed_resource resource_file output_file)
    # Read file as hex
    file(READ "${resource_file}" filedata HEX)

    # Convert hex to C array format: "00" -> "0x00,"
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," filedata "${filedata}")

    # Get filename for variable naming
    get_filename_component(filename "${resource_file}" NAME)
    # Replace dots and dashes with underscores for valid C++ identifier
    string(REPLACE "." "_" varname "${filename}")
    string(REPLACE "-" "_" varname "${varname}")
    string(TOLOWER "${varname}" varname)

    # Get file size
    file(SIZE "${resource_file}" filesize)

    # Generate the header file
    file(WRITE "${output_file}"
        "// Auto-generated file - DO NOT EDIT\n"
        "// Embedded resource: ${filename}\n"
        "// Size: ${filesize} bytes\n"
        "#pragma once\n\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n\n"
        "namespace orpheus::embedded {\n\n"
        "inline constexpr unsigned char ${varname}[] = {\n"
        "    ${filedata}\n"
        "};\n\n"
        "inline constexpr size_t ${varname}_size = ${filesize};\n\n"
        "} // namespace orpheus::embedded\n"
    )

    message(STATUS "Embedded resource: ${filename} (${filesize} bytes)")
endfunction()

# Function to embed all DLLs in a directory
function(embed_all_dlls)
    set(DLL_DIR "${CMAKE_SOURCE_DIR}/resources/dlls")
    set(FONT_DIR "${CMAKE_SOURCE_DIR}/resources/fonts")
    set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated")

    # Create output directory
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    # List of DLLs to embed
    set(EMBEDDED_DLLS
        "${DLL_DIR}/vmm.dll"
        "${DLL_DIR}/leechcore.dll"
        "${DLL_DIR}/FTD3XX.dll"
        "${DLL_DIR}/dbghelp.dll"
        "${DLL_DIR}/symsrv.dll"
        "${DLL_DIR}/tinylz4.dll"
        "${DLL_DIR}/vcruntime140.dll"
    )

    # List of fonts to embed
    set(EMBEDDED_FONTS
        "${FONT_DIR}/JetBrainsMono-Regular.ttf"
        "${FONT_DIR}/JetBrainsMono-Bold.ttf"
        "${FONT_DIR}/JetBrainsMono-Medium.ttf"
    )

    # Process each DLL
    foreach(dll_file ${EMBEDDED_DLLS})
        if(EXISTS "${dll_file}")
            get_filename_component(dll_name "${dll_file}" NAME_WE)
            string(TOLOWER "${dll_name}" dll_name_lower)
            set(output_file "${OUTPUT_DIR}/embedded_${dll_name_lower}.h")
            embed_resource("${dll_file}" "${output_file}")
            list(APPEND EMBEDDED_HEADERS "${output_file}")
        else()
            message(WARNING "DLL not found: ${dll_file}")
        endif()
    endforeach()

    # Process each font
    foreach(font_file ${EMBEDDED_FONTS})
        if(EXISTS "${font_file}")
            get_filename_component(font_name "${font_file}" NAME_WE)
            string(TOLOWER "${font_name}" font_name_lower)
            string(REPLACE "-" "_" font_name_lower "${font_name_lower}")
            set(output_file "${OUTPUT_DIR}/embedded_${font_name_lower}.h")
            embed_resource("${font_file}" "${output_file}")
            list(APPEND EMBEDDED_HEADERS "${output_file}")
        else()
            message(WARNING "Font not found: ${font_file}")
        endif()
    endforeach()

    # Process icon (optional)
    set(ICON_DIR "${CMAKE_SOURCE_DIR}/resources/icons")
    set(ICON_FILE "${ICON_DIR}/orpheus.png")
    set(HAS_ICON FALSE)
    if(EXISTS "${ICON_FILE}")
        set(output_file "${OUTPUT_DIR}/embedded_orpheus_icon.h")
        embed_resource("${ICON_FILE}" "${output_file}")
        list(APPEND EMBEDDED_HEADERS "${output_file}")
        set(HAS_ICON TRUE)
        message(STATUS "Embedded icon: orpheus.png")
    else()
        message(STATUS "Icon not found (optional): ${ICON_FILE}")
    endif()

    # Generate a master header that includes all embedded resources
    set(MASTER_HEADER "${OUTPUT_DIR}/embedded_resources.h")
    file(WRITE "${MASTER_HEADER}"
        "// Auto-generated master header for all embedded resources\n"
        "#pragma once\n\n"
    )

    foreach(header ${EMBEDDED_HEADERS})
        get_filename_component(header_name "${header}" NAME)
        file(APPEND "${MASTER_HEADER}"
            "#include \"${header_name}\"\n"
        )
    endforeach()

    # Add a resource registry for runtime lookup
    file(APPEND "${MASTER_HEADER}"
        "\n#include <string_view>\n"
        "#include <utility>\n"
        "#include <array>\n\n"
        "namespace orpheus::embedded {\n\n"
        "struct ResourceInfo {\n"
        "    std::string_view name;\n"
        "    const unsigned char* data;\n"
        "    size_t size;\n"
        "};\n\n"
        "inline constexpr std::array<ResourceInfo, 7> dll_resources = {{\n"
        "    {\"vmm.dll\", vmm_dll, vmm_dll_size},\n"
        "    {\"leechcore.dll\", leechcore_dll, leechcore_dll_size},\n"
        "    {\"FTD3XX.dll\", ftd3xx_dll, ftd3xx_dll_size},\n"
        "    {\"dbghelp.dll\", dbghelp_dll, dbghelp_dll_size},\n"
        "    {\"symsrv.dll\", symsrv_dll, symsrv_dll_size},\n"
        "    {\"tinylz4.dll\", tinylz4_dll, tinylz4_dll_size},\n"
        "    {\"vcruntime140.dll\", vcruntime140_dll, vcruntime140_dll_size},\n"
        "}};\n\n"
        "// Font resources\n"
        "inline constexpr std::array<ResourceInfo, 3> font_resources = {{\n"
        "    {\"JetBrainsMono-Regular.ttf\", jetbrainsmono_regular_ttf, jetbrainsmono_regular_ttf_size},\n"
        "    {\"JetBrainsMono-Bold.ttf\", jetbrainsmono_bold_ttf, jetbrainsmono_bold_ttf_size},\n"
        "    {\"JetBrainsMono-Medium.ttf\", jetbrainsmono_medium_ttf, jetbrainsmono_medium_ttf_size},\n"
        "}};\n\n"
        "// Legacy alias for backward compatibility\n"
        "inline constexpr auto& resources = dll_resources;\n\n"
        "// Icon resource (optional)\n"
    )

    if(HAS_ICON)
        file(APPEND "${MASTER_HEADER}"
            "inline constexpr bool has_icon = true;\n"
            "// orpheus_png and orpheus_png_size defined in embedded_orpheus_icon.h\n"
        )
    else()
        file(APPEND "${MASTER_HEADER}"
            "inline constexpr bool has_icon = false;\n"
            "inline constexpr unsigned char orpheus_png[] = {0};  // Placeholder byte\n"
            "inline constexpr size_t orpheus_png_size = 0;\n"
        )
    endif()

    file(APPEND "${MASTER_HEADER}"
        "\n} // namespace orpheus::embedded\n"
    )

    message(STATUS "Generated master header: ${MASTER_HEADER}")

    # Set parent scope variable for the generated headers
    set(EMBEDDED_RESOURCE_HEADERS ${EMBEDDED_HEADERS} PARENT_SCOPE)
    set(EMBEDDED_RESOURCE_INCLUDE_DIR "${OUTPUT_DIR}" PARENT_SCOPE)
endfunction()

# Alternative: Generate resources at configure time with a separate tool
# This is more efficient for large files
function(embed_resources_with_tool)
    # Find or build the resource compiler tool
    find_program(RESOURCE_COMPILER rc_embed HINTS "${CMAKE_BINARY_DIR}/tools")

    if(NOT RESOURCE_COMPILER)
        message(STATUS "Resource compiler not found, using CMake fallback")
        embed_all_dlls()
    else()
        # Use external tool for better performance
        # TODO: Implement external resource compiler
    endif()
endfunction()
