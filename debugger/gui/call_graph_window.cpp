#include "call_graph_window.h"
#include "idebug_backend.h"
#include "rdb_controller.h"

// imgui-node-editor
#include "imgui_node_editor.h"

// Dear ImGui
#include "imgui.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

namespace ed = ax::NodeEditor;

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------

static constexpr float NODE_WIDTH       = 200.0f;
static constexpr float NODE_H_SPACING   = 60.0f;
static constexpr float NODE_V_SPACING   = 80.0f;
static constexpr int   NODES_PER_ROW    = 5;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

CallGraphWindow::CallGraphWindow()
{
    ed::Config config;
    config.SettingsFile = nullptr;  // no persistent settings (Stage 6.12 §12,13)
    editorContext_ = ed::CreateEditor(&config);
}

CallGraphWindow::~CallGraphWindow()
{
    if (editorContext_)
        ed::DestroyEditor(editorContext_);
}

// ---------------------------------------------------------------------------
// Build — populate Graph Model from RDB snapshot
// ---------------------------------------------------------------------------

void CallGraphWindow::build(IDebugBackend &backend)
{
    state_ = State::Building;

    // Get RDB snapshot (read-only, thread-safe via existing mechanism)
    const RdbController &rdb = backend.rdbController();
    std::vector<RdbObject> objects = rdb.listObjects();

    // Build graph model — O(N + E)
    model_ = buildCallGraphModel(objects);

    // Assign unique node editor IDs (1-based, address → sequential ID)
    nodeIdMap_.clear();
    nodeIdMap_.reserve(model_.nodes.size());
    for (size_t i = 0; i < model_.nodes.size(); i++)
    {
        nodeIdMap_[model_.nodes[i].address] = static_cast<uint32_t>(i + 1);
    }

    outdated_ = false;
    state_ = State::Built;
}

// ---------------------------------------------------------------------------
// Deterministic layout — arrange nodes in a grid
// ---------------------------------------------------------------------------

static void applyGridLayout(const CallGraphModel &model,
                             const std::unordered_map<uint16_t, uint32_t> &nodeIdMap)
{
    // Sort nodes by address for deterministic ordering
    std::vector<uint16_t> sortedAddresses;
    sortedAddresses.reserve(model.nodes.size());
    for (const auto &node : model.nodes)
        sortedAddresses.push_back(node.address);
    std::sort(sortedAddresses.begin(), sortedAddresses.end());

    // Place nodes in grid
    float cellW = NODE_WIDTH + NODE_H_SPACING;
    float cellH = 120.0f + NODE_V_SPACING;

    for (size_t i = 0; i < sortedAddresses.size(); i++)
    {
        int row = static_cast<int>(i) / NODES_PER_ROW;
        int col = static_cast<int>(i) % NODES_PER_ROW;

        float x = 50.0f + col * cellW;
        float y = 50.0f + row * cellH;

        // Set node position via imgui-node-editor API
        ed::SetNodePosition(ed::NodeId(nodeIdMap.at(sortedAddresses[i])),
                            ImVec2(x, y));
    }
}

// ---------------------------------------------------------------------------
// Render node content
// ---------------------------------------------------------------------------

void CallGraphWindow::renderNodeContent(const CallGraphNode &node)
{
    if (node.unresolved)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "<unresolved>");
        ImGui::Text("%04X", node.address);
    }
    else
    {
        // Name
        if (!node.name.empty())
            ImGui::TextUnformatted(node.name.c_str());
        else
            ImGui::Text("%04X", node.address);

        // Address
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "0x%04X", node.address);

        // Type
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "%s",
                           rdbObjectTypeToString(node.type));
    }
}

// ---------------------------------------------------------------------------
// Render context menu for a node
// ---------------------------------------------------------------------------

void CallGraphWindow::renderNodeContextMenu(const CallGraphNode &node)
{
    char label[128];
    snprintf(label, sizeof(label), "Go to Disassembly (%04X)", node.address);
    if (ImGui::MenuItem(label))
    {
        pendingGoToDisassembly_ = node.address;
        hasPendingDisassembly_ = true;
    }

    snprintf(label, sizeof(label), "Go to Memory (%04X)", node.address);
    if (ImGui::MenuItem(label))
    {
        pendingGoToMemory_ = node.address;
        hasPendingMemory_ = true;
    }

    // Additional info
    ImGui::Separator();
    if (!node.comment.empty())
        ImGui::TextDisabled("Comment: %s", node.comment.c_str());
    if (node.hasSize)
        ImGui::TextDisabled("Size: %u", node.size);
    if (!node.properties.empty())
    {
        ImGui::TextDisabled("Properties:");
        for (const auto &kv : node.properties)
            ImGui::TextDisabled("  %s: %s", kv.first.c_str(),
                                kv.second.stringValue.c_str());
    }
}

// ---------------------------------------------------------------------------
// Render node editor
// ---------------------------------------------------------------------------

void CallGraphWindow::renderNodeEditor(IDebugBackend &backend)
{
    ed::SetCurrentEditor(editorContext_);
    ed::Begin("Call Graph Editor", ImVec2(0.0f, 0.0f));

    // --- Draw nodes -------------------------------------------------------

    for (const auto &node : model_.nodes)
    {
        uint32_t id = nodeIdMap_.at(node.address);
        ed::BeginNode(ed::NodeId(id));

        renderNodeContent(node);

        // Input pin (for incoming edges)
        ed::BeginPin(ed::PinId(id * 1000), ed::PinKind::Input);
        ImGui::Text(" in");
        ed::EndPin();

        ImGui::SameLine();

        // Output pin (for outgoing edges)
        ed::BeginPin(ed::PinId(id * 1000 + 1), ed::PinKind::Output);
        ImGui::Text("out ");
        ed::EndPin();

        ed::EndNode();

        // Context menu on right-click
        ed::NodeId contextNodeId;
        if (ed::ShowNodeContextMenu(&contextNodeId))
        {
            // Find which node was right-clicked
            uint32_t clickedId = static_cast<uint32_t>(static_cast<uintptr_t>(contextNodeId));
            for (const auto &n : model_.nodes)
            {
                if (nodeIdMap_.count(n.address) && nodeIdMap_.at(n.address) == clickedId)
                {
                    selectedNodeAddress_ = n.address;
                    ImGui::OpenPopup("node_context");
                    break;
                }
            }
        }
    }

    // Render context menu popup if open
    if (ImGui::IsPopupOpen("node_context"))
    {
        if (ImGui::BeginPopup("node_context"))
        {
            auto it = model_.addressToIndex.find(selectedNodeAddress_);
            if (it != model_.addressToIndex.end())
            {
                const auto &node = model_.nodes[it->second];
                renderNodeContextMenu(node);
            }
            ImGui::EndPopup();
        }
    }

    // --- Draw links -------------------------------------------------------

    for (size_t i = 0; i < model_.edges.size(); i++)
    {
        const auto &edge = model_.edges[i];

        auto srcIt = nodeIdMap_.find(edge.source);
        auto tgtIt = nodeIdMap_.find(edge.target);
        if (srcIt == nodeIdMap_.end() || tgtIt == nodeIdMap_.end())
            continue;

        uint32_t srcId = srcIt->second;
        uint32_t tgtId = tgtIt->second;

        // Output pin of source → Input pin of target
        ed::Link(ed::LinkId(static_cast<uint32_t>(i + 1)),
                 ed::PinId(srcId * 1000 + 1),  // output pin
                 ed::PinId(tgtId * 1000));      // input pin
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void CallGraphWindow::render(IDebugBackend &backend)
{
    if (!visible_) return;

    // Window title with state indicator
    std::string title = "Call Graph";
    if (state_ == State::Outdated)
        title += " — outdated";
    else if (state_ == State::NotBuilt)
        title += " (not built)";

    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(title.c_str(), &visible_))
    {
        ImGui::End();
        return;
    }

    // Check if RDB changed externally → mark outdated
    if (state_ == State::Built && outdated_)
    {
        state_ = State::Outdated;
    }

    // --- Toolbar ----------------------------------------------------------

    if (ImGui::Button("Build"))
    {
        build(backend);
        // Apply layout after build
        applyGridLayout(model_, nodeIdMap_);
    }

    ImGui::SameLine();

    // Statistics
    if (state_ == State::Built || state_ == State::Outdated)
    {
        ImGui::Text("Nodes: %zu  |  Links: %zu  |  Unresolved: %zu",
                    model_.nodes.size(), model_.edges.size(), model_.unresolvedCount);
    }

    ImGui::Separator();

    // --- Content based on state -------------------------------------------

    switch (state_)
    {
    case State::NotBuilt:
        ImGui::TextDisabled("No graph built.");
        ImGui::TextDisabled("Click 'Build' to create Call Graph from RDB.");
        break;

    case State::Building:
        ImGui::TextDisabled("Building...");
        break;

    case State::Built:
    case State::Outdated:
        if (model_.nodes.empty())
        {
            ImGui::TextDisabled("No objects in RDB.");
        }
        else
        {
            renderNodeEditor(backend);
        }
        break;
    }

    ImGui::End();

    // --- Process pending navigation (outside ImGui::Begin/End) ------------

    if (hasPendingDisassembly_ && onGoToDisassembly)
    {
        onGoToDisassembly(pendingGoToDisassembly_);
        hasPendingDisassembly_ = false;
    }
    if (hasPendingMemory_ && onGoToMemoryInspector)
    {
        onGoToMemoryInspector(pendingGoToMemory_);
        hasPendingMemory_ = false;
    }
}
