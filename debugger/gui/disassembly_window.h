#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include "backend.h"

// Forward declarations
class DebugBackend;

// ---------------------------------------------------------------------------
// Disassembly View (Stage 3.8)
//
// Shows disassembled code around the current PC with breakpoint markers,
// Follow PC, Go To Address, Step, and context menu for breakpoints.
// Uses DebugBackend API only — no direct Memory/Board/CPU access.
// ---------------------------------------------------------------------------

class DisassemblyWindow
{
public:
    DisassemblyWindow();
    
    // Render the window. Call every frame.
    void render(DebugBackend &backend);
    
    // Request refresh on next render
    void requestRefresh() { needsRefresh_ = true; }
    
    // Check if window is visible
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    
    // Callback: called when user wants to navigate to Memory Inspector
    // Parameter: address to navigate to
    std::function<void(uint16_t address)> onGoToMemoryInspector;
    
private:
    bool visible_ = true;
    bool followPc_ = true;
    bool needsRefresh_ = true;
    
    uint16_t viewAddress_ = 0;          // top of the disassembly view
    uint16_t lastPc_ = 0;               // previous PC for change detection
    bool     pcInitialized_ = false;     // first frame flag
    
    char addressInput_[8] = "0000";
    
    // Render sub-components
    void renderToolbar(DebugBackend &backend);
    void renderDisassemblyList(DebugBackend &backend);
    
    // Address parsing
    bool parseAddress(const char *input, uint16_t &address) const;
};
