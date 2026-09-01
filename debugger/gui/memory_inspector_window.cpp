#include "memory_inspector_window.h"
#include "disassembler.h"

// Dear ImGui
#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <cctype>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

MemoryInspectorWindow::MemoryInspectorWindow()
{
    // Initialize snapshot with empty data
    snapshot_.start = 0;
}

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
                if (addr == pc) {
                    containsPc = true;
                    break;
                }
            }
            
            // Highlight line with PC
            if (containsPc) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.5f, 1.0f));
            }
            
            // Make line clickable
            ImGui::PushID(line);
            
            // Address column
            char lineBuf[256];
            int pos = snprintf(lineBuf, sizeof(lineBuf), "%04X: ", lineAddr);
            
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
                    // Printable ASCII: 0x20-0x7E
                    if (byte >= 0x20 && byte <= 0x7E) {
                        lineBuf[pos++] = static_cast<char>(byte);
                    } else {
                        lineBuf[pos++] = '.';
                    }
                }
            }
            lineBuf[pos++] = '|';
            lineBuf[pos] = '\0';
            
            // Check if this line is selected
            bool isSelected = false;
            for (int i = 0; i < bytesPerLine; ++i) {
                size_t offset = lineOffset + i;
                if (offset >= totalBytes) break;
                uint16_t addr = static_cast<uint16_t>((snapshot_.start + offset) & 0xFFFF);
                if (addr == selectedAddress_) {
                    isSelected = true;
                    break;
                }
            }
            
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.5f, 0.3f, 1.0f));
            }
            
            // Render as selectable text
            if (ImGui::Selectable(lineBuf, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                // Calculate clicked address based on mouse position
                // For simplicity, use the line start address
                selectedAddress_ = lineAddr;
                
                if (ImGui::IsMouseDoubleClicked(0)) {
                    // Double-click: set as current address
                    address_ = lineAddr;
                    snprintf(addressInput_, sizeof(addressInput_), "%04X", lineAddr);
                }
            }
            
            if (isSelected) {
                ImGui::PopStyleColor();
            }
            
            ImGui::PopID();
            
            if (containsPc) {
                ImGui::PopStyleColor();
            }
        }
    }
    clipper.End();
    
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
