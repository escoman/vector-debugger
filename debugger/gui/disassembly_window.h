#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include "idebug_backend.h"

// Forward declarations
class IDebugBackend;

// ---------------------------------------------------------------------------
// Disassembly View (Stage 3.8)
//
// Shows disassembled code around the current PC with breakpoint markers,
// Follow PC, Go To Address, Step, and context menu for breakpoints.
// Uses IDebugBackend API only — no direct Memory/Board/CPU access.
// ---------------------------------------------------------------------------

class DisassemblyWindow
{
public:
    DisassemblyWindow() {}
    
    // Render the window. Call every frame.
    void render(IDebugBackend &backend);
    
    // Request refresh on next render
    void requestRefresh() { needsRefresh_ = true; }
    
    // Navigate to specific address (Stage 3.9 — real scrolling, inline for testability)
    void gotoAddress(uint16_t address) {
        viewAddress_ = address;
        followPc_ = false;
        pendingScroll_ = true;
        needsRefresh_ = true;
        visible_ = true;
        snprintf(addressInput_, sizeof(addressInput_), "%04X", address);
    }
    
    // Current view address (for tests / navigation)
    uint16_t address() const { return viewAddress_; }
    
    // Check if window is visible
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    bool &getVisibleRef() { return visible_; }
    
    // Callback: called when user wants to navigate to Memory Inspector
    // Parameter: address to navigate to
    std::function<void(uint16_t address)> onGoToMemoryInspector;
    
private:
    bool visible_ = true;
    bool followPc_ = true;
    bool needsRefresh_ = true;
    bool pendingScroll_ = false;        // Stage 3.9: real scroll on next render
    
    uint16_t viewAddress_ = 0;          // top of the disassembly view
    uint16_t lastPc_ = 0;               // previous PC for change detection
    bool     pcInitialized_ = false;     // first frame flag
    
    char addressInput_[8] = "0000";
    
    // Stage 4.6: inline editing for symbols/comments
    bool editingDefineFunc_ = false;
    bool editingDefineLabel_ = false;
    bool editingComment_ = false;
    uint16_t editingAddress_ = 0;
    char editBuffer_[64] = "";
    
    // Render sub-components
    void renderToolbar(IDebugBackend &backend);
    void renderDisassemblyList(IDebugBackend &backend);
    
    // Address parsing
    bool parseAddress(const char *input, uint16_t &address) const;
};
