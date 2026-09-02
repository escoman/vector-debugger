#include "disassembly_window.h"
#include "disassembler.h"
#include "opcode_info.h"

// Dear ImGui
#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Address parsing
// ---------------------------------------------------------------------------

bool DisassemblyWindow::parseAddress(const char *input, uint16_t &address) const
{
    unsigned int addr = 0;
    if (sscanf(input, "%x", &addr) != 1) return false;
    if (addr > 0xFFFF) return false;
    address = static_cast<uint16_t>(addr);
    return true;
}

// ---------------------------------------------------------------------------
// Main render
// ---------------------------------------------------------------------------

void DisassemblyWindow::render(DebugBackend &backend)
{
    if (!visible_) return;
    
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    
    if (!ImGui::Begin("Disassembly", &visible_)) {
        ImGui::End();
        return;
    }
    
    // Detect PC changes for Follow PC
    CpuState cpu = backend.getCpuState();
    if (!pcInitialized_) {
        lastPc_ = cpu.pc;
        viewAddress_ = cpu.pc;
        pcInitialized_ = true;
    } else if (followPc_ && cpu.pc != lastPc_) {
        needsRefresh_ = true;
    }
    lastPc_ = cpu.pc;
    
    // Render toolbar
    renderToolbar(backend);
    
    ImGui::Separator();
    
    // Render disassembly list
    renderDisassemblyList(backend);
    
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

void DisassemblyWindow::renderToolbar(DebugBackend &backend)
{
    // Address input
    ImGui::SetNextItemWidth(80);
    bool enterPressed = ImGui::InputText("##dasmaddr", addressInput_, sizeof(addressInput_),
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    
    ImGui::SameLine();
    if (ImGui::Button("Go") || enterPressed) {
        uint16_t addr;
        if (parseAddress(addressInput_, addr)) {
            viewAddress_ = addr;
            followPc_ = false;  // manual navigation disables follow
            needsRefresh_ = true;
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("PC")) {
        CpuState cpu = backend.getCpuState();
        viewAddress_ = cpu.pc;
        snprintf(addressInput_, sizeof(addressInput_), "%04X", cpu.pc);
        needsRefresh_ = true;
    }
    
    ImGui::SameLine();
    if (ImGui::Checkbox("Follow PC", &followPc_)) {
        if (followPc_) {
            CpuState cpu = backend.getCpuState();
            viewAddress_ = cpu.pc;
            needsRefresh_ = true;
        }
    }
    
    // Step button
    ImGui::SameLine();
    bool paused = backend.isPaused();
    if (ImGui::Button("Step") && paused) {
        backend.stepInstruction();
        needsRefresh_ = true;
    }
}

// ---------------------------------------------------------------------------
// Disassembly list
// ---------------------------------------------------------------------------

void DisassemblyWindow::renderDisassemblyList(DebugBackend &backend)
{
    CpuState cpu = backend.getCpuState();
    uint16_t pc = cpu.pc;
    
    // Determine the start address for decoding
    uint16_t startAddr;
    if (followPc_) {
        // Go back ~60 bytes from PC for context above
        startAddr = static_cast<uint16_t>(pc - 60);
    } else {
        startAddr = viewAddress_;
    }
    
    // Read function: uses DebugBackend::readMemory() which goes through
    // DebugMemoryAccess::peek() — respects banking, no raw buffer access.
    DisasmReadFn readFn = [&backend](uint16_t addr) -> uint8_t {
        return backend.readMemory(addr);
    };
    
    // Decode instructions forward from startAddr.
    // We collect enough lines to fill the visible area (~40 lines).
    // In Follow PC mode, we skip lines before (pc - 60) to ensure
    // correct alignment at the display start.
    
    const int maxLines = 50;
    const int displayLines = 40;
    
    // For Follow PC mode, the first visible address is (pc - 60).
    // We start decoding from there but instructions might not align,
    // so we decode and skip until we reach the display start.
    uint16_t displayStartAddr = startAddr;
    
    // Decode all instructions into a temporary array
    struct Line {
        uint16_t addr;
        DisassembledInstruction instr;
    };
    Line lines[60];  // extra capacity for skip region
    int lineCount = 0;
    
    uint16_t addr = startAddr;
    // Decode up to 60 instructions (enough for skip region + display)
    for (int i = 0; i < 60; ++i) {
        lines[lineCount].addr = addr;
        lines[lineCount].instr = disassemble(addr, readFn);
        lineCount++;
        
        uint8_t len = lines[lineCount - 1].instr.length;
        uint16_t nextAddr = static_cast<uint16_t>(addr + len);
        
        // Stop if address wrapped around (nextAddr < addr means overflow)
        if (nextAddr <= addr) break;
        addr = nextAddr;
        
        // Stop if we've gone far enough past the display area
        if (followPc_) {
            // We want to show displayLines past the PC
            // Stop when addr > pc + displayLines * 3 (max instruction size)
            if (lineCount >= 60) break;
            // Check if we've gone far enough past PC
            int distFromPc = static_cast<int>(static_cast<uint16_t>(addr - pc));
            if (distFromPc > displayLines * 3 && lineCount > 20) break;
        } else {
            if (lineCount >= maxLines) break;
        }
    }
    
    // Determine which lines to display
    int firstVisible = 0;
    if (followPc_) {
        // Skip lines that start before displayStartAddr
        for (int i = 0; i < lineCount; ++i) {
            // A line is visible if its address >= displayStartAddr
            // or if it contains the displayStartAddr (straddling)
            if (lines[i].addr >= displayStartAddr || 
                lines[i].addr + lines[i].instr.length > displayStartAddr) {
                // But only if it starts at or after displayStartAddr
                // (we don't show partial instructions from before the window)
                if (lines[i].addr >= displayStartAddr) {
                    firstVisible = i;
                    break;
                }
            }
            firstVisible = i + 1;
        }
    }
    
    // Child window for scrolling
    ImGui::BeginChild("DasmScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                       ImGuiWindowFlags_HorizontalScrollbar);
    
    int pcLineIndex = -1;
    int displayedCount = 0;
    
    for (int i = firstVisible; i < lineCount && displayedCount < displayLines; ++i) {
        const auto &line = lines[i];
        uint16_t lineAddr = line.addr;
        const auto &instr = line.instr;
        
        bool isPc = (lineAddr == pc);
        bool hasBp = backend.hasBreakpoint(lineAddr);
        
        if (isPc) pcLineIndex = displayedCount;
        
        // Highlight PC line
        if (isPc) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.5f, 1.0f));
        }
        
        // Build the line text
        ImGui::PushID(i);
        
        // PC marker
        if (isPc) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "\xe2\x86\x92");  // →
        } else {
            ImGui::TextDisabled(" ");
        }
        ImGui::SameLine();
        
        // Breakpoint marker
        if (hasBp) {
            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "\xe2\x97\x8f");  // ●
        } else {
            ImGui::TextDisabled(" ");
        }
        ImGui::SameLine();
        
        // Address
        char addrStr[8];
        snprintf(addrStr, sizeof(addrStr), "%04X", lineAddr);
        
        // Bytes column
        char bytesStr[16] = "";
        int bpos = 0;
        for (int b = 0; b < 3; ++b) {
            if (b < instr.length) {
                bpos += snprintf(bytesStr + bpos, sizeof(bytesStr) - bpos, "%02X ",
                                 static_cast<unsigned>(instr.bytes[b]));
            } else {
                bpos += snprintf(bytesStr + bpos, sizeof(bytesStr) - bpos, "   ");
            }
        }
        
        // Render as selectable line
        char fullLine[128];
        snprintf(fullLine, sizeof(fullLine), "%s  %s  %s",
                 addrStr, bytesStr, instr.text.c_str());
        
        bool clicked = ImGui::Selectable(fullLine, isPc,
                                          ImGuiSelectableFlags_AllowDoubleClick);
        
        // Context menu for breakpoint toggle
        if (ImGui::BeginPopupContextItem("dasmctx")) {
            if (hasBp) {
                if (ImGui::MenuItem("Remove Breakpoint")) {
                    backend.removeBreakpoint(lineAddr);
                }
            } else {
                if (ImGui::MenuItem("Set Breakpoint")) {
                    backend.addBreakpoint(lineAddr);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Go to Memory Inspector")) {
                if (onGoToMemoryInspector) {
                    onGoToMemoryInspector(lineAddr);
                }
            }
            ImGui::EndPopup();
        }
        
        // Double-click → Go to Memory Inspector
        if (clicked && ImGui::IsMouseDoubleClicked(0)) {
            if (onGoToMemoryInspector) {
                onGoToMemoryInspector(lineAddr);
            }
        }
        
        ImGui::PopID();
        
        if (isPc) {
            ImGui::PopStyleColor();
            
            // Auto-scroll to PC line when Follow PC is active
            if (followPc_) {
                ImGui::SetScrollHereY(0.4f);
            }
        }
        
        // Stage 3.9: scroll to gotoAddress target
        if (pendingScroll_ && lineAddr == viewAddress_) {
            ImGui::SetScrollHereY(0.3f);
            pendingScroll_ = false;
        }
        
        displayedCount++;
    }
    
    if (lineCount == 0) {
        ImGui::TextDisabled("(no disassembly)");
    }
    
    ImGui::EndChild();
}
