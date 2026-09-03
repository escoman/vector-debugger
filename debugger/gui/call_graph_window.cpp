#include "call_graph_window.h"
#include "idebug_backend.h"
#include "symbol_database.h"

// Dear ImGui
#include "imgui.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// Build call graph
// ---------------------------------------------------------------------------

void CallGraphWindow::buildCallGraph(IDebugBackend &backend)
{
    callGraph_.clear();
    functionAddresses_.clear();

    auto &symbols = backend.symbolDatabase();
    auto allSymbols = symbols.allSymbols();

    // Collect all function addresses
    for (const auto &sym : allSymbols) {
        if (sym.type == SymbolType::Function) {
            functionAddresses_.insert(sym.address);
        }
    }

    // Get call edges
    auto edges = symbols.callGraph();

    // Build adjacency list
    for (const auto &edge : edges) {
        callGraph_[edge.from].push_back(edge.to);
    }

    needsRefresh_ = false;
}

// ---------------------------------------------------------------------------
// Render node recursively
// ---------------------------------------------------------------------------

void CallGraphWindow::renderNode(uint16_t addr, IDebugBackend &backend, int depth,
                                  std::set<uint16_t> &visited)
{
    auto &symbols = backend.symbolDatabase();

    // Get display name
    std::string name = symbols.displayName(addr);
    if (name.empty()) {
        char buf[16];
        snprintf(buf, sizeof(buf), "sub_%04X", addr);
        name = buf;
    }

    // Check for recursion
    bool isRecursive = (visited.find(addr) != visited.end());

    // Create tree node
    ImGui::PushID(addr);

    char label[128];
    snprintf(label, sizeof(label), "%04X: %s", addr, name.c_str());

    bool hasChildren = (callGraph_.find(addr) != callGraph_.end());

    if (isRecursive) {
        // Show as leaf with recursion indicator
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s (recursive)", label);
    } else if (hasChildren) {
        if (ImGui::TreeNode(label)) {
            visited.insert(addr);

            // Render children
            for (uint16_t child : callGraph_[addr]) {
                renderNode(child, backend, depth + 1, visited);
            }

            visited.erase(addr);
            ImGui::TreePop();
        }
    } else {
        // Leaf node
        ImGui::Text("%s", label);
    }

    ImGui::PopID();
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void CallGraphWindow::render(IDebugBackend &backend)
{
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Call Graph", &visible_)) {
        ImGui::End();
        return;
    }

    // Toolbar
    if (ImGui::Button("Refresh")) {
        needsRefresh_ = true;
    }

    ImGui::Separator();

    // Rebuild if needed
    if (needsRefresh_) {
        buildCallGraph(backend);
    }

    if (functionAddresses_.empty()) {
        ImGui::TextDisabled("(no functions defined)");
        ImGui::TextDisabled("Use 'Define Function' in Disassembly or Functions window");
        ImGui::End();
        return;
    }

    ImGui::Text("Functions: %zu", functionAddresses_.size());
    ImGui::Separator();

    // Find root functions (functions that are not called by others)
    std::set<uint16_t> calledFunctions;
    for (const auto &pair : callGraph_) {
        for (uint16_t target : pair.second) {
            calledFunctions.insert(target);
        }
    }

    std::vector<uint16_t> rootFunctions;
    for (uint16_t addr : functionAddresses_) {
        if (calledFunctions.find(addr) == calledFunctions.end()) {
            rootFunctions.push_back(addr);
        }
    }

    // If no roots found, show all functions
    if (rootFunctions.empty()) {
        rootFunctions.assign(functionAddresses_.begin(), functionAddresses_.end());
    }

    // Render tree
    ImGui::BeginChild("CallGraphScroll", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);

    for (uint16_t root : rootFunctions) {
        std::set<uint16_t> visited;
        renderNode(root, backend, 0, visited);
    }

    ImGui::EndChild();

    ImGui::End();
}
