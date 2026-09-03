#pragma once

#include <cstdint>
#include <functional>

// Forward declarations
class IDebugBackend;

// ---------------------------------------------------------------------------
// Functions Window — Stage 4.5
//
// Table of all user-defined functions and labels.
// Columns: Address | Name | Size | Calls | Comment
// ---------------------------------------------------------------------------

class FunctionsWindow
{
public:
    FunctionsWindow() = default;

    void render(IDebugBackend &backend);

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }
    void requestRefresh() { needsRefresh_ = true; }

    // Navigation callbacks
    std::function<void(uint16_t address)> onGoToDisassembly;
    std::function<void(uint16_t address)> onGoToMemoryInspector;

private:
    bool visible_ = true;
    bool needsRefresh_ = true;

    // Search/filter
    char searchBuffer_[64] = "";

    // Sorting
    int sortColumn_ = 0;  // 0=address, 1=name
    bool sortReverse_ = false;

    // "Define Function" dialog state
    bool showDefineDialog_ = false;
    char defineAddrBuffer_[8] = "";
    char defineNameBuffer_[64] = "";
    char defineCommentBuffer_[128] = "";

    // Context menu state
    uint16_t contextAddress_ = 0;

    // Inline editing state
    bool editingName_ = false;
    uint16_t editingAddress_ = 0;
    char editNameBuffer_[64] = "";

    bool editingComment_ = false;
    char editCommentBuffer_[128] = "";
};
