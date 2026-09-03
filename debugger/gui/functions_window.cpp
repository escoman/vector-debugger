#include "functions_window.h"
#include "idebug_backend.h"
#include "symbol_database.h"

// Dear ImGui
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Helper: calculate function size
// ---------------------------------------------------------------------------

static uint16_t calculateFunctionSize(uint16_t addr, const SymbolDatabase &db)
{
    // Find the next function after this one
    auto allSymbols = db.allSymbols();
    uint16_t nextFuncAddr = 0xFFFF;
    bool found = false;

    for (const auto &sym : allSymbols) {
        if (sym.type == SymbolType::Function && sym.address > addr) {
            if (!found || sym.address < nextFuncAddr) {
                nextFuncAddr = sym.address;
                found = true;
            }
        }
    }

    if (found) {
        return nextFuncAddr - addr;
    }
    return 0;  // Unknown size
}

// ---------------------------------------------------------------------------
// Helper: count xrefs to an address
// ---------------------------------------------------------------------------

static int countXrefsTo(uint16_t addr, const SymbolDatabase &db)
{
    auto xrefs = db.xrefsTo(addr);
    return static_cast<int>(xrefs.size());
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void FunctionsWindow::render(IDebugBackend &backend)
{
    if (!visible_) return;

    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Functions", &visible_)) {
        ImGui::End();
        return;
    }

    // Toolbar
    if (ImGui::Button("Define Function")) {
        showDefineDialog_ = true;
        defineAddrBuffer_[0] = '\0';
        defineNameBuffer_[0] = '\0';
        defineCommentBuffer_[0] = '\0';
    }
    ImGui::SameLine();
    ImGui::Text("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputText("##search", searchBuffer_, sizeof(searchBuffer_))) {
        needsRefresh_ = true;
    }

    ImGui::Separator();

    // Get symbols
    auto allSymbols = backend.symbolDatabase();
    auto symbols = allSymbols.allSymbols();

    // Filter by search
    std::vector<DebugSymbol> filtered;
    for (const auto &sym : symbols) {
        if (searchBuffer_[0] != '\0') {
            // Case-insensitive search in name
            std::string nameLower = sym.name;
            std::string searchLower = searchBuffer_;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
            if (nameLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }
        filtered.push_back(sym);
    }

    // Sort
    if (sortColumn_ == 0) {
        // Sort by address
        std::sort(filtered.begin(), filtered.end(),
            [this](const DebugSymbol &a, const DebugSymbol &b) {
                return sortReverse_ ? (a.address > b.address) : (a.address < b.address);
            });
    } else if (sortColumn_ == 1) {
        // Sort by name
        std::sort(filtered.begin(), filtered.end(),
            [this](const DebugSymbol &a, const DebugSymbol &b) {
                return sortReverse_ ? (a.name > b.name) : (a.name < b.name);
            });
    }

    // Table
    ImGui::BeginChild("TableScroll", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);

    if (ImGui::BeginTable("FunctionsTable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Sortable |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {

        // Headers
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_DefaultSort, 80.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 150.0f);
        ImGui::TableSetupColumn("Size", 0, 60.0f);
        ImGui::TableSetupColumn("Calls", 0, 50.0f);
        ImGui::TableSetupColumn("Comment", 0, 200.0f);
        ImGui::TableHeadersRow();

        // Handle sorting
        if (ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs()) {
            if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0) {
                sortColumn_ = sortSpecs->Specs[0].ColumnIndex;
                sortReverse_ = (sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Descending);
                sortSpecs->SpecsDirty = false;
                needsRefresh_ = true;
            }
        }

        // Rows
        for (const auto &sym : filtered) {
            ImGui::TableNextRow();

            // Address column
            ImGui::TableSetColumnIndex(0);
            char addrBuf[16];
            snprintf(addrBuf, sizeof(addrBuf), "%04X", sym.address);
            ImGui::Text("%s", addrBuf);

            // Context menu for this row
            if (ImGui::BeginPopupContextItem()) {
                contextAddress_ = sym.address;

                if (ImGui::MenuItem("Rename")) {
                    editingName_ = true;
                    editingAddress_ = sym.address;
                    snprintf(editNameBuffer_, sizeof(editNameBuffer_), "%s", sym.name.c_str());
                }
                if (ImGui::MenuItem("Edit Comment")) {
                    editingComment_ = true;
                    editingAddress_ = sym.address;
                    snprintf(editCommentBuffer_, sizeof(editCommentBuffer_), "%s", sym.comment.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Go to Disassembly")) {
                    if (onGoToDisassembly) {
                        onGoToDisassembly(sym.address);
                    }
                }
                if (ImGui::MenuItem("Go to Memory Inspector")) {
                    if (onGoToMemoryInspector) {
                        onGoToMemoryInspector(sym.address);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete")) {
                    auto &db = backend.symbolDatabase();
                    db.removeSymbol(sym.address);
                    needsRefresh_ = true;
                }
                ImGui::EndPopup();
            }

            // Name column
            ImGui::TableSetColumnIndex(1);
            if (editingName_ && editingAddress_ == sym.address) {
                ImGui::SetNextItemWidth(-1);
                bool enterPressed = ImGui::InputText("##editname", editNameBuffer_,
                    sizeof(editNameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
                bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                if (enterPressed || escapePressed || !ImGui::IsItemActive()) {
                    if (enterPressed && editNameBuffer_[0] != '\0') {
                        auto &db = backend.symbolDatabase();
                        db.renameSymbol(sym.address, editNameBuffer_);
                        needsRefresh_ = true;
                    }
                    editingName_ = false;
                }
            } else {
                const char *typeStr = (sym.type == SymbolType::Function) ? "func" : "label";
                ImGui::TextColored(
                    sym.type == SymbolType::Function ? ImVec4(1.0f, 1.0f, 0.6f, 1.0f)
                                                      : ImVec4(0.6f, 1.0f, 0.6f, 1.0f),
                    "%s [%s]", sym.name.c_str(), typeStr);
            }

            // Size column
            ImGui::TableSetColumnIndex(2);
            if (sym.type == SymbolType::Function) {
                uint16_t size = calculateFunctionSize(sym.address, allSymbols);
                if (size > 0) {
                    ImGui::Text("%u", size);
                } else {
                    ImGui::TextDisabled("N/A");
                }
            } else {
                ImGui::TextDisabled("N/A");
            }

            // Calls column
            ImGui::TableSetColumnIndex(3);
            int calls = countXrefsTo(sym.address, allSymbols);
            ImGui::Text("%d", calls);

            // Comment column
            ImGui::TableSetColumnIndex(4);
            if (editingComment_ && editingAddress_ == sym.address) {
                ImGui::SetNextItemWidth(-1);
                bool enterPressed = ImGui::InputText("##editcomment", editCommentBuffer_,
                    sizeof(editCommentBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
                bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                if (enterPressed || escapePressed || !ImGui::IsItemActive()) {
                    if (enterPressed) {
                        auto &db = backend.symbolDatabase();
                        db.setComment(sym.address, editCommentBuffer_);
                        needsRefresh_ = true;
                    }
                    editingComment_ = false;
                }
            } else {
                if (sym.comment.empty()) {
                    ImGui::TextDisabled("-");
                } else {
                    ImGui::Text("%s", sym.comment.c_str());
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();

    // "Define Function" dialog
    if (showDefineDialog_) {
        ImGui::OpenPopup("Define Function Dialog");
        showDefineDialog_ = false;
    }

    if (ImGui::BeginPopupModal("Define Function Dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Address (hex):");
        ImGui::InputText("##addr", defineAddrBuffer_, sizeof(defineAddrBuffer_),
            ImGuiInputTextFlags_CharsHexadecimal);

        ImGui::Text("Name:");
        ImGui::InputText("##name", defineNameBuffer_, sizeof(defineNameBuffer_));

        ImGui::Text("Comment:");
        ImGui::InputText("##comment", defineCommentBuffer_, sizeof(defineCommentBuffer_));

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            unsigned int addr = 0;
            if (sscanf(defineAddrBuffer_, "%x", &addr) == 1 && addr <= 0xFFFF) {
                if (defineNameBuffer_[0] != '\0') {
                    auto &db = backend.symbolDatabase();
                    db.addSymbol(static_cast<uint16_t>(addr), defineNameBuffer_, SymbolType::Function);
                    if (defineCommentBuffer_[0] != '\0') {
                        db.setComment(static_cast<uint16_t>(addr), defineCommentBuffer_);
                    }
                    needsRefresh_ = true;
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}
