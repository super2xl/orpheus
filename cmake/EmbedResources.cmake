# EmbedResources.cmake
# Converts binary files (DLLs/SOs, etc.) to C++ byte arrays at build time
# This allows embedding all dependencies into a single executable
# Supports both Windows (.dll) and Linux (.so) platforms

# Function to embed a single resource file as a C++ header
# Optional third argument: variable name prefix (e.g., "map_de_dust2_")
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

    # Apply optional prefix (passed as third argument)
    if(ARGC GREATER 2)
        set(varname "${ARGV2}${varname}")
    endif()

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

# Function to embed all platform libraries
function(embed_all_dlls)
    set(FONT_DIR "${CMAKE_SOURCE_DIR}/resources/fonts")
    set(SLEIGH_DIR "${CMAKE_SOURCE_DIR}/resources/sleigh/x86")
    set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated")

    # Create output directory
    file(MAKE_DIRECTORY "${OUTPUT_DIR}")

    # Platform-specific library configuration
    if(WIN32)
        set(LIB_DIR "${CMAKE_SOURCE_DIR}/resources/dlls")
        set(LIB_EXT "dll")
        set(EMBEDDED_LIBS
            "${LIB_DIR}/vmm.dll"
            "${LIB_DIR}/leechcore.dll"
            "${LIB_DIR}/FTD3XX.dll"
            "${LIB_DIR}/dbghelp.dll"
            "${LIB_DIR}/symsrv.dll"
            "${LIB_DIR}/tinylz4.dll"
            "${LIB_DIR}/vcruntime140.dll"
        )
        set(LIB_COUNT 7)
    else()
        set(LIB_DIR "${CMAKE_SOURCE_DIR}/resources/sos")
        set(LIB_EXT "so")
        set(EMBEDDED_LIBS
            "${LIB_DIR}/vmm.so"
            "${LIB_DIR}/leechcore.so"
            "${LIB_DIR}/leechcore_ft601_driver_linux.so"
            "${LIB_DIR}/libpdbcrust.so"
        )
        set(LIB_COUNT 4)
    endif()

    message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}, embedding ${LIB_COUNT} libraries from ${LIB_DIR}")

    # List of fonts to embed
    set(EMBEDDED_FONTS
        "${FONT_DIR}/JetBrainsMono-Regular.ttf"
        "${FONT_DIR}/JetBrainsMono-Bold.ttf"
        "${FONT_DIR}/JetBrainsMono-Medium.ttf"
    )

    # Icon font (FontAwesome 6 Free Solid) - optional
    set(ICON_FONT_FILE "${FONT_DIR}/fa-solid-900.ttf")
    set(HAS_ICON_FONT FALSE)
    if(EXISTS "${ICON_FONT_FILE}")
        set(output_file "${OUTPUT_DIR}/embedded_fa_solid_900.h")
        embed_resource("${ICON_FONT_FILE}" "${output_file}")
        list(APPEND EMBEDDED_HEADERS "${output_file}")
        set(HAS_ICON_FONT TRUE)
        message(STATUS "Embedded icon font: fa-solid-900.ttf")
    else()
        message(STATUS "Icon font not found (optional): ${ICON_FONT_FILE}")
        message(STATUS "  Download FontAwesome 6 Free from https://fontawesome.com/download")
        message(STATUS "  Place fa-solid-900.ttf in resources/fonts/")
    endif()

    # SLEIGH processor specification files for Ghidra decompiler
    set(EMBEDDED_SLEIGH
        "${SLEIGH_DIR}/x86.ldefs"
        "${SLEIGH_DIR}/x86.pspec"
        "${SLEIGH_DIR}/x86-64.sla"
        "${SLEIGH_DIR}/x86-64.pspec"
        "${SLEIGH_DIR}/x86-64-win.cspec"
    )

    # Process each library
    foreach(lib_file ${EMBEDDED_LIBS})
        if(EXISTS "${lib_file}")
            get_filename_component(lib_name "${lib_file}" NAME_WE)
            string(TOLOWER "${lib_name}" lib_name_lower)
            set(output_file "${OUTPUT_DIR}/embedded_${lib_name_lower}.h")
            embed_resource("${lib_file}" "${output_file}")
            list(APPEND EMBEDDED_HEADERS "${output_file}")
        else()
            message(WARNING "Library not found: ${lib_file}")
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

    # Process SLEIGH spec files
    set(HAS_SLEIGH FALSE)
    foreach(sleigh_file ${EMBEDDED_SLEIGH})
        if(EXISTS "${sleigh_file}")
            get_filename_component(sleigh_name "${sleigh_file}" NAME)
            string(TOLOWER "${sleigh_name}" sleigh_name_lower)
            string(REPLACE "." "_" sleigh_name_lower "${sleigh_name_lower}")
            string(REPLACE "-" "_" sleigh_name_lower "${sleigh_name_lower}")
            set(output_file "${OUTPUT_DIR}/embedded_sleigh_${sleigh_name_lower}.h")
            embed_resource("${sleigh_file}" "${output_file}")
            list(APPEND EMBEDDED_HEADERS "${output_file}")
            set(HAS_SLEIGH TRUE)
        else()
            message(WARNING "SLEIGH file not found: ${sleigh_file}")
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

    # Process MCP bridge script
    set(MCP_BRIDGE_FILE "${CMAKE_SOURCE_DIR}/mcp_bridge.js")
    set(HAS_MCP_BRIDGE FALSE)
    if(EXISTS "${MCP_BRIDGE_FILE}")
        set(output_file "${OUTPUT_DIR}/embedded_mcp_bridge.h")
        embed_resource("${MCP_BRIDGE_FILE}" "${output_file}")
        list(APPEND EMBEDDED_HEADERS "${output_file}")
        set(HAS_MCP_BRIDGE TRUE)
        message(STATUS "Embedded MCP bridge: mcp_bridge.js")
    else()
        message(WARNING "MCP bridge not found: ${MCP_BRIDGE_FILE}")
    endif()

    # Process CS2 map resources (radar images and info files)
    set(MAPS_DIR "${CMAKE_SOURCE_DIR}/resources/maps")
    set(CS2_MAPS
        "de_dust2"
        "de_mirage"
        "de_inferno"
        "de_nuke"
        "de_overpass"
        "de_ancient"
        "de_anubis"
        "de_vertigo"
        "cs_office"
        "ar_shoots"
    )
    set(HAS_MAPS FALSE)
    set(MAP_COUNT 0)
    foreach(map_name ${CS2_MAPS})
        set(MAP_DIR "${MAPS_DIR}/${map_name}")
        set(RADAR_FILE "${MAP_DIR}/radar.png")
        set(INFO_FILE "${MAP_DIR}/info.txt")

        if(EXISTS "${RADAR_FILE}" AND EXISTS "${INFO_FILE}")
            # Generate safe variable prefix (replace - with _ for valid C++ identifier)
            string(REPLACE "-" "_" safe_map_name "${map_name}")
            set(var_prefix "map_${safe_map_name}_")

            # Embed radar image with unique variable prefix
            set(output_file "${OUTPUT_DIR}/embedded_map_${map_name}_radar.h")
            embed_resource("${RADAR_FILE}" "${output_file}" "${var_prefix}")
            list(APPEND EMBEDDED_HEADERS "${output_file}")

            # Embed info.txt with unique variable prefix
            set(output_file "${OUTPUT_DIR}/embedded_map_${map_name}_info.h")
            embed_resource("${INFO_FILE}" "${output_file}" "${var_prefix}")
            list(APPEND EMBEDDED_HEADERS "${output_file}")

            set(HAS_MAPS TRUE)
            math(EXPR MAP_COUNT "${MAP_COUNT} + 1")
            message(STATUS "Embedded map: ${map_name}")
        else()
            message(STATUS "Map not found (optional): ${map_name}")
        endif()
    endforeach()
    if(HAS_MAPS)
        message(STATUS "Embedded ${MAP_COUNT} CS2 maps")
    endif()

    # Generate a master header that includes all embedded resources
    set(MASTER_HEADER "${OUTPUT_DIR}/embedded_resources.h")
    file(WRITE "${MASTER_HEADER}"
        "// Auto-generated master header for all embedded resources\n"
        "// Platform: ${CMAKE_SYSTEM_NAME}\n"
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
    )

    # Platform-specific library resources
    if(WIN32)
        file(APPEND "${MASTER_HEADER}"
            "// Windows DLL resources\n"
            "inline constexpr std::array<ResourceInfo, 7> lib_resources = {{\n"
            "    {\"vmm.dll\", vmm_dll, vmm_dll_size},\n"
            "    {\"leechcore.dll\", leechcore_dll, leechcore_dll_size},\n"
            "    {\"FTD3XX.dll\", ftd3xx_dll, ftd3xx_dll_size},\n"
            "    {\"dbghelp.dll\", dbghelp_dll, dbghelp_dll_size},\n"
            "    {\"symsrv.dll\", symsrv_dll, symsrv_dll_size},\n"
            "    {\"tinylz4.dll\", tinylz4_dll, tinylz4_dll_size},\n"
            "    {\"vcruntime140.dll\", vcruntime140_dll, vcruntime140_dll_size},\n"
            "}};\n\n"
            "// Legacy aliases for backward compatibility\n"
            "inline constexpr auto& dll_resources = lib_resources;\n"
            "inline constexpr auto& resources = lib_resources;\n\n"
        )
    else()
        file(APPEND "${MASTER_HEADER}"
            "// Linux shared object resources\n"
            "inline constexpr std::array<ResourceInfo, 4> lib_resources = {{\n"
            "    {\"vmm.so\", vmm_so, vmm_so_size},\n"
            "    {\"leechcore.so\", leechcore_so, leechcore_so_size},\n"
            "    {\"leechcore_ft601_driver_linux.so\", leechcore_ft601_driver_linux_so, leechcore_ft601_driver_linux_so_size},\n"
            "    {\"libpdbcrust.so\", libpdbcrust_so, libpdbcrust_so_size},\n"
            "}};\n\n"
            "// Legacy aliases for backward compatibility\n"
            "inline constexpr auto& dll_resources = lib_resources;\n"
            "inline constexpr auto& resources = lib_resources;\n\n"
        )
    endif()

    file(APPEND "${MASTER_HEADER}"
        "// Font resources\n"
        "inline constexpr std::array<ResourceInfo, 3> font_resources = {{\n"
        "    {\"JetBrainsMono-Regular.ttf\", jetbrainsmono_regular_ttf, jetbrainsmono_regular_ttf_size},\n"
        "    {\"JetBrainsMono-Bold.ttf\", jetbrainsmono_bold_ttf, jetbrainsmono_bold_ttf_size},\n"
        "    {\"JetBrainsMono-Medium.ttf\", jetbrainsmono_medium_ttf, jetbrainsmono_medium_ttf_size},\n"
        "}};\n\n"
    )

    # Icon font availability
    if(HAS_ICON_FONT)
        file(APPEND "${MASTER_HEADER}"
            "// Icon font (FontAwesome 6 Free Solid)\n"
            "inline constexpr bool has_icon_font = true;\n"
            "// fa_solid_900_ttf and fa_solid_900_ttf_size defined in embedded_fa_solid_900.h\n\n"
        )
    else()
        file(APPEND "${MASTER_HEADER}"
            "// Icon font not available - download fa-solid-900.ttf to resources/fonts/\n"
            "inline constexpr bool has_icon_font = false;\n"
            "inline constexpr unsigned char fa_solid_900_ttf[] = {0};\n"
            "inline constexpr size_t fa_solid_900_ttf_size = 0;\n\n"
        )
    endif()

    # SLEIGH resources
    if(HAS_SLEIGH)
        file(APPEND "${MASTER_HEADER}"
            "// SLEIGH processor specification files for Ghidra decompiler\n"
            "inline constexpr bool has_sleigh = true;\n"
            "inline constexpr std::array<ResourceInfo, 5> sleigh_resources = {{\n"
            "    {\"x86.ldefs\", x86_ldefs, x86_ldefs_size},\n"
            "    {\"x86.pspec\", x86_pspec, x86_pspec_size},\n"
            "    {\"x86-64.sla\", x86_64_sla, x86_64_sla_size},\n"
            "    {\"x86-64.pspec\", x86_64_pspec, x86_64_pspec_size},\n"
            "    {\"x86-64-win.cspec\", x86_64_win_cspec, x86_64_win_cspec_size},\n"
            "}};\n\n"
        )
    else()
        file(APPEND "${MASTER_HEADER}"
            "// SLEIGH resources not available\n"
            "inline constexpr bool has_sleigh = false;\n"
            "inline constexpr std::array<ResourceInfo, 0> sleigh_resources = {{}};\n\n"
        )
    endif()

    file(APPEND "${MASTER_HEADER}"
        "// Icon resource (optional)\n"
    )

    if(HAS_ICON)
        file(APPEND "${MASTER_HEADER}"
            "inline constexpr bool has_icon = true;\n"
            "// orpheus_png and orpheus_png_size defined in embedded_orpheus_icon.h\n\n"
        )
    else()
        file(APPEND "${MASTER_HEADER}"
            "inline constexpr bool has_icon = false;\n"
            "inline constexpr unsigned char orpheus_png[] = {0};  // Placeholder byte\n"
            "inline constexpr size_t orpheus_png_size = 0;\n\n"
        )
    endif()

    # MCP Bridge resource
    if(HAS_MCP_BRIDGE)
        file(APPEND "${MASTER_HEADER}"
            "// MCP Bridge script\n"
            "inline constexpr bool has_mcp_bridge = true;\n"
            "// mcp_bridge_js and mcp_bridge_js_size defined in embedded_mcp_bridge.h\n\n"
        )
    else()
        file(APPEND "${MASTER_HEADER}"
            "// MCP Bridge script (not found)\n"
            "inline constexpr bool has_mcp_bridge = false;\n"
            "inline constexpr unsigned char mcp_bridge_js[] = {0};\n"
            "inline constexpr size_t mcp_bridge_js_size = 0;\n\n"
        )
    endif()

    # CS2 Map resources
    if(HAS_MAPS)
        file(APPEND "${MASTER_HEADER}"
            "// CS2 Map resources (radar images and info files)\n"
            "inline constexpr bool has_maps = true;\n"
            "inline constexpr size_t map_count = ${MAP_COUNT};\n\n"
            "struct MapResource {\n"
            "    std::string_view name;\n"
            "    const unsigned char* radar_data;\n"
            "    size_t radar_size;\n"
            "    const unsigned char* info_data;\n"
            "    size_t info_size;\n"
            "};\n\n"
            "inline constexpr std::array<MapResource, ${MAP_COUNT}> map_resources = {{\n"
        )

        # Generate entries for each map
        set(FIRST_MAP TRUE)
        foreach(map_name ${CS2_MAPS})
            set(MAP_DIR "${MAPS_DIR}/${map_name}")
            if(EXISTS "${MAP_DIR}/radar.png" AND EXISTS "${MAP_DIR}/info.txt")
                if(NOT FIRST_MAP)
                    file(APPEND "${MASTER_HEADER}" ",\n")
                endif()
                # Variable names match embed_resource output: map_{mapname}_radar_png, map_{mapname}_info_txt
                string(REPLACE "-" "_" safe_name "${map_name}")
                file(APPEND "${MASTER_HEADER}"
                    "    {\"${map_name}\", map_${safe_name}_radar_png, map_${safe_name}_radar_png_size, map_${safe_name}_info_txt, map_${safe_name}_info_txt_size}"
                )
                set(FIRST_MAP FALSE)
            endif()
        endforeach()

        file(APPEND "${MASTER_HEADER}"
            "\n}};\n\n"
            "// Map lookup helper\n"
            "inline const MapResource* GetMapResource(std::string_view name) {\n"
            "    for (const auto& map : map_resources) {\n"
            "        if (map.name == name) return &map;\n"
            "    }\n"
            "    return nullptr;\n"
            "}\n\n"
        )
    else()
        file(APPEND "${MASTER_HEADER}"
            "// CS2 Map resources (not found)\n"
            "inline constexpr bool has_maps = false;\n"
            "inline constexpr size_t map_count = 0;\n\n"
        )
    endif()

    file(APPEND "${MASTER_HEADER}"
        "} // namespace orpheus::embedded\n"
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
