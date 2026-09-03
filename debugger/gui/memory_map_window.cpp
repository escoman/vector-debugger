#include "memory_map_window.h"
#include "idebug_backend.h"
#include "symbol_database.h"

// Dear ImGui
#include "imgui.h"

// OpenGL
#include "SDL.h"
#include "SDL_opengl.h"

#include <algorithm>
#include <cmath>

// Map dimensions: 256x256 pixels, each pixel = 256 bytes
static constexpr int MAP_WIDTH = 256;
static constexpr int MAP_HEIGHT = 256;
static constexpr int BYTES_PER_PIXEL = 256;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

MemoryMapWindow::~MemoryMapWindow()
{
    destroyTexture();
}

void MemoryMapWindow::destroyTexture()
{
    if (mapTextureId_) {
        glDeleteTextures(1, &mapTextureId_);
        mapTextureId_ = 0;
    }
}

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

uint16_t MemoryMapWindow::pixelToAddress(int px, int py) const
{
    // Each row = 256 bytes (one pixel per byte-group)
    // Row 0 = addresses 0x0000-0x00FF
    // Row 1 = addresses 0x0100-0x01FF
    // etc.
    int address = (py * MAP_WIDTH + px) * BYTES_PER_PIXEL;
    return static_cast<uint16_t>(address & 0xFFFF);
}

// ---------------------------------------------------------------------------
// Color calculation
// ---------------------------------------------------------------------------

uint32_t MemoryMapWindow::getColorForAddress(uint16_t baseAddr,
                                              int regionType,
                                              uint64_t maxActivity,
                                              uint64_t activity) const
{
    // Base colors by classification (ARGB)
    uint32_t baseColor;
    switch (regionType) {
        case 1:  // Code
            baseColor = 0xFF4040C0;  // Blue
            break;
        case 2:  // Data
            baseColor = 0xFF40C040;  // Green
            break;
        default: // Unknown
            baseColor = 0xFF808080;  // Gray
            break;
    }

    // Overlay activity: brighter = more active
    if (maxActivity > 0 && activity > 0) {
        // Calculate brightness multiplier (1.0 to 2.0)
        float ratio = static_cast<float>(activity) / static_cast<float>(maxActivity);
        float brightness = 1.0f + ratio;  // 1.0 to 2.0

        // Extract RGB components
        uint8_t r = (baseColor >> 16) & 0xFF;
        uint8_t g = (baseColor >> 8) & 0xFF;
        uint8_t b = baseColor & 0xFF;

        // Apply brightness
        r = static_cast<uint8_t>(std::min(255.0f, r * brightness));
        g = static_cast<uint8_t>(std::min(255.0f, g * brightness));
        b = static_cast<uint8_t>(std::min(255.0f, b * brightness));

        return 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    return baseColor;
}

// ---------------------------------------------------------------------------
// Map rebuild
// ---------------------------------------------------------------------------

void MemoryMapWindow::rebuildMap(IDebugBackend &backend)
{
    mapPixels_.resize(MAP_WIDTH * MAP_HEIGHT);

    // Get snapshots
    auto activitySnap = backend.activitySnapshot();
    auto symbols = backend.symbolDatabase();

    // Find max activity for normalization
    uint64_t maxActivity = 0;
    for (size_t i = 0; i < activitySnap.executeCount.size(); ++i) {
        maxActivity = std::max(maxActivity, activitySnap.executeCount[i]);
        maxActivity = std::max(maxActivity, activitySnap.readCount[i]);
        maxActivity = std::max(maxActivity, activitySnap.writeCount[i]);
    }

    // Build the map
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        for (int x = 0; x < MAP_WIDTH; ++x) {
            uint16_t baseAddr = pixelToAddress(x, y);

            // Get classification for this address
            int regionType = 0;  // Unknown
            MemoryRegionType mrt = symbols.classify(baseAddr);
            if (mrt == MemoryRegionType::Code) regionType = 1;
            else if (mrt == MemoryRegionType::Data) regionType = 2;

            // Get activity for this 256-byte block
            uint64_t activity = 0;
            for (int i = 0; i < BYTES_PER_PIXEL; ++i) {
                uint16_t addr = (baseAddr + i) & 0xFFFF;
                if (addr < activitySnap.executeCount.size()) {
                    activity += activitySnap.executeCount[addr];
                    activity += activitySnap.readCount[addr];
                    activity += activitySnap.writeCount[addr];
                }
            }

            mapPixels_[y * MAP_WIDTH + x] = getColorForAddress(
                baseAddr, regionType, maxActivity, activity);
        }
    }

    // Create or update texture
    if (mapTextureId_ == 0) {
        glGenTextures(1, &mapTextureId_);
        glBindTexture(GL_TEXTURE_2D, mapTextureId_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MAP_WIDTH, MAP_HEIGHT, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, mapPixels_.data());
    } else {
        glBindTexture(GL_TEXTURE_2D, mapTextureId_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MAP_WIDTH, MAP_HEIGHT,
                        GL_RGBA, GL_UNSIGNED_BYTE, mapPixels_.data());
    }

    needsRefresh_ = false;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void MemoryMapWindow::render(IDebugBackend &backend)
{
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(400, 450), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Memory Map", &visible_)) {
        ImGui::End();
        return;
    }

    // Toolbar
    if (ImGui::Button("Refresh")) {
        needsRefresh_ = true;
    }
    ImGui::SameLine();
    ImGui::Text("256x256 (each pixel = 256 bytes)");

    ImGui::Separator();

    // Rebuild map if needed
    if (needsRefresh_ || mapTextureId_ == 0) {
        rebuildMap(backend);
    }

    if (mapTextureId_ == 0) {
        ImGui::TextDisabled("(map not generated)");
        ImGui::End();
        return;
    }

    // Render the map image
    ImVec2 imageSize(static_cast<float>(MAP_WIDTH), static_cast<float>(MAP_HEIGHT));
    ImVec2 uv0(0, 0);
    ImVec2 uv1(1, 1);

    ImGui::Image(static_cast<ImTextureID>(static_cast<uint64_t>(mapTextureId_)),
                 imageSize, uv0, uv1);

    // Handle mouse interaction
    if (ImGui::IsItemHovered()) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        ImVec2 imageMin = ImGui::GetItemRectMin();
        float relX = mousePos.x - imageMin.x;
        float relY = mousePos.y - imageMin.y;

        int px = static_cast<int>(relX);
        int py = static_cast<int>(relY);

        if (px >= 0 && px < MAP_WIDTH && py >= 0 && py < MAP_HEIGHT) {
            hoverAddress_ = pixelToAddress(px, py);

            // Get symbol info for tooltip
            auto symbols = backend.symbolDatabase();
            const DebugSymbol *sym = symbols.findSymbol(hoverAddress_);

            // Tooltip
            ImGui::BeginTooltip();
            ImGui::Text("Address: %04X - %04X", hoverAddress_,
                        (hoverAddress_ + BYTES_PER_PIXEL - 1) & 0xFFFF);

            MemoryRegionType mrt = symbols.classify(hoverAddress_);
            const char *typeStr = "Unknown";
            if (mrt == MemoryRegionType::Code) typeStr = "Code";
            else if (mrt == MemoryRegionType::Data) typeStr = "Data";
            ImGui::Text("Type: %s", typeStr);

            if (sym) {
                ImGui::Text("Symbol: %s", sym->name.c_str());
                if (!sym->comment.empty()) {
                    ImGui::Text("%s", sym->comment.c_str());
                }
            }
            ImGui::EndTooltip();

            // Left click: popup menu
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                ImGui::OpenPopup("MapContextMenu");
                contextAddress_ = hoverAddress_;
            }

            // Right click: mark as
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                ImGui::OpenPopup("MapMarkMenu");
                contextAddress_ = hoverAddress_;
            }
        }
    }

    // Context menu (left click)
    if (ImGui::BeginPopup("MapContextMenu")) {
        ImGui::Text("Address: %04X", contextAddress_);
        ImGui::Separator();

        if (ImGui::MenuItem("Go to Memory Inspector")) {
            if (onGoToMemoryInspector) {
                onGoToMemoryInspector(contextAddress_);
            }
        }
        if (ImGui::MenuItem("Go to Disassembly")) {
            if (onGoToDisassembly) {
                onGoToDisassembly(contextAddress_);
            }
        }
        ImGui::EndPopup();
    }

    // Context menu (right click)
    if (ImGui::BeginPopup("MapMarkMenu")) {
        ImGui::Text("Mark %04X as:", contextAddress_);
        ImGui::Separator();

        if (ImGui::MenuItem("Code")) {
            auto &symbols = backend.symbolDatabase();
            symbols.setRegion(contextAddress_,
                             (contextAddress_ + BYTES_PER_PIXEL - 1) & 0xFFFF,
                             MemoryRegionType::Code);
            needsRefresh_ = true;
        }
        if (ImGui::MenuItem("Data")) {
            auto &symbols = backend.symbolDatabase();
            symbols.setRegion(contextAddress_,
                             (contextAddress_ + BYTES_PER_PIXEL - 1) & 0xFFFF,
                             MemoryRegionType::Data);
            needsRefresh_ = true;
        }
        if (ImGui::MenuItem("Unknown")) {
            auto &symbols = backend.symbolDatabase();
            symbols.removeRegion(contextAddress_);
            needsRefresh_ = true;
        }
        ImGui::EndPopup();
    }

    // Legend
    ImGui::Separator();
    ImGui::Text("Legend:");
    ImGui::SameLine();
    ImGui::ColorButton("##code", ImVec4(0.25f, 0.25f, 0.75f, 1.0f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
    ImGui::SameLine();
    ImGui::Text("Code");
    ImGui::SameLine();
    ImGui::ColorButton("##data", ImVec4(0.25f, 0.75f, 0.25f, 1.0f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
    ImGui::SameLine();
    ImGui::Text("Data");
    ImGui::SameLine();
    ImGui::ColorButton("##unknown", ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       ImGuiColorEditFlags_NoTooltip, ImVec2(16, 16));
    ImGui::SameLine();
    ImGui::Text("Unknown");

    ImGui::End();
}
