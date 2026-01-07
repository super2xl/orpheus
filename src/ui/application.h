#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <deque>

#include "core/dma_interface.h"

// Forward declarations
struct GLFWwindow;
struct ImFont;

namespace orpheus {

class DMAInterface;
class BookmarkManager;

namespace analysis {
    class Disassembler;
    class MemoryWatcher;
    class RTTIParser;
    struct InstructionInfo;
    struct StringMatch;
    struct PatternMatch;
    struct WatchRegion;
    struct MemoryChange;
    struct RTTIClassInfo;
}

namespace mcp {
    class MCPServer;
    struct MCPConfig;
}

namespace emulation {
    class Emulator;
}

} // namespace orpheus

// Forward declarations for game-specific dumpers
namespace dumper {
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

    // Dialogs
    void RenderCommandPalette();
    void RenderAboutDialog();
    void RenderDumpDialog();
    void RenderGotoDialog();
    void RenderSettingsDialog();

    // Helpers
    void RefreshProcesses();
    void RefreshModules();
    void NavigateToAddress(uint64_t address, bool add_to_history = true);
    void NavigateBack();
    void NavigateForward();
    bool CanNavigateBack() const { return history_index_ > 0; }
    bool CanNavigateForward() const { return history_index_ >= 0 && history_index_ < (int)address_history_.size() - 1; }
    void DumpModule(uint64_t base_address, uint32_t size, const std::string& name);

    void Shutdown();

    // Window
    GLFWwindow* window_ = nullptr;
    bool running_ = false;
    bool first_frame_ = true;

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

    // Panel visibility
    PanelState panels_;

    // Dialog states
    bool show_about_ = false;
    bool show_command_palette_ = false;
    bool show_dump_dialog_ = false;
    bool show_goto_dialog_ = false;
    bool show_settings_ = false;
    bool show_demo_ = false;

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
    std::unique_ptr<::dumper::CS2SchemaDumper> cs2_schema_;
    uint32_t cs2_schema_pid_ = 0;
    bool cs2_schema_initialized_ = false;
    bool cs2_schema_dumping_ = false;
    int cs2_schema_progress_ = 0;
    int cs2_schema_total_ = 0;
    char cs2_class_filter_[256] = {};
    char cs2_field_filter_[256] = {};
    int cs2_selected_scope_ = 0;
    std::string cs2_selected_class_;
    std::vector<::dumper::SchemaClass> cs2_cached_classes_;

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
};

} // namespace ui
} // namespace orpheus
