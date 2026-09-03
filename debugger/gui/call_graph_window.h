#pragma once

#include <cstdint>
#include <vector>
#include <map>
#include <set>

// Forward declarations
class DebugBackend;

// ---------------------------------------------------------------------------
// Call Graph Window — Stage 4.7
//
// Tree view of function call hierarchy.
// Built from SymbolDatabase::callGraph().
// ---------------------------------------------------------------------------

class CallGraphWindow
{
public:
    CallGraphWindow() = default;

    void render(DebugBackend &backend);

    void setVisible(bool v) { visible_ = v; }
    bool isVisible() const { return visible_; }
    void requestRefresh() { needsRefresh_ = true; }

private:
    bool visible_ = true;
    bool needsRefresh_ = true;

    // Call graph data: function -> list of called functions
    std::map<uint16_t, std::vector<uint16_t>> callGraph_;

    // Set of all known function addresses
    std::set<uint16_t> functionAddresses_;

    // Build the call graph from symbol database
    void buildCallGraph(DebugBackend &backend);

    // Render a node recursively
    void renderNode(uint16_t addr, DebugBackend &backend, int depth, std::set<uint16_t> &visited);
};
