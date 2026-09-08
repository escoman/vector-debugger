#pragma once

#include "call_graph_model.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

// Forward declarations
class IDebugBackend;
namespace ax { namespace NodeEditor { struct EditorContext; } }

// ---------------------------------------------------------------------------
// Call Graph Window — Stage 6.12
//
// Visual node graph built from RDB objects + links.
// Uses imgui-node-editor for rendering.
//
// Graph is built only on explicit "Build" button press.
// RDB is the sole data source. Layout is not saved to RDB.
// ---------------------------------------------------------------------------

class CallGraphWindow
{
public:
    CallGraphWindow();
    ~CallGraphWindow();

    void render(IDebugBackend &backend);

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }
    bool &getVisibleRef() { return visible_; }

    // Mark graph as outdated (called when RDB changes externally).
    void markOutdated() { outdated_ = true; }

    // Navigation callbacks (same pattern as other windows)
    std::function<void(uint16_t address)> onGoToDisassembly;
    std::function<void(uint16_t address)> onGoToMemoryInspector;

private:
    bool visible_ = true;

    // Graph state
    enum class State { NotBuilt, Built, Outdated, Building };
    State state_ = State::NotBuilt;
    bool  outdated_ = false;

    // Current graph model (read-only snapshot after Build)
    CallGraphModel model_;

    // imgui-node-editor context
    ax::NodeEditor::EditorContext *editorContext_ = nullptr;

    // Node ID assignment: address → unique node editor ID
    std::unordered_map<uint16_t, uint32_t> nodeIdMap_;

    // Currently selected node for context menu
    uint16_t selectedNodeAddress_ = 0;

    // Navigation callbacks (set by GUI for right-click menu)
    // These are resolved at render time via backend.
    uint16_t pendingGoToDisassembly_ = 0;
    uint16_t pendingGoToMemory_      = 0;
    bool     hasPendingDisassembly_  = false;
    bool     hasPendingMemory_       = false;

    // Build the graph model from RDB
    void build(IDebugBackend &backend);

    // Render the node editor with all nodes and links
    void renderNodeEditor(IDebugBackend &backend);

    // Render a single node's content
    void renderNodeContent(const CallGraphNode &node);

    // Render context menu for a node
    void renderNodeContextMenu(const CallGraphNode &node);
};
