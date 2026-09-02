#include "memory_inspector_window.h"
#include "disassembler.h"

// Dear ImGui
#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void MemoryInspectorWindow::render(DebugBackend &backend)
{
    if (!visible_) return;
    
    // Window flags: movable, resizable, with title bar
    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    
    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin("Memory Inspector", &visible_, flags)) {
        ImGui::End();
        return;
    }
    
    // Refresh snapshot if needed
    if (needsRefresh_) {
        refreshSnapshot(backend);
        needsRefresh_ = false;
    }
    
    // Render toolbar (address input, Go, Follow PC, Refresh)
    renderToolbar(backend);
    
    ImGui::Separator();
    
    // Render memory view (hex + ASCII)
    renderMemoryView(backend);
    
    ImGui::Separator();
    
    // Render disassembly of selected address
    renderDisassembly(backend);
    
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void MemoryInspectorWindow::renderToolbar(DebugBackend &backend)
{
    // Byte editing mode (Stage 3.5)
    if (editingByte_) {
        ImGui::Text("Edit %04X:", editAddress_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(50);
        bool enterPressed = ImGui::InputText("##editbyte", editBuffer_, sizeof(editBuffer_),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SameLine();
        if (ImGui::Button("Go") || enterPressed) {
            confirmEdit(backend);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            cancelEdit();
        }
        if (writeFailed_) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Write failed: CPU must be Paused");
        }
        return;  // Skip normal toolbar while editing
    }
    
    writeFailed_ = false;
    // Address input
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputText("##addr", addressInput_, sizeof(addressInput_), 
                         ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
        uint16_t addr;
        if (parseAddress(addressInput_, addr)) {
            address_ = addr;
            selectedAddress_ = addr;
        }
    }
    
    ImGui::SameLine();
    
    // Go button
    if (ImGui::Button("Go")) {
        uint16_t addr;
        if (parseAddress(addressInput_, addr)) {
            address_ = addr;
            selectedAddress_ = addr;
        }
    }
    
    ImGui::SameLine();
    
    // Follow PC button
    if (ImGui::Button("Follow PC")) {
        CpuState cpu = backend.getCpuState();
        address_ = cpu.pc;
        selectedAddress_ = cpu.pc;
        snprintf(addressInput_, sizeof(addressInput_), "%04X", cpu.pc);
    }
    
    ImGui::SameLine();
    
    // Refresh button
    if (ImGui::Button("Refresh")) {
        needsRefresh_ = true;
    }
    
    ImGui::SameLine();
    ImGui::Text("Address: %04X  Selected: %04X", address_, selectedAddress_);
}

// ---------------------------------------------------------------------------
// Memory View (hex + ASCII)
// ---------------------------------------------------------------------------

void MemoryInspectorWindow::renderMemoryView(DebugBackend &backend)
{
    if (snapshot_.data.empty()) {
        ImGui::Text("No data. Click Refresh to load memory.");
        return;
    }
    
    // Get current PC for highlighting
    CpuState cpu = backend.getCpuState();
    uint16_t pc = cpu.pc;
    
    // Calculate visible range
    const int bytesPerLine = 16;
    const size_t totalBytes = snapshot_.data.size();
    const size_t totalLines = (totalBytes + bytesPerLine - 1) / bytesPerLine;
    
    // Character width for click calculation (monospace font)
    float charWidth = ImGui::CalcTextSize("M").x;
    if (charWidth < 1.0f) charWidth = 8.0f;
    
    // Child window for scrolling
    ImGui::BeginChild("MemoryScroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 4), 
                      ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    
    // Use clipper for performance with large memory ranges
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(totalLines));
    
    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
            size_t lineOffset = static_cast<size_t>(line) * bytesPerLine;
            uint16_t lineAddr = static_cast<uint16_t>((snapshot_.start + lineOffset) & 0xFFFF);
            
            // Check if this line contains PC
            bool containsPc = false;
            for (int i = 0; i < bytesPerLine; ++i) {
                size_t offset = lineOffset + i;
                if (offset >= totalBytes) break;
                uint16_t addr = static_cast<uint16_t>((snapshot_.start + offset) & 0xFFFF);
                if (addr == pc) containsPc = true;
            }
            
            // Check if this line contains a breakpoint
            bool containsBp = false;
            for (int i = 0; i < bytesPerLine; ++i) {
                size_t offset = lineOffset + i;
                if (offset >= totalBytes) break;
                uint16_t addr = static_cast<uint16_t>((snapshot_.start + offset) & 0xFFFF);
                if (backend.hasBreakpoint(addr)) containsBp = true;
            }

            // Highlight line with PC
            if (containsPc) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.5f, 1.0f));
            }
            
            // Make line selectable (for click-to-select-row)
            ImGui::PushID(line);
            
            // Address column with breakpoint marker (Stage 3.7)
            char lineBuf[256];
            int pos = 0;
            if (containsBp) {
                pos += snprintf(lineBuf, sizeof(lineBuf), "\xe2\x97\x8f ");  // red dot ●
            } else {
                pos += snprintf(lineBuf, sizeof(lineBuf), "  ");
            }
            pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "%04X: ", lineAddr);
            
            // Hex column
            for (int i = 0; i < bytesPerLine; ++i) {
                size_t offset = lineOffset + i;
                if (offset >= totalBytes) {
                    pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "   ");
                } else {
                    uint8_t byte = snapshot_.data[offset];
                    pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, "%02X ", byte);
                }
            }
            
            // Separator
            pos += snprintf(lineBuf + pos, sizeof(lineBuf) - pos, " |");
            
            // ASCII column
            for (int i = 0; i < bytesPerLine; ++i) {
                size_t offset = lineOffset + i;
                if (offset >= totalBytes) {
                    lineBuf[pos++] = ' ';
                } else {
                    uint8_t byte = snapshot_.data[offset];
                    if (byte >= 0x20 && byte <= 0x7E) {
                        lineBuf[pos++] = static_cast<char>(byte);
                    } else {
                        lineBuf[pos++] = '.';
                    }
                }
            }
            lineBuf[pos++] = '|';
            lineBuf[pos] = '\0';
            
            // Render the full line as selectable
            bool lineClicked = ImGui::Selectable(lineBuf, false, ImGuiSelectableFlags_AllowDoubleClick);
            
            if (lineClicked) {
                // Calculate which byte was clicked based on mouse X position
                ImVec2 textStart = ImGui::GetItemRectMin();
                float mouseX = ImGui::GetIO().MousePos.x;
                float offset = mouseX - textStart.x;
                
                // Address field: "XXXX: " = 6 chars
                float addrWidth = 6.0f * charWidth;
                
                if (offset >= addrWidth) {
                    float byteOffset = offset - addrWidth;
                    // Each byte cell: "XX " = 3 chars wide
                    float cellWidth = 3.0f * charWidth;
                    int byteIndex = static_cast<int>(byteOffset / cellWidth);
                    byteIndex = std::max(0, std::min(byteIndex, bytesPerLine - 1));
                    
                    size_t clickOffset = lineOffset + byteIndex;
                    if (clickOffset < totalBytes) {
                        uint16_t clickedAddr = static_cast<uint16_t>((snapshot_.start + clickOffset) & 0xFFFF);
                        selectedAddress_ = clickedAddr;
                        
                        if (ImGui::IsMouseDoubleClicked(0)) {
                            beginEditByte(clickedAddr);
                        }
                    }
                }
            }
            
            // Right-click context menu for breakpoints (Stage 3.7) + navigation (Stage 3.9)
            if (ImGui::BeginPopupContextItem("bpctx")) {
                // Use the selectedAddress_ as the target
                if (backend.hasBreakpoint(selectedAddress_)) {
                    if (ImGui::MenuItem("Remove Breakpoint")) {
                        backend.removeBreakpoint(selectedAddress_);
                    }
                } else {
                    if (ImGui::MenuItem("Set Breakpoint")) {
                        backend.addBreakpoint(selectedAddress_);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Go to Disassembly")) {
                    if (onGoToDisassembly) {
                        onGoToDisassembly(selectedAddress_);
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::PopID();
            
            if (containsPc) {
                ImGui::PopStyleColor();
            }
        }
    }
    clipper.End();
    
    // Stage 3.9: real scroll to selected address
    if (pendingScroll_) {
        // Calculate which line contains selectedAddress_
        size_t offset = static_cast<size_t>((selectedAddress_ - snapshot_.start) & 0xFFFF);
        int targetLine = static_cast<int>(offset / bytesPerLine);
        float lineY = targetLine * ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetScrollY(lineY - ImGui::GetWindowHeight() * 0.4f);
        pendingScroll_ = false;
    }
    
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Disassembly
// ---------------------------------------------------------------------------

void MemoryInspectorWindow::renderDisassembly(DebugBackend &backend)
{
    ImGui::Text("Disassembly at %04X:", selectedAddress_);
    
    // Create read function for disassembler
    auto readFn = [&backend](uint16_t addr) -> uint8_t {
        return backend.readMemory(addr);
    };
    
    // Disassemble a few instructions starting from selected address
    uint16_t addr = selectedAddress_;
    for (int i = 0; i < 5; ++i) {
        DisassembledInstruction instr = disassemble(addr, readFn);
        
        // Format: "ADDR: BYTES  MNEMONIC OPERANDS"
        char buf[128];
        int pos = snprintf(buf, sizeof(buf), "  %04X: ", instr.address);
        
        // Bytes
        for (int j = 0; j < instr.length; ++j) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "%02X ", instr.bytes[j]);
        }
        // Pad to align mnemonics
        for (int j = instr.length; j < 3; ++j) {
            pos += snprintf(buf + pos, sizeof(buf) - pos, "   ");
        }
        
        // Mnemonic + operands
        pos += snprintf(buf + pos, sizeof(buf) - pos, " %s", instr.text.c_str());
        
        ImGui::Text("%s", buf);
        
        // Advance to next instruction
        addr = static_cast<uint16_t>((addr + instr.length) & 0xFFFF);
    }
}

// ---------------------------------------------------------------------------
// Snapshot management
// ---------------------------------------------------------------------------

void MemoryInspectorWindow::refreshSnapshot(DebugBackend &backend)
{
    // Load entire 64 KB
    snapshot_ = backend.readMemorySnapshot(0x0000, 0x10000);
}

// ---------------------------------------------------------------------------
// Address parsing
// ---------------------------------------------------------------------------

bool MemoryInspectorWindow::parseAddress(const char *input, uint16_t &address) const
{
    if (!input || !*input) return false;
    
    // Skip leading whitespace
    while (*input && std::isspace(*input)) ++input;
    
    // Check for 0x prefix
    bool hasPrefix = false;
    if (input[0] == '0' && (input[1] == 'x' || input[1] == 'X')) {
        input += 2;
        hasPrefix = true;
    }
    
    // Parse hex digits
    unsigned long value = 0;
    int digits = 0;
    while (*input && std::isxdigit(*input)) {
        char c = *input++;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;
        
        value = (value << 4) | digit;
        ++digits;
    }
    
    // Must have at least one digit
    if (digits == 0) return false;
    
    // Must fit in 16 bits
    if (value > 0xFFFF) return false;
    
    address = static_cast<uint16_t>(value);
    return true;
}

// ---------------------------------------------------------------------------
// Byte editing (Stage 3.5)
// ---------------------------------------------------------------------------

void MemoryInspectorWindow::beginEditByte(uint16_t address)
{
    editingByte_ = true;
    editAddress_ = address;
    writeFailed_ = false;
    
    // Pre-fill with current value from snapshot
    size_t offset = static_cast<size_t>((address - snapshot_.start) & 0xFFFF);
    if (offset < snapshot_.data.size()) {
        snprintf(editBuffer_, sizeof(editBuffer_), "%02X", snapshot_.data[offset]);
    } else {
        editBuffer_[0] = '\0';
    }
}

void MemoryInspectorWindow::confirmEdit(DebugBackend &backend)
{
    // Parse hex value
    unsigned int value = 0;
    if (sscanf(editBuffer_, "%x", &value) != 1 || value > 0xFF) {
        cancelEdit();
        return;
    }
    
    uint8_t byte = static_cast<uint8_t>(value);
    
    // Write through backend (goes through emulation thread command protocol)
    bool ok = backend.writeMemoryByte(editAddress_, byte);
    
    if (ok) {
        // Write succeeded — refresh snapshot to show new value
        cancelEdit();
        needsRefresh_ = true;
    } else {
        // Write failed (not paused)
        writeFailed_ = true;
    }
}

void MemoryInspectorWindow::cancelEdit()
{
    editingByte_ = false;
    writeFailed_ = false;
}
