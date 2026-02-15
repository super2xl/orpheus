#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <deque>
#include <future>
#include <atomic>
#include <chrono>

#include "core/dma_interface.h"
#include "analysis/function_recovery.h"

// Forward declarations
struct GLFWwindow;
struct ImFont;
struct ImVec2;

namespace orpheus {

class DMAInterface;
class BookmarkManager;

namespace analysis {
    class Disassembler;
    class MemoryWatcher;
    class RTTIParser;
    class CFGBuilder;
    class FunctionRecovery;
    struct InstructionInfo;
    struct StringMatch;
    struct PatternMatch;
    struct WatchRegion;
    struct MemoryChange;
    struct RTTIClassInfo;
    struct ControlFlowGraph;
    struct CFGNode;
    struct CFGEdge;
}

namespace mcp {
    class MCPServer;
    struct MCPConfig;
}

namespace emulation {
    class Emulator;
}

#ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
class Decompiler;
#endif

} // namespace orpheus

// Forward declarations for game-specific dumpers
namespace orpheus::dumper {
    class CS2SchemaDumper;
    struct SchemaClass;
}

namespace orpheus {
namespace ui {

/**
 * Keybind - Single keyboard shortcut
 */
struct Keybind {
    std::string name;
    std::string description;
    int key;
    int modifiers;  // GLFW mods: Ctrl=2, Shift=1, Alt=4
    std::function<void()> action;
};

/**
 * Theme - UI color themes
 */
enum class Theme {
    Dark = 0,
    Light = 1,
    Nord = 2,             // Popular dark theme with blue tints
    Dracula = 3,          // Purple/pink accents on dark background
    CatppuccinMocha = 4   // Warm pastel dark theme
};

/**
 * PanelState - Track panel visibility
 */
struct PanelState {
    bool process_list = true;
    bool module_list = true;
    bool memory_viewer = true;
    bool disassembly = true;
    bool pattern_scanner = true;
    bool string_scanner = true;
    bool memory_watcher = true;
    bool rtti_scanner = false;
    bool bookmarks = false;
    bool console = true;
    bool imports = false;
    bool exports = false;
    bool emulator = false;
    bool cs2_schema = false;  // CS2-specific schema dumper
    bool cs2_entity_inspector = false;  // CS2 entity inspector (RTTI + Schema)
    bool decompiler = true;  // Ghidra decompiler view (shown by default)
    bool cfg_viewer = false;  // Control Flow Graph visualization
    bool cs2_radar = false;  // CS2 Live Radar view
    bool cs2_dashboard = false;  // CS2 Player Dashboard
    bool pointer_chain = false;  // Pointer Chain Resolver
    bool memory_regions = false;  // VAD memory regions panel
    bool xref_finder = false;    // Cross-reference finder panel
    bool function_recovery = false;  // Function recovery panel
    bool vtable_reader = false;      // VTable reader panel
    bool cache_manager = false;      // Cache management panel
    bool task_manager = false;       // Background task manager panel
};

/**
 * Application - Main ImGui application with professional layout
 */
class Application {
public:
    Application();
    ~Application();

    // Delete copy/move
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize(const std::string& title = "Orpheus - DMA Reversing Framework",
                    int width = 1920, int height = 1080);
    int Run();
    void RequestExit();

    DMAInterface* GetDMA() { return dma_.get(); }
    BookmarkManager* GetBookmarks() { return bookmarks_.get(); }
    void GetWindowSize(int& width, int& height) const;
    bool IsMinimized() const;

private:
    // Initialization
    bool InitializeGLFW();
    bool InitializeImGui();
    void SetupImGuiStyle();
    void SetupDefaultLayout();
    void RegisterKeybinds();
    void ApplyTheme(Theme theme);
    void RebuildFonts();
    void ToggleFullscreen();
    void SetWindowIcon();

    // Main loop
    void BeginFrame();
    void EndFrame();
    void ProcessInput();
    void RenderDockspace();
    void RenderMenuBar();
    void RenderStatusBar();
    void RenderToolbar();

    // Panels - Core
    void RenderProcessList();
    void RenderModuleList();
    void RenderMemoryViewer();
    void RenderDisassembly();
    void RenderConsole();

    // Panels - Analysis
    void RenderPatternScanner();
    void RenderStringScanner();
    void RenderMemoryWatcher();
    void RenderRTTIScanner();
    void RenderBookmarks();
    void RenderImports();
    void RenderExports();
    void RenderEmulatorPanel();
    void RenderCS2Schema();
    void RenderCS2EntityInspector();
    void RenderDecompiler();
    void RenderCFGViewer();
    void RenderCS2Radar();
    void RenderCS2Dashboard();
    void RenderPointerChain();
    void RenderMemoryRegions();
    void RenderXRefFinder();
    void RenderFunctionRecovery();
    void RenderVTableReader();
    void RenderCacheManager();
    void RenderTaskManager();

    // Dialogs
    void RenderCommandPalette();
    void RenderAboutDialog();
    void RenderDumpDialog();
    void RenderGotoDialog();
    void RenderSettingsDialog();
#ifdef PLATFORM_LINUX
    void RenderUdevPermissionDialog();
#endif

    // Helpers
    void RefreshProcesses();
    void RefreshModules();
    void NavigateToAddress(uint64_t address, bool add_to_history = true);
    void NavigateBack();
    void NavigateForward();
    bool CanNavigateBack() const { return history_index_ > 0; }
    bool CanNavigateForward() const { return history_index_ >= 0 && history_index_ < (int)address_history_.size() - 1; }
    void DumpModule(uint64_t base_address, uint32_t size, const std::string& name);

    // CS2 auto-initialization
    bool InitializeCS2();  // Returns true if CS2 was initialized successfully
    bool IsCS2Process() const;

    void Shutdown();

    // Window
    GLFWwindow* window_ = nullptr;
    bool running_ = false;
    bool first_frame_ = true;

    // ImGui settings path (stored as member since ImGui keeps pointer)
    std::string ini_path_;

    // Fullscreen state
    bool is_fullscreen_ = false;
    int windowed_x_ = 100;
    int windowed_y_ = 100;
    int windowed_width_ = 1920;
    int windowed_height_ = 1080;

    // Fonts
    ImFont* font_regular_ = nullptr;   // JetBrains Mono Regular - main UI
    ImFont* font_bold_ = nullptr;      // JetBrains Mono Bold - headings
    ImFont* font_mono_ = nullptr;      // JetBrains Mono Medium - hex/disasm

    // Appearance
    Theme current_theme_ = Theme::Dark;
    float font_size_ = 15.0f;      // Base font size (unscaled)
    float dpi_scale_ = 1.0f;       // DPI scale factor from monitor
    bool theme_changed_ = false;
    bool pending_font_rebuild_ = false;

    // DMA
    std::unique_ptr<DMAInterface> dma_;
    std::future<bool> dma_connect_future_;
    std::atomic<bool> dma_connecting_{false};

    // Analysis tools
    std::unique_ptr<analysis::Disassembler> disassembler_;
    std::unique_ptr<analysis::MemoryWatcher> memory_watcher_;

    // MCP Server
    std::unique_ptr<mcp::MCPServer> mcp_server_;
    std::unique_ptr<mcp::MCPConfig> mcp_config_;
    bool mcp_config_dirty_ = false;

    // Bookmarks
    std::unique_ptr<BookmarkManager> bookmarks_;

    // Emulator
    std::unique_ptr<emulation::Emulator> emulator_;
    uint32_t emulator_pid_ = 0;

    #ifdef ORPHEUS_HAS_GHIDRA_DECOMPILER
    // Decompiler
    std::unique_ptr<Decompiler> decompiler_;
    std::string decompiled_code_;
    uint64_t decompile_address_ = 0;
    char decompile_address_input_[32] = {};
    bool decompiler_initialized_ = false;
#endif

    // CFG Viewer state
    std::unique_ptr<analysis::ControlFlowGraph> cfg_;
    std::unique_ptr<analysis::CFGBuilder> cfg_builder_;
    uint64_t cfg_function_addr_ = 0;
    char cfg_address_input_[32] = {};
    uint64_t cfg_selected_node_ = 0;
    float cfg_scroll_x_ = 0.0f;
    float cfg_scroll_y_ = 0.0f;
    float cfg_zoom_ = 1.0f;
    bool cfg_needs_layout_ = false;

    // Panel visibility
    PanelState panels_;

    // Dialog states
    bool show_about_ = false;
    bool show_command_palette_ = false;
    bool show_dump_dialog_ = false;
    bool show_goto_dialog_ = false;
    bool show_settings_ = false;
    bool show_demo_ = false;
#ifdef PLATFORM_LINUX
    bool show_udev_dialog_ = false;
    std::string udev_vendor_id_;
    std::string udev_product_id_;
#endif

    // Process state
    uint32_t selected_pid_ = 0;
    std::string selected_process_name_;
    std::vector<ProcessInfo> cached_processes_;

    // Module state
    std::string selected_module_name_;
    uint64_t selected_module_base_ = 0;
    uint32_t selected_module_size_ = 0;
    std::vector<ModuleInfo> cached_modules_;
    char module_filter_[256] = {};
    int module_sort_column_ = 0;   // 0=name, 1=base, 2=size
    bool module_sort_ascending_ = true;

    // Process list sort state
    int process_sort_column_ = 0;  // 0=pid, 1=name, 2=arch
    bool process_sort_ascending_ = true;

    // Memory viewer state
    uint64_t memory_address_ = 0;
    std::vector<uint8_t> memory_data_;
    char address_input_[32] = {};
    int bytes_per_row_ = 16;
    bool show_ascii_ = true;

    // Disassembly state
    uint64_t disasm_address_ = 0;
    std::vector<analysis::InstructionInfo> disasm_instructions_;
    char disasm_address_input_[32] = {};

    // Pattern scanner state
    char pattern_input_[256] = {};
    std::vector<uint64_t> pattern_results_;
    bool pattern_scanning_ = false;

    // Async pattern scanning state
    std::future<std::vector<uint64_t>> pattern_scan_future_;
    std::atomic<bool> pattern_scan_cancel_requested_{false};
    std::string pattern_scan_progress_stage_;  // "Reading chunk 5/20..."
    float pattern_scan_progress_ = 0.0f;       // 0.0 to 1.0
    std::string pattern_scan_error_;           // Error message if failed

    // String scanner state
    int string_min_length_ = 4;
    bool scan_ascii_ = true;
    bool scan_unicode_ = true;
    std::vector<analysis::StringMatch> string_results_;
    bool string_scanning_ = false;

    // Console state
    char console_filter_[256] = {};
    bool console_auto_scroll_ = true;

    // Dump dialog state
    char dump_filename_[256] = {};
    bool dump_fix_headers_ = true;
    bool dump_rebuild_iat_ = true;
    bool dump_unmap_sections_ = true;
    bool dump_in_progress_ = false;
    float dump_progress_ = 0.0f;

    // Keybinds
    std::vector<Keybind> keybinds_;
    char command_search_[256] = {};

    // Status bar
    std::string status_message_;
    float status_timer_ = 0.0f;

    // Address history (back/forward navigation)
    std::deque<uint64_t> address_history_;
    int history_index_ = -1;
    static constexpr int MAX_HISTORY_SIZE = 100;

    // Auto-refresh state
    bool auto_refresh_enabled_ = true;
    float process_refresh_interval_ = 2.0f;   // seconds
    float module_refresh_interval_ = 1.0f;    // seconds
    double last_process_refresh_ = 0.0;
    double last_module_refresh_ = 0.0;

    // Emulator state
    char emu_start_addr_[32] = {};
    char emu_end_addr_[32] = {};
    char emu_instr_count_[16] = "100";
    char emu_map_module_[64] = {};
    char emu_map_addr_[32] = {};
    char emu_map_size_[16] = "4096";
    std::string emu_last_result_;
    bool emu_show_registers_ = true;

    // Memory Watcher state
    char watch_addr_input_[32] = {};
    char watch_size_input_[16] = "8";
    char watch_name_input_[64] = {};
    int watch_type_index_ = 1;  // Default: Write
    int watch_scan_interval_ = 100;  // ms
    uint32_t watcher_pid_ = 0;  // Track which PID the watcher is for

    // RTTI Scanner state
    std::vector<analysis::RTTIClassInfo> rtti_results_;
    bool rtti_scanning_ = false;
    uint64_t rtti_scanned_module_base_ = 0;
    std::string rtti_scanned_module_name_;
    char rtti_filter_[256] = {};
    int rtti_sort_column_ = 0;  // 0=vtable, 1=methods, 2=flags, 3=type
    bool rtti_sort_ascending_ = true;

    // Bookmark UI state
    char bookmark_label_[64] = {};
    char bookmark_notes_[256] = {};
    char bookmark_category_[32] = {};
    char bookmark_filter_[128] = {};
    int bookmark_edit_index_ = -1;  // -1 = not editing
    bool show_add_bookmark_popup_ = false;

    // CS2 Schema Dumper state
    std::unique_ptr<orpheus::dumper::CS2SchemaDumper> cs2_schema_;
    uint32_t cs2_schema_pid_ = 0;
    bool cs2_schema_initialized_ = false;
    bool cs2_schema_dumping_ = false;
    int cs2_schema_progress_ = 0;
    int cs2_schema_total_ = 0;
    char cs2_class_filter_[256] = {};
    char cs2_field_filter_[256] = {};
    int cs2_selected_scope_ = 0;
    std::string cs2_selected_class_;
    std::vector<orpheus::dumper::SchemaClass> cs2_cached_classes_;

    // CS2 auto-init state
    bool cs2_auto_init_attempted_ = false;  // Prevent repeated init attempts on failure
    bool cs2_auto_init_success_ = false;    // Track if auto-init succeeded

    // CS2 Entity Inspector state
    bool cs2_entity_initialized_ = false;
    uint64_t cs2_entity_system_ = 0;
    uint64_t cs2_local_player_array_ = 0;
    uint64_t cs2_client_base_ = 0;
    uint32_t cs2_client_size_ = 0;
    uint64_t cs2_selected_entity_ = 0;
    std::string cs2_selected_entity_class_;
    char cs2_entity_filter_[256] = {};
    bool cs2_entity_auto_refresh_ = false;
    float cs2_entity_refresh_timer_ = 0.0f;

    // Cached entity data for display
    struct EntityCacheEntry {
        uint64_t address = 0;
        std::string class_name;
        int entity_index = -1;
    };
    std::vector<EntityCacheEntry> cs2_entity_cache_;

    // Cached field data for selected entity
    struct FieldCacheEntry {
        std::string name;
        std::string type;
        uint32_t offset = 0;
        std::string value;
    };
    std::vector<FieldCacheEntry> cs2_field_cache_;

    // CS2 Radar state
    struct MapInfo {
        std::string name;
        float pos_x = 0.0f;   // World X offset
        float pos_y = 0.0f;   // World Y offset
        float scale = 1.0f;   // World units per pixel
        unsigned int texture_id = 0;
        int texture_width = 0;
        int texture_height = 0;
        bool loaded = false;
    };
    MapInfo radar_map_;
    std::string radar_current_map_;  // e.g., "de_dust2"
    std::string radar_detected_map_; // Auto-detected from CS2 memory
    uint64_t radar_map_name_addr_ = 0; // Cached address of map name string
    bool radar_auto_detect_map_ = true; // Auto-detect map from CS2 memory
    float radar_zoom_ = 1.0f;
    float radar_scroll_x_ = 0.0f;
    float radar_scroll_y_ = 0.0f;
    bool radar_center_on_local_ = true;
    bool radar_show_names_ = true;
    bool radar_auto_refresh_ = true;
    float radar_refresh_timer_ = 0.0f;
    float radar_refresh_interval_ = 0.1f;  // 100ms refresh rate

    // Radar player data cache
    struct RadarPlayer {
        std::string name;
        int team = 0;           // 2=T, 3=CT
        float x = 0, y = 0, z = 0;
        int health = 0;
        bool is_alive = false;
        bool is_local = false;
        bool is_spotted = false;
    };
    std::vector<RadarPlayer> radar_players_;

    // CS2 Dashboard state
    bool dashboard_show_all_players_ = true;
    bool dashboard_show_bots_ = false;
    int dashboard_selected_player_ = -1;

    // Pointer Chain Resolver state
    char pointer_base_input_[32] = {};
    char pointer_offsets_input_[256] = {};  // comma-separated offsets
    std::vector<std::pair<uint64_t, uint64_t>> pointer_chain_results_;  // (address, value) pairs
    uint64_t pointer_final_address_ = 0;
    std::string pointer_chain_error_;
    int pointer_final_type_ = 0;  // 0=int32, 1=float, 2=int64, 3=double

    // Memory Regions state
    std::vector<MemoryRegion> cached_memory_regions_;
    uint32_t memory_regions_pid_ = 0;  // Track which PID the regions belong to
    char memory_regions_filter_[256] = {};
    int memory_regions_sort_column_ = 0;  // 0=base, 1=size, 2=protection, 3=type
    bool memory_regions_sort_ascending_ = true;

    // Function Recovery state
    std::unique_ptr<analysis::FunctionRecovery> function_recovery_;
    std::vector<analysis::FunctionInfo> recovered_functions_;
    std::future<std::map<uint64_t, analysis::FunctionInfo>> function_recovery_future_;
    bool function_recovery_running_ = false;
    char function_filter_[256] = {};
    uint64_t function_recovery_module_base_ = 0;
    uint32_t function_recovery_module_size_ = 0;
    std::string function_recovery_module_name_;
    bool function_recovery_use_prologues_ = true;
    bool function_recovery_follow_calls_ = true;
    bool function_recovery_use_pdata_ = true;
    std::string function_recovery_progress_stage_;
    float function_recovery_progress_ = 0.0f;
    int function_recovery_sort_column_ = 0;  // 0=address, 1=size, 2=name, 3=source
    bool function_recovery_sort_ascending_ = true;
    char function_containing_input_[32] = {};
    uint64_t function_containing_result_addr_ = 0;
    std::string function_containing_result_name_;

    // XRef Finder state
    struct XRefResult {
        uint64_t address = 0;
        std::string type;       // "ptr64" or "rel32"
        std::string context;    // module+offset
    };
    char xref_target_input_[32] = {};
    char xref_base_input_[32] = {};
    char xref_size_input_[16] = {};
    std::vector<XRefResult> xref_results_;
    bool xref_use_module_ = true;  // Use selected module vs custom range
    bool xref_scanning_ = false;

    // Signature Generator state (in disassembly panel)
    std::string generated_signature_;
    std::string generated_signature_ida_;
    std::string generated_signature_ce_;
    std::string generated_signature_mask_;
    int generated_signature_length_ = 0;
    int generated_signature_unique_ = 0;
    float generated_signature_ratio_ = 0.0f;
    bool generated_signature_valid_ = false;
    bool show_signature_popup_ = false;
    uint64_t signature_address_ = 0;

    // VTable Reader state
    struct VTableEntry {
        uint64_t address = 0;        // Function pointer address
        uint64_t function = 0;       // Function pointer value
        std::string context;         // module+offset
        std::string first_instr;     // First instruction at function
        bool valid = false;
    };
    char vtable_address_input_[32] = {};
    int vtable_entry_count_ = 20;    // Number of entries to read
    bool vtable_disasm_ = false;     // Show first instruction of each entry
    std::vector<VTableEntry> vtable_entries_;
    std::string vtable_class_name_;  // RTTI class name if found
    std::string vtable_error_;

    // Cache Manager state
    struct CacheEntry {
        std::string name;
        std::string path;
        size_t size = 0;
        std::string modified;
        std::string type;  // "rtti", "schema", "functions"
    };
    std::vector<CacheEntry> cache_entries_;
    int cache_selected_type_ = 0;  // 0=All, 1=RTTI, 2=Schema, 3=Functions
    char cache_filter_[256] = {};
    bool cache_needs_refresh_ = true;

    // Task Manager state
    struct TaskInfo {
        std::string id;
        std::string type;           // "pattern_scan", "string_scan", etc.
        std::string status;         // "pending", "running", "completed", "failed", "cancelled"
        float progress = 0.0f;      // 0.0 - 1.0
        std::string message;        // Status message
        std::string result_preview; // Preview of result (first few matches, etc.)
        std::chrono::system_clock::time_point created;
        std::chrono::system_clock::time_point completed;
    };
    std::vector<TaskInfo> task_list_;
    bool task_list_auto_refresh_ = true;
    float task_refresh_timer_ = 0.0f;
    float task_refresh_interval_ = 0.5f;  // Refresh every 500ms
    int task_filter_status_ = 0;  // 0=All, 1=Running, 2=Completed, 3=Failed

    // Helper functions for radar
    bool LoadRadarMap(const std::string& map_name);
    void UnloadRadarMap();
    void RefreshRadarData();
    ImVec2 WorldToRadar(float world_x, float world_y, const ImVec2& canvas_pos, const ImVec2& canvas_size);
};

} // namespace ui
} // namespace orpheus
