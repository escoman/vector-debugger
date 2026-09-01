#pragma once

#include <cstdint>
#include "backend.h"

// Forward declarations
class DebugBackend;

// ---------------------------------------------------------------------------
// Memory Inspector Window (Stage 3.3)
//
// Displays 64 KB virtual address space with banking support.
// Uses MemorySnapshot for efficient data access.
// ---------------------------------------------------------------------------

class MemoryInspectorWindow
{
public:
    MemoryInspectorWindow();
    
    // Render the window. Call every frame.
    void render(DebugBackend &backend);
    
    // Request snapshot refresh on next render
    void requestRefresh() { needsRefresh_ = true; }
    
    // Check if window is visible
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    
private:
    bool visible_ = true;
    
    // Address state
    uint16_t address_ = 0;          // Current view address (for scroll position)
    uint16_t selectedAddress_ = 0;  // Selected/clicked address
    
    // Snapshot cache
    MemorySnapshot snapshot_;
    bool needsRefresh_ = true;
    
    // Input buffer for address field
    char addressInput_[8] = "0000";
    
    // Render sub-components
    void renderToolbar(DebugBackend &backend);
    void renderMemoryView(DebugBackend &backend);
    void renderDisassembly(DebugBackend &backend);
    
    // Snapshot management
    void refreshSnapshot(DebugBackend &backend);
    
    // Address parsing
    bool parseAddress(const char *input, uint16_t &address) const;
};
