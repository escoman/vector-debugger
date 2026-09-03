#pragma once

#include <cstdint>
#include <vector>
#include <functional>

// Forward declarations
class DebugBackend;

// ---------------------------------------------------------------------------
// Memory Map Window — Stage 4.4
//
// Visualizes the full 64K address space as a 2D bitmap (256x256 pixels,
// each pixel = 256 bytes). Color by classification, overlay activity.
// ---------------------------------------------------------------------------

class MemoryMapWindow
{
public:
    MemoryMapWindow() = default;
    ~MemoryMapWindow();

    void render(DebugBackend &backend);

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }
    void requestRefresh() { needsRefresh_ = true; }

    // Navigation callbacks
    std::function<void(uint16_t address)> onGoToMemoryInspector;
    std::function<void(uint16_t address)> onGoToDisassembly;

private:
    bool visible_ = true;
    bool needsRefresh_ = true;

    // Map texture (256x256 pixels)
    unsigned int mapTextureId_ = 0;
    std::vector<uint32_t> mapPixels_;  // 256*256 ARGB

    // Hover state
    uint16_t hoverAddress_ = 0;

    // Context menu state
    uint16_t contextAddress_ = 0;
    bool showContextMenu_ = false;

    // Rebuild the map texture from backend data
    void rebuildMap(DebugBackend &backend);

    // Clean up GL texture on destruction
    void destroyTexture();

    // Convert pixel coordinates to address
    uint16_t pixelToAddress(int px, int py) const;

    // Get color for a memory region based on classification and activity
    uint32_t getColorForAddress(uint16_t baseAddr,
                                int regionType,
                                uint64_t maxActivity,
                                uint64_t activity) const;
};
