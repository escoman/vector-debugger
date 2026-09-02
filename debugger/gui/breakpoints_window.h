#pragma once

#include <cstdint>
#include "backend.h"

// Forward declarations
class DebugBackend;

// ---------------------------------------------------------------------------
// Breakpoints Window (Stage 3.7)
//
// Displays all breakpoints with enable/disable toggle, add/remove controls.
// Uses DebugBackend API — no direct access to Board or CPU.
// ---------------------------------------------------------------------------

class BreakpointsWindow
{
public:
    BreakpointsWindow();
    
    // Render the window. Call every frame.
    void render(DebugBackend &backend);
    
    // Check if window is visible
    bool isVisible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }
    
private:
    bool visible_ = true;
    
    // Input buffer for address field
    char addressInput_[8] = "";
    
    // Selected breakpoint index in the list (-1 = none)
    int selectedBpIndex_ = -1;
};
