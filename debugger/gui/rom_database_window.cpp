#include "rom_database_window.h"
#include "idebug_backend.h"
#include "rdb_controller.h"
#include "rdb_types.h"

// Dear ImGui
#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>

// ---------------------------------------------------------------------------
// Type names for combo box
// ---------------------------------------------------------------------------

static const char *RDB_TYPE_NAMES[] = {
    "Function", "Variable", "Data", "Table", "String", "Code", "Label", "Unknown"
};
static const int RDB_TYPE_COUNT = 8;

static RdbObjectType indexToType(int idx)
{
    switch (idx) {
        case 0: return RdbObjectType::Function;
        case 1: return RdbObjectType::Variable;
        case 2: return RdbObjectType::Data;
        case 3: return RdbObjectType::Table;
        case 4: return RdbObjectType::String;
        case 5: return RdbObjectType::Code;
        case 6: return RdbObjectType::Label;
        default: return RdbObjectType::Unknown;
    }
}

// ---------------------------------------------------------------------------
// Cache refresh
// ---------------------------------------------------------------------------

void RomDatabaseWindow::refreshCache(const RdbController &rdb)
{
    cachedObjects_.clear();
    auto objects = rdb.listObjects();

    for (const auto &obj : objects) {
        CachedObject co;
        co.address = obj.address;
        co.typeStr = rdbObjectTypeToString(obj.type);
        co.name    = obj.name;
        co.sizeStr = obj.hasSize ? std::to_string(obj.size) : "-";
        co.comment = obj.comment;
        cachedObjects_.push_back(std::move(co));
    }

    needsRefresh_ = false;
}

bool RomDatabaseWindow::matchesFilter(const CachedObject &obj) const
{
    if (filterBuffer_[0] == '\0') return true;

    // Case-insensitive search in name
    std::string nameLower = obj.name;
    std::string filterLower = filterBuffer_;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
    std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
    if (nameLower.find(filterLower) != std::string::npos) return true;

    // Search by address hex
    char addrBuf[8];
    snprintf(addrBuf, sizeof(addrBuf), "%04X", obj.address);
    if (std::string(addrBuf).find(filterLower) != std::string::npos) return true;

    return false;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void RomDatabaseWindow::render(IDebugBackend &backend)
{
    if (!visible_) return;

    RdbController &rdb = backend.rdbController();

    // Refresh cache if needed
    if (needsRefresh_) {
        refreshCache(rdb);
    }

    // Window title with dirty indicator
    std::string title = "ROM Database";
    if (rdb.isDirty()) {
        title += " *";
    }

    ImGui::SetNextWindowSize(ImVec2(700, 400), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(title.c_str(), &visible_)) {
        ImGui::End();
        return;
    }

    // -- Toolbar -------------------------------------------------------------

    if (ImGui::Button("Add Object")) {
        showAddDialog_ = true;
        addAddrBuffer_[0] = '\0';
        addNameBuffer_[0] = '\0';
        addTypeIndex_ = 0;
    }
    ImGui::SameLine();
    if (rdb.isDirty()) {
        if (ImGui::Button("Save")) {
            rdb.save();
            needsRefresh_ = true;
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Save");
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::Text("Filter:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    if (ImGui::InputText("##filter", filterBuffer_, sizeof(filterBuffer_))) {
        // filter changed — no explicit action needed, rendering uses it
    }

    // ROM identity info
    auto identity = rdb.getRomIdentity();
    if (!identity.file.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu objects, %s)", rdb.objectCount(),
                            rdb.existsOnDisk() ? "on disk" : "in memory");
    }

    ImGui::Separator();

    // -- Add Object Dialog ---------------------------------------------------

    if (showAddDialog_) {
        ImGui::OpenPopup("Add RDB Object");
        showAddDialog_ = false;
    }

    if (ImGui::BeginPopupModal("Add RDB Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Address (hex)", addAddrBuffer_, sizeof(addAddrBuffer_));
        ImGui::InputText("Name", addNameBuffer_, sizeof(addNameBuffer_));
        ImGui::Combo("Type", &addTypeIndex_, RDB_TYPE_NAMES, RDB_TYPE_COUNT);

        if (ImGui::Button("Add", ImVec2(120, 0))) {
            unsigned int addr = 0;
            if (sscanf(addAddrBuffer_, "%x", &addr) == 1 && addr <= 0xFFFF && addNameBuffer_[0] != '\0') {
                RdbObject obj;
                obj.address = static_cast<uint16_t>(addr);
                obj.type = indexToType(addTypeIndex_);
                obj.name = addNameBuffer_;
                rdb.addObject(obj);
                needsRefresh_ = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // -- Object Table --------------------------------------------------------

    ImGui::BeginChild("RdbListScroll", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_None);

    // Table header
    if (ImGui::BeginTable("RdbTable", 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size",    ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthStretch);

        // Manual header row with clickable sort headers
        static const char *colNames[] = {"Address", "Type", "Name", "Size", "Comment"};
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int column = 0; column < 5; column++) {
            ImGui::TableSetColumnIndex(column);
            std::string label = std::string(colNames[column]);
            if (sortColumn_ == column) {
                label += sortReverse_ ? " \xe2\x96\xbc" : " \xe2\x96\xb2"; // ▼ or ▲
            }
            if (ImGui::Selectable(label.c_str(), sortColumn_ == column))
            {
                if (sortColumn_ == column) {
                    sortReverse_ = !sortReverse_;
                } else {
                    sortColumn_ = column;
                    sortReverse_ = false;
                }
            }
        }

        // Filter and sort
        std::vector<const CachedObject*> filtered;
        for (const auto &obj : cachedObjects_) {
            if (matchesFilter(obj)) {
                filtered.push_back(&obj);
            }
        }

        auto sizeToInt = [](const std::string &s) -> int {
            if (s.empty() || s == "-") return 0;
            try { return std::stoi(s); } catch (...) { return 0; }
        };

        auto compareCols = [&sizeToInt](const CachedObject *a, const CachedObject *b, int col) {
            switch (col) {
                case 0: return a->address < b->address;
                case 1: return a->typeStr < b->typeStr;
                case 2: return a->name < b->name;
                case 3: return sizeToInt(a->sizeStr) < sizeToInt(b->sizeStr);
                case 4: return a->comment < b->comment;
                default: return a->address < b->address;
            }
        };

        std::stable_sort(filtered.begin(), filtered.end(),
            [this, &compareCols](const CachedObject *a, const CachedObject *b) {
                // Swap arguments for reverse — preserves strict weak ordering
                // for equal elements (unlike negating the result).
                return sortReverse_
                    ? compareCols(b, a, sortColumn_)
                    : compareCols(a, b, sortColumn_);
            });

        if (filtered.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("(no objects)");
        }

        for (const auto *obj : filtered) {
            ImGui::PushID(obj->address);
            ImGui::TableNextRow();

            // Address column
            ImGui::TableNextColumn();
            char addrBuf[16];
            snprintf(addrBuf, sizeof(addrBuf), "%04X", obj->address);
            bool isSelected = (contextAddress_ == obj->address);
            if (ImGui::Selectable(addrBuf, isSelected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
            {
                if (ImGui::IsMouseDoubleClicked(0) && onGoToDisassembly) {
                    onGoToDisassembly(obj->address);
                }
            }

            // Context menu
            if (ImGui::BeginPopupContextItem("rdbctx")) {
                contextAddress_ = obj->address;

                if (ImGui::MenuItem("Rename")) {
                    editingName_ = true;
                    editingAddress_ = contextAddress_;
                    snprintf(editNameBuffer_, sizeof(editNameBuffer_), "%s", obj->name.c_str());
                }
                if (ImGui::MenuItem("Edit Comment")) {
                    editingComment_ = true;
                    editingAddress_ = contextAddress_;
                    snprintf(editCommentBuffer_, sizeof(editCommentBuffer_), "%s", obj->comment.c_str());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Go to Disassembly")) {
                    if (onGoToDisassembly) onGoToDisassembly(obj->address);
                }
                if (ImGui::MenuItem("Go to Memory")) {
                    if (onGoToMemoryInspector) onGoToMemoryInspector(obj->address);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Set Breakpoint")) {
                    backend.addBreakpoint(obj->address);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Remove Object")) {
                    rdb.removeObject(obj->address);
                    needsRefresh_ = true;
                }
                ImGui::EndPopup();
            }

            // Type column
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(obj->typeStr.c_str());

            // Name column (inline editing)
            ImGui::TableNextColumn();
            if (editingName_ && editingAddress_ == obj->address) {
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##editname", editNameBuffer_, sizeof(editNameBuffer_),
                        ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    if (editNameBuffer_[0] != '\0') {
                        const RdbObject *existing = rdb.getObject(obj->address);
                        if (existing) {
                            RdbObject updated = *existing;
                            updated.name = editNameBuffer_;
                            rdb.updateObject(updated);
                            needsRefresh_ = true;
                        }
                    }
                    editingName_ = false;
                }
                // Cancel on Escape or focus loss
                if (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) {
                    editingName_ = false;
                }
            } else {
                ImGui::TextUnformatted(obj->name.c_str());
            }

            // Size column
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(obj->sizeStr.c_str());

            // Comment column (inline editing)
            ImGui::TableNextColumn();
            if (editingComment_ && editingAddress_ == obj->address) {
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText("##editcomment", editCommentBuffer_, sizeof(editCommentBuffer_),
                        ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    rdb.setComment(obj->address, editCommentBuffer_);
                    needsRefresh_ = true;
                    editingComment_ = false;
                }
                if (ImGui::IsItemDeactivated() && !ImGui::IsItemActive()) {
                    editingComment_ = false;
                }
            } else {
                if (obj->comment.empty()) {
                    ImGui::TextDisabled("-");
                } else {
                    ImGui::TextUnformatted(obj->comment.c_str());
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
    ImGui::End();
}
