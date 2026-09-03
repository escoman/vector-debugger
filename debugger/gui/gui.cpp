#include "gui.h"

// Dear ImGui core + backends
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl2.h"

// SDL2
#include "SDL.h"
#include "SDL_opengl.h"

// Debugger backend
#include "backend.h"
#include "events.h"
#include "disassembler.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Native file dialog (zenity on Linux)
// ---------------------------------------------------------------------------

static std::string showNativeFileOpenDialog()
{
#ifdef __linux__
    FILE *fp = popen(
        "zenity --title='Open ROM' --file-selection"
        " --file-filter='ROM files | *.rom *.r0m *.bin *.ROM *.R0M *.BIN'"
        " --file-filter='All files | *'"
        " 2>/dev/null", "r");
    if (!fp) return {};

    char buf[1024];
    std::string result;
    if (fgets(buf, sizeof(buf), fp)) {
        result = buf;
        // Remove trailing newline
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(fp);
    return result;
#else
    return {};
#endif
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

DebuggerGui::~DebuggerGui()
{
    shutdown();
}

bool DebuggerGui::initialize(int width, int height)
{
    // SDL init (video + events)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // GL 2.1 context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    window_ = SDL_CreateWindow(
        "Vector-06C Debugger",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        SDL_Quit();
        return false;
    }

    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1);  // vsync

    // ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Style
    ImGui::StyleColorsDark();

    // Backend init
    ImGui_ImplSDL2_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL2_Init();

    return true;
}

void DebuggerGui::shutdown()
{
    if (!window_) return;

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glContext_);
    SDL_DestroyWindow(window_);
    SDL_Quit();

    glContext_ = nullptr;
    window_    = nullptr;
}

// ---------------------------------------------------------------------------
// Frame management
// ---------------------------------------------------------------------------

void DebuggerGui::beginFrame()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) {
            quit_ = true;
        }
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == SDL_GetWindowID(window_)) {
            quit_ = true;
        }
    }

    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void DebuggerGui::endFrame()
{
    ImGui::Render();
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
}

bool DebuggerGui::shouldQuit() const
{
    return quit_;
}

// ---------------------------------------------------------------------------
// Central Navigation API (Stage 3.9)
// ---------------------------------------------------------------------------

void DebuggerGui::gotoMemory(uint16_t address)
{
    memoryInspector_.setVisible(true);
    memoryInspector_.gotoAddress(address);
}

void DebuggerGui::gotoDisassembly(uint16_t address)
{
    disassemblyView_.setVisible(true);
    disassemblyView_.gotoAddress(address);
}

void DebuggerGui::gotoStack(uint16_t address)
{
    stackView_.setVisible(true);
    stackView_.gotoAddress(address);
}

// ---------------------------------------------------------------------------
// Main render — assembles all panels
// ---------------------------------------------------------------------------

void DebuggerGui::render(IDebugBackend &backend)
{
    // Full-window layout with docking-like splits.
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Debugger", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);

    // -- Top: toolbar with Run / Pause buttons --
    // File menu button (opens popup below toolbar)
    if (ImGui::Button("File")) {
        ImGui::OpenPopup("FileMenu");
    }
    if (ImGui::BeginPopup("FileMenu")) {
        if (ImGui::MenuItem("Open ROM...")) {
            showOpenRomDialog_ = true;
            romErrorBuffer_[0] = '\0';
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    renderControls(backend);
    ImGui::Separator();

    // Get CPU state for this frame
    CpuState cpu = backend.getCpuState();

    // -- Middle: two-column layout (reserve space for status bar at bottom) --
    float leftWidth = 280.0f;
    float statusbarHeight = ImGui::GetFrameHeightWithSpacing();
    float childHeight = ImGui::GetContentRegionAvail().y - statusbarHeight;

    ImGui::BeginChild("LeftPanel", ImVec2(leftWidth, childHeight), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        renderCpuPanel(backend);
        ImGui::Separator();
        renderCurrentInstruction(cpu.pc, backend);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("RightPanel", ImVec2(0, childHeight), ImGuiChildFlags_None,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        renderInstructionHistory(backend);
    }
    ImGui::EndChild();

    // -- Bottom: status bar --
    ImGui::Separator();
    renderStatusBar(backend);

    ImGui::End();

    // --- Open ROM dialog ---
    if (showOpenRomDialog_) {
        showOpenRomDialog_ = false;
        std::string path = showNativeFileOpenDialog();
        if (!path.empty()) {
            uint32_t org = 0;
            if (true) { // auto-detect
                size_t dot = path.rfind('.');
                if (dot != std::string::npos) {
                    std::string ext = path.substr(dot);
                    if (ext == ".rom") org = 0x0100;
                    else if (ext == ".r0m") org = 0x0000;
                }
            }
            backend.requestPause();
            if (backend.loadRom(path, org)) {
                memoryInspector_.requestRefresh();
                disassemblyView_.requestRefresh();
                stackView_.requestRefresh();
                vectorScreen_.requestRefresh();
                histNeedsRefresh_ = true;
            } else {
                snprintf(romErrorBuffer_, sizeof(romErrorBuffer_),
                         "Failed to load: %s", path.c_str());
            }
        }
    }
    if (romErrorBuffer_[0]) {
        ImGui::OpenPopup("ROM Error");
    }
    if (ImGui::BeginPopupModal("ROM Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", romErrorBuffer_);
        if (ImGui::Button("OK")) {
            romErrorBuffer_[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Setup cross-window navigation callbacks (Stage 3.9)
    stackView_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    stackView_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    
    memoryInspector_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    
    disassemblyView_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    
    breakpointsWindow_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    breakpointsWindow_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    
    executionTrace_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    executionTrace_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    
    ioInspector_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    ioInspector_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    
    memoryMap_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    memoryMap_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    
    functionsWindow_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    functionsWindow_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    
    xrefsWindow_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    
    searchWindow_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    searchWindow_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    
    // Stage 4 Enhanced: Vector Screen navigation callbacks
    vectorScreen_.onGoToMemoryInspector = [this](uint16_t a) { gotoMemory(a); };
    vectorScreen_.onGoToDisassembly = [this](uint16_t a) { gotoDisassembly(a); };
    
    // Render Memory Inspector window (separate, movable window)
    memoryInspector_.render(backend);
    
    // Render Stack View window (separate, movable window)
    stackView_.render(backend);
    
    // RenderBreakpoints window (Stage 3.7)
    breakpointsWindow_.render(backend);
    
    // Render Disassembly View window (Stage 3.8)
    disassemblyView_.render(backend);
    
    // Render Execution Trace window (Stage 3.10)
    executionTrace_.render(backend);
    
    // Render I/O Inspector window (Stage 3.11)
    ioInspector_.render(backend);
    
    // Render Vector Screen window (Stage 4.3)
    vectorScreen_.render(backend);
    
    // Render Memory Map window (Stage 4.4)
    memoryMap_.render(backend);
    
    // Render Functions window (Stage 4.5)
    functionsWindow_.render(backend);
    
    // Render Xrefs window (Stage 4.7)
    xrefsWindow_.render(backend);
    
    // Render Call Graph window (Stage 4.7)
    callGraphWindow_.render(backend);
    
    // Render Search window (Stage 4.8)
    searchWindow_.render(backend);
}

// ---------------------------------------------------------------------------
// CPU Panel
// ---------------------------------------------------------------------------

void DebuggerGui::renderCpuPanel(IDebugBackend &backend)
{
    CpuState s = backend.getCpuState();
    
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "CPU");
    ImGui::Spacing();
    
    // Register editing mode (Stage 3.6)
    if (editingRegister_) {
        const char *regNames[] = { "AF", "BC", "DE", "HL", "SP", "PC" };
        int idx = static_cast<int>(editingRegId_);
        ImGui::Text("Edit %s:", regNames[idx]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        bool enterPressed = ImGui::InputText("##editreg", editRegBuffer_, sizeof(editRegBuffer_),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SameLine();
        if (ImGui::Button("Go") || enterPressed) {
            unsigned int value = 0;
            if (sscanf(editRegBuffer_, "%x", &value) == 1 && value <= 0xFFFF) {
                bool ok = backend.writeRegister(editingRegId_, static_cast<uint16_t>(value));
                if (ok) {
                    editingRegister_ = false;
                    writeRegFailed_ = false;
                } else {
                    writeRegFailed_ = true;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            editingRegister_ = false;
            writeRegFailed_ = false;
        }
        if (writeRegFailed_) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Write failed: CPU must be Paused");
        }
        ImGui::Separator();
        return;  // Skip normal display while editing
    }
    
    writeRegFailed_ = false;
    
    // Helper lambda: render editable register row (Stage 3.6)
    auto renderRegRow = [&](const char *name, uint16_t value, IDebugBackend::RegisterId regId) {
        ImGui::Text("%s   %04X", name, value);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            editingRegister_ = true;
            editingRegId_ = regId;
            snprintf(editRegBuffer_, sizeof(editRegBuffer_), "%04X", value);
        }
    };
    
    // 16-bit register pairs
    uint16_t af = (static_cast<uint16_t>(s.a) << 8) | s.flags;
    uint16_t bc = (static_cast<uint16_t>(s.b) << 8) | s.c;
    uint16_t de = (static_cast<uint16_t>(s.d) << 8) | s.e;
    uint16_t hl = (static_cast<uint16_t>(s.h) << 8) | s.l;
    
    renderRegRow("PC", s.pc, IDebugBackend::RegisterId::PC);
    
    // Stage 3.9: PC context menu for navigation
    if (ImGui::BeginPopupContextItem("pc_ctx")) {
        if (ImGui::MenuItem("Go to Disassembly")) {
            gotoDisassembly(s.pc);
        }
        if (ImGui::MenuItem("Go to Memory Inspector")) {
            gotoMemory(s.pc);
        }
        ImGui::EndPopup();
    }
    
    renderRegRow("AF", af,  IDebugBackend::RegisterId::AF);
    renderRegRow("BC", bc,  IDebugBackend::RegisterId::BC);
    renderRegRow("DE", de,  IDebugBackend::RegisterId::DE);
    renderRegRow("HL", hl,  IDebugBackend::RegisterId::HL);
    renderRegRow("SP", s.sp, IDebugBackend::RegisterId::SP);
    
    // Stage 3.9: SP context menu for navigation
    if (ImGui::BeginPopupContextItem("sp_ctx")) {
        if (ImGui::MenuItem("Go to Stack")) {
            gotoStack(s.sp);
        }
        if (ImGui::MenuItem("Go to Memory Inspector")) {
            gotoMemory(s.sp);
        }
        ImGui::EndPopup();
    }
    
    // 8-bit components (read-only)
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
        "A=%02X  B=%02X C=%02X  D=%02X E=%02X  H=%02X L=%02X",
        s.a, s.b, s.c, s.d, s.e, s.h, s.l);
    
    // Flags
    ImGui::Spacing();
    uint8_t f = s.flags;
    bool flagS  = (f >> 7) & 1;
    bool flagZ  = (f >> 6) & 1;
    bool flagAC = (f >> 4) & 1;
    bool flagP  = (f >> 2) & 1;
    bool flagCY = f & 1;
    
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Flags");
    ImGui::Text("S  Z  AC P  CY");
    ImGui::Text("%d  %d   %d  %d   %d",
        flagS ? 1 : 0, flagZ ? 1 : 0, flagAC ? 1 : 0,
        flagP ? 1 : 0, flagCY ? 1 : 0);
    
    // Additional CPU state (read-only)
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "State");
    ImGui::Text("IFF:        %d", s.iff ? 1 : 0);
    // Not exposed by the current emulator core.
    ImGui::Text("EI pending: N/A");
    ImGui::Text("Cycles:     %u", s.cycles);
    ImGui::Text("Last PC:    %04X", s.last_pc);
}

// ---------------------------------------------------------------------------
// Current Instruction
// ---------------------------------------------------------------------------

void DebuggerGui::renderCurrentInstruction(uint16_t pc, IDebugBackend &backend)
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Current Instruction");
    ImGui::Spacing();

    // Use the disassembler with backend.readMemory() as the read function.
    DisasmReadFn readFn = [&backend](uint16_t addr) -> uint8_t {
        return backend.readMemory(addr);
    };

    DisassembledInstruction instr = disassemble(pc, readFn);
    ImGui::Text("%04X: %s", pc, instr.text.c_str());
}

// ---------------------------------------------------------------------------
// Instruction History
// ---------------------------------------------------------------------------

void DebuggerGui::renderInstructionHistory(IDebugBackend &backend)
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Instruction History");
    ImGui::Spacing();

    // Stage 3.12: refresh cache only when needed (avoid per-frame snapshot)
    if (histNeedsRefresh_) {
        cachedHistEntries_ = backend.instructionHistorySnapshot();
        histNeedsRefresh_ = false;
    }

    if (cachedHistEntries_.empty()) {
        ImGui::TextDisabled("(no instructions executed)");
        return;
    }

    // Show last N instructions (scrollable region).
    ImGui::BeginChild("HistoryScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                       ImGuiWindowFlags_None);

    for (size_t i = 0; i < cachedHistEntries_.size(); ++i) {
        const auto &ev = cachedHistEntries_[i];
        bool isLast = (i == cachedHistEntries_.size() - 1);

        if (isLast) {
            // Highlight the most recent instruction.
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.4f, 1.0f));
        }

        ImGui::Text("%04X  %02X  seq=%llu",
                     ev.pcBefore, ev.opcode,
                     (unsigned long long)ev.sequence);

        if (isLast) {
            ImGui::PopStyleColor();
            // Auto-scroll to the last item.
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Controls — Run / Pause / Step
// ---------------------------------------------------------------------------

void DebuggerGui::renderControls(IDebugBackend &backend)
{
    DebuggerState state = backend.getState();

    // Step button: always available when paused.
    bool paused = (state == DebuggerState::Paused);
    if (paused) {
        if (ImGui::Button("Step")) {
            backend.requestStep();
            // Refresh memory inspector and stack view after step
            memoryInspector_.requestRefresh();
            stackView_.requestRefresh();
            disassemblyView_.requestRefresh();
            executionTrace_.requestRefresh();
            ioInspector_.requestRefresh();
            vectorScreen_.requestRefresh();
            memoryMap_.requestRefresh();
            functionsWindow_.requestRefresh();
            xrefsWindow_.requestRefresh();
            callGraphWindow_.requestRefresh();
            histNeedsRefresh_ = true;
        }
        ImGui::SameLine();
    }

    // Run / Pause toggle.
    if (state == DebuggerState::Running) {
        if (ImGui::Button("Pause")) {
            backend.requestPause();
            // Refresh memory inspector and stack view after pause
            memoryInspector_.requestRefresh();
            stackView_.requestRefresh();
            disassemblyView_.requestRefresh();
            executionTrace_.requestRefresh();
            ioInspector_.requestRefresh();
            vectorScreen_.requestRefresh();
            memoryMap_.requestRefresh();
            functionsWindow_.requestRefresh();
            xrefsWindow_.requestRefresh();
            callGraphWindow_.requestRefresh();
            histNeedsRefresh_ = true;
        }
    } else {
        if (ImGui::Button("Run")) {
            backend.requestRun();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        backend.requestReset();
        // Refresh memory inspector and stack view after reset
        memoryInspector_.requestRefresh();
        stackView_.requestRefresh();
        disassemblyView_.requestRefresh();
        executionTrace_.requestRefresh();
        ioInspector_.requestRefresh();
        vectorScreen_.requestRefresh();
        histNeedsRefresh_ = true;
    }
    
    // Memory Inspector toggle
    ImGui::SameLine();
    {
        bool hidden = !memoryInspector_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Memory Inspector"))
            memoryInspector_.setVisible(!memoryInspector_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Stack View toggle
    ImGui::SameLine();
    {
        bool hidden = !stackView_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Stack View"))
            stackView_.setVisible(!stackView_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Breakpoints toggle (Stage 3.7)
    ImGui::SameLine();
    {
        bool hidden = !breakpointsWindow_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Breakpoints"))
            breakpointsWindow_.setVisible(!breakpointsWindow_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Disassembly toggle (Stage 3.8)
    ImGui::SameLine();
    {
        bool hidden = !disassemblyView_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Disassembly"))
            disassemblyView_.setVisible(!disassemblyView_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Execution Trace toggle (Stage 3.10)
    ImGui::SameLine();
    {
        bool hidden = !executionTrace_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Execution Trace"))
            executionTrace_.setVisible(!executionTrace_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // I/O Inspector toggle (Stage 3.11)
    ImGui::SameLine();
    {
        bool hidden = !ioInspector_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("I/O Inspector"))
            ioInspector_.setVisible(!ioInspector_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Vector Screen toggle (Stage 4.3)
    ImGui::SameLine();
    {
        bool hidden = !vectorScreen_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Vector Screen"))
            vectorScreen_.setVisible(!vectorScreen_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Memory Map toggle (Stage 4.4)
    ImGui::SameLine();
    {
        bool hidden = !memoryMap_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Memory Map"))
            memoryMap_.setVisible(!memoryMap_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Functions toggle (Stage 4.5)
    ImGui::SameLine();
    {
        bool hidden = !functionsWindow_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Functions"))
            functionsWindow_.setVisible(!functionsWindow_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Call Graph toggle (Stage 4.7)
    ImGui::SameLine();
    {
        bool hidden = !callGraphWindow_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Call Graph"))
            callGraphWindow_.setVisible(!callGraphWindow_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
    
    // Search toggle (Stage 4.8)
    ImGui::SameLine();
    {
        bool hidden = !searchWindow_.isVisible();
        if (hidden) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
        }
        if (ImGui::Button("Search"))
            searchWindow_.setVisible(!searchWindow_.isVisible());
        if (hidden) ImGui::PopStyleColor(3);
    }
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void DebuggerGui::renderStatusBar(IDebugBackend &backend)
{
    DebuggerState state = backend.getState();
    CpuState cpu = backend.getCpuState();
    uint64_t seq = backend.instructionSequence();

    const char *stateStr = "UNKNOWN";
    switch (state) {
        case DebuggerState::Running: stateStr = "RUNNING"; break;
        case DebuggerState::Paused:  stateStr = "PAUSED";  break;
        case DebuggerState::Stopped: stateStr = "STOPPED"; break;
    }

    // Stop reason (Stage 3.7)
    const char *reasonStr = "";
    switch (backend.getStopReason()) {
        case StopReason::Breakpoint:
            // Show address of the breakpoint
            {
                static char reasonBuf[64];
                snprintf(reasonBuf, sizeof(reasonBuf), " (Breakpoint at %04X)", cpu.pc);
                reasonStr = reasonBuf;
            }
            break;
        case StopReason::UserPause:  reasonStr = " (User Pause)"; break;
        case StopReason::Step:       reasonStr = " (Step)"; break;
        case StopReason::Reset:      reasonStr = " (Reset)"; break;
        default: break;
    }

    // Stage 4.10: Show current function name if PC is inside a function
    std::string funcName = backend.symbolDatabase().displayName(cpu.pc);
    if (funcName.empty()) {
        ImGui::Text("Status: %s%s   PC: %04X   Instructions: %llu",
                    stateStr, reasonStr, cpu.pc, (unsigned long long)seq);
    } else {
        ImGui::Text("Status: %s%s   PC: %04X (%s)   Instructions: %llu",
                    stateStr, reasonStr, cpu.pc, funcName.c_str(), (unsigned long long)seq);
    }
}
