#include "orpheus_core.h"
#include "dma_interface.h"
#include "task_manager.h"
#include "analysis/disassembler.h"
#include "utils/bookmarks.h"
#include "utils/logger.h"

// MemoryWatcher needs a ReadMemoryFunc but we don't wire it until a process is attached,
// so we just include the header for the destructor (unique_ptr).
#include "analysis/memory_watcher.h"

namespace orpheus {

OrpheusCore::OrpheusCore() = default;

OrpheusCore::~OrpheusCore() {
    Shutdown();
}

bool OrpheusCore::Initialize() {
    LOG_INFO("OrpheusCore::Initialize()");

    // Create DMA interface (not connected yet — caller must call dma_->Initialize())
    dma_ = std::make_unique<DMAInterface>();

    // Create disassembler (default: x64)
    disassembler_ = std::make_unique<analysis::Disassembler>(true);

    // Create bookmark manager and load saved bookmarks
    bookmarks_ = std::make_unique<BookmarkManager>();
    bookmarks_->Load();

    LOG_INFO("OrpheusCore initialized successfully");
    return true;
}

void OrpheusCore::Shutdown() {
    LOG_INFO("OrpheusCore::Shutdown()");

    if (memory_watcher_) {
        memory_watcher_->StopAutoScan();
        memory_watcher_.reset();
    }

    disassembler_.reset();

    if (dma_) {
        dma_->Close();
        dma_.reset();
    }

    bookmarks_.reset();
}

core::TaskManager& OrpheusCore::GetTaskManager() {
    return core::TaskManager::Instance();
}

} // namespace orpheus
