#pragma once

#include <memory>

namespace orpheus {

class DMAInterface;
class BookmarkManager;

namespace analysis {
    class Disassembler;
    class MemoryWatcher;
}

namespace core {
    class TaskManager;
}

/**
 * OrpheusCore - Headless core owning all analysis tools without any UI dependency.
 *
 * This class can be used standalone (headless mode) or embedded inside
 * the GUI Application. It provides DMA access, disassembly, bookmarks,
 * and memory watching — everything the MCP server needs.
 */
class OrpheusCore {
public:
    OrpheusCore();
    ~OrpheusCore();

    // Non-copyable, non-movable
    OrpheusCore(const OrpheusCore&) = delete;
    OrpheusCore& operator=(const OrpheusCore&) = delete;

    /**
     * Initialize core subsystems (DMA interface, disassembler, bookmarks).
     * Does NOT initialize any UI or graphics.
     */
    bool Initialize();

    /**
     * Shut down and release all resources.
     */
    void Shutdown();

    // Accessors
    DMAInterface* GetDMA() { return dma_.get(); }
    analysis::Disassembler* GetDisassembler() { return disassembler_.get(); }
    BookmarkManager* GetBookmarks() { return bookmarks_.get(); }
    analysis::MemoryWatcher* GetMemoryWatcher() { return memory_watcher_.get(); }

    // TaskManager is a singleton — convenience accessor
    static core::TaskManager& GetTaskManager();

private:
    std::unique_ptr<DMAInterface> dma_;
    std::unique_ptr<analysis::Disassembler> disassembler_;
    std::unique_ptr<BookmarkManager> bookmarks_;
    std::unique_ptr<analysis::MemoryWatcher> memory_watcher_;
};

} // namespace orpheus
