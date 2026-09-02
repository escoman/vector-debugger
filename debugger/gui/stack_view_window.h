#pragma once

#include <cstdint>
#include <functional>
#include "backend.h"

// Forward declarations
class DebugBackend;

// ---------------------------------------------------------------------------
// Stack View Window (Stage 3.4)
//
// Displays memory around current SP with byte and little-endian word columns.
// Uses existing readMemorySnapshot() API — no direct Memory access.
// ---------------------------------------------------------------------------

class StackViewWindow
{
public:
    StackViewWindow();
    
    // Render the window. Call every frame.
    void render(DebugBackend &backend);
    
    // Request snapshot refresh on next render
    void requestRefresh() { needsRefresh_ = true; }
    
    // Check if window is visible
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    
    // Callback: called when user wants to navigate to Memory Inspector
    // Parameter: address to navigate to
    std::function<void(uint16_t address)> onGoToMemoryInspector;
    
private:
    bool visible_ = true;
    
    // Snapshot cache
    MemorySnapshot snapshot_;
    bool needsRefresh_ = true;
    
    // Current SP (cached for follow logic)
    uint16_t currentSP_ = 0;
    
    // Render sub-components
    void renderToolbar(DebugBackend &backend);
    void renderStackView(DebugBackend &backend);
    
    // Snapshot management
    void refreshSnapshot(DebugBackend &backend);
    
    // Helper: compute safe range around SP
    static void computeRange(uint16_t sp, int &start, int &end);
};
