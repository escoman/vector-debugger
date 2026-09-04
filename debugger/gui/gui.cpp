#include "gui.h"

// Dear ImGui core + backends
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl2.h"

// SDL2
#include "SDL.h"
#include "SDL_opengl.h"

// Debugger backend
#include "backend.h"
#include "events.h"
#include "disassembler.h"
#include "icon.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>

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

    // Maximize after creation — ImGui viewport init may override the
    // SDL_WINDOW_MAXIMIZED create flag, so we maximize explicitly.
    SDL_MaximizeWindow(window_);

    // Set window icon from embedded resource
    icon_set(window_);

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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Style
    ImGui::StyleColorsDark();

    // Backend init
    ImGui_ImplSDL2_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL2_Init();

    // Workspace Manager (Stage 5.1) — directory setup only;
    // visibility refs and actual loading happen after windows are created
    //
    // Allow override via environment variable so that GUI smoke tests
    // don't clobber the user's real workspace presets.
    std::string wsDir = "workspaces";
    if (const char *env = std::getenv("V06C_WORKSPACE_DIR")) {
        wsDir = env;
    }
    workspaceManager_.initialize(wsDir);

    return true;
}

void DebuggerGui::shutdown()
{
    if (!window_) return;

    // Save workspace before tearing down ImGui context
    workspaceManager_.shutdown();

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

void DebuggerGui::beginFrame(IDebugBackend &backend)
{
    const bool kbdLocked = keyboardWindow_.isLocked();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // When keyboard lock is active, forward key events to the emulator
        // and block them from ImGui — the user's keyboard is "captured" by
        // the emulated Vector-06C.
        if (kbdLocked && (event.type == SDL_KEYDOWN ||
                          event.type == SDL_KEYUP ||
                          event.type == SDL_TEXTINPUT)) {
            keyboardWindow_.handleSdlEvent(event, backend);
            continue;  // do NOT pass to ImGui
        }

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

void DebuggerGui::applyPendingWorkspace()
{
    workspaceManager_.applyPendingWorkspace();
    workspaceManager_.processDeferredOps();
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
    ImGuiViewport *viewport = ImGui::GetMainViewport();

    // --- Main menu bar (rendered first, on top of everything) ---
    renderToolbar(backend);

    // --- Adjust work area below the main menu bar ---
    float menuBarHeight = ImGui::GetFrameHeight();
    viewport->WorkPos.y += menuBarHeight;
    viewport->WorkSize.y -= menuBarHeight;

    // --- DockSpace (Stage 5.0/5.1) ---
    // Fills the adjusted work area — windows cannot overlap the menu bar.
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = 0;
    hostFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
    mainDockId_ = ImGui::GetID("MainDock");
    ImGui::DockSpace(mainDockId_, ImVec2(0, 0),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar();

    // Get CPU state for this frame
    CpuState cpu = backend.getCpuState();

    // --- CPU Registers window (dockable) ---
    if (showCpuRegisters_) {
        ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("CPU Registers", &showCpuRegisters_)) {
            renderCpuPanel(backend);
            ImGui::Separator();
            renderCurrentInstruction(cpu.pc, backend);
        }
        ImGui::End();
    }

    // --- Instruction History window (dockable) ---
    if (showInstructionHistory_) {
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Instruction History", &showInstructionHistory_)) {
            renderInstructionHistory(backend);
        }
        ImGui::End();
    }

    // --- Open ROM dialog (custom, non-blocking) ---
    if (showOpenRomDialog_) {
        showOpenRomDialog_ = false;
        std::string lastDir = workspaceManager_.getSetting("LastRomDirectory");
        romFileDialog_.onFileSelected = [this, &backend](const std::string &path) {
            // Save the directory of the selected ROM
            size_t lastSlash = path.rfind('/');
            if (lastSlash != std::string::npos) {
                workspaceManager_.setSetting("LastRomDirectory",
                    path.substr(0, lastSlash));
            }
            uint32_t org = 0;
            // Auto-detect load address based on extension
            size_t dot = path.rfind('.');
            if (dot != std::string::npos) {
                std::string ext = path.substr(dot);
                // Convert to lowercase for comparison
                std::string extLower;
                for (char c : ext) {
                    extLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (extLower == ".rom") org = 0x0100;
                else if (extLower == ".r0m") org = 0x0000;
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
        };
        romFileDialog_.show(lastDir);
    }
    
    // Render the ROM file dialog if open
    romFileDialog_.render();
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
    
    // --- Apply cascade layout BEFORE rendering windows ---
    if (cascadeRequested_) {
        applyCascade();
    }

    // Render all debugger windows (dockable)
    memoryInspector_.render(backend);
    stackView_.render(backend);
    breakpointsWindow_.render(backend);
    disassemblyView_.render(backend);
    executionTrace_.render(backend);
    ioInspector_.render(backend);
    vectorScreen_.render(backend);
    memoryMap_.render(backend);
    functionsWindow_.render(backend);
    xrefsWindow_.render(backend);
    callGraphWindow_.render(backend);
    searchWindow_.render(backend);
    keyboardWindow_.render(backend);
    soundWindow_.render(backend);

    // Register visibility refs on first frame (triggers workspace loading)
    static bool visRegsRegistered = false;
    if (!visRegsRegistered) {
        workspaceManager_.setWindowVisibilityRefs({
            {"CPU Registers", &showCpuRegisters_},
            {"Instruction History", &showInstructionHistory_},
            {"Vector Screen", &vectorScreen_.getVisibleRef()},
            {"Memory Inspector", &memoryInspector_.getVisibleRef()},
            {"Memory Map", &memoryMap_.getVisibleRef()},
            {"Disassembly", &disassemblyView_.getVisibleRef()},
            {"Stack View", &stackView_.getVisibleRef()},
            {"Breakpoints", &breakpointsWindow_.getVisibleRef()},
            {"Execution Trace", &executionTrace_.getVisibleRef()},
            {"I/O & Hardware Inspector", &ioInspector_.getVisibleRef()},
            {"Functions", &functionsWindow_.getVisibleRef()},
            {"Cross References", &xrefsWindow_.getVisibleRef()},
            {"Call Graph", &callGraphWindow_.getVisibleRef()},
            {"Search", &searchWindow_.getVisibleRef()},
            {"Keyboard", &keyboardWindow_.getVisibleRef()},
            {"Sound", &soundWindow_.getVisibleRef()},
        });
        visRegsRegistered = true;
    }

    // Autosave workspace if layout changed
    workspaceManager_.autosave();

    // --- Save As dialog ---
    if (showSaveAsDialog_) {
        ImGui::OpenPopup("Save Workspace As");
        showSaveAsDialog_ = false;
    }
    if (ImGui::BeginPopupModal("Save Workspace As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Workspace name:");
        ImGui::InputText("##name", saveAsNameBuffer_, sizeof(saveAsNameBuffer_));
        if (ImGui::Button("Save") || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            if (saveAsNameBuffer_[0] != '\0') {
                workspaceManager_.saveWorkspaceAs(saveAsNameBuffer_);
                saveAsNameBuffer_[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            saveAsNameBuffer_[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // --- Delete confirmation ---
    if (showDeleteConfirm_) {
        ImGui::OpenPopup("Delete Workspace?");
        showDeleteConfirm_ = false;
    }
    if (ImGui::BeginPopupModal("Delete Workspace?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Delete workspace '%s'?", workspaceManager_.currentWorkspaceName().c_str());
        if (workspaceManager_.isBuiltIn(workspaceManager_.currentWorkspaceName())) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "Cannot delete built-in workspace.");
            if (ImGui::Button("OK")) { ImGui::CloseCurrentPopup(); }
        } else {
            if (ImGui::Button("Delete")) {
                workspaceManager_.deleteWorkspace(workspaceManager_.currentWorkspaceName());
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndPopup();
    }

    // --- Status bar (fixed overlay at bottom) ---
    {
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x,
                                        viewport->WorkPos.y + viewport->WorkSize.y - ImGui::GetFrameHeightWithSpacing()));
        ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, ImGui::GetFrameHeightWithSpacing()));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("##StatusBar", nullptr, flags);
        ImGui::PopStyleVar(2);
        renderStatusBar(backend);
        ImGui::End();
    }
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
// Toolbar — menu bar with File, View menus and debug controls (Stage 5.0)
// ---------------------------------------------------------------------------

void DebuggerGui::renderToolbar(IDebugBackend &backend)
{
    if (ImGui::BeginMainMenuBar())
    {
        // File menu
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open ROM...")) {
                showOpenRomDialog_ = true;
                romErrorBuffer_[0] = '\0';
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                quit_ = true;
            }
            ImGui::EndMenu();
        }

        // View menu — window visibility toggles + layout
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("CPU Registers", nullptr, &showCpuRegisters_);
            ImGui::MenuItem("Instruction History", nullptr, &showInstructionHistory_);
            ImGui::Separator();
            ImGui::MenuItem("Vector Screen", nullptr, &vectorScreen_.getVisibleRef());
            ImGui::MenuItem("Memory Inspector", nullptr, &memoryInspector_.getVisibleRef());
            ImGui::MenuItem("Memory Map", nullptr, &memoryMap_.getVisibleRef());
            ImGui::MenuItem("Disassembly", nullptr, &disassemblyView_.getVisibleRef());
            ImGui::MenuItem("Stack", nullptr, &stackView_.getVisibleRef());
            ImGui::MenuItem("Breakpoints", nullptr, &breakpointsWindow_.getVisibleRef());
            ImGui::MenuItem("Execution Trace", nullptr, &executionTrace_.getVisibleRef());
            ImGui::MenuItem("I/O & Hardware Inspector", nullptr, &ioInspector_.getVisibleRef());
            ImGui::MenuItem("Functions", nullptr, &functionsWindow_.getVisibleRef());
            ImGui::MenuItem("Cross References", nullptr, &xrefsWindow_.getVisibleRef());
            ImGui::MenuItem("Call Graph", nullptr, &callGraphWindow_.getVisibleRef());
            ImGui::MenuItem("Search", nullptr, &searchWindow_.getVisibleRef());
            ImGui::MenuItem("Keyboard", nullptr, &keyboardWindow_.getVisibleRef());
            ImGui::MenuItem("Sound", nullptr, &soundWindow_.getVisibleRef());
            ImGui::Separator();
            if (ImGui::MenuItem("Cascade")) {
                layoutCascade();
            }
            if (ImGui::MenuItem("Tile")) {
                layoutTile();
            }
            ImGui::EndMenu();
        }

        // Workspace menu (Stage 5.1)
        if (ImGui::BeginMenu("Workspace")) {
            auto workspaces = workspaceManager_.listWorkspaces();
            for (const auto &ws : workspaces) {
                bool isCurrent = (ws == workspaceManager_.currentWorkspaceName());
                if (ImGui::MenuItem(ws.c_str(), nullptr, isCurrent)) {
                    workspaceManager_.switchWorkspace(ws);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save")) {
                workspaceManager_.saveCurrentWorkspace();
            }
            if (ImGui::MenuItem("Save As...")) {
                snprintf(saveAsNameBuffer_, sizeof(saveAsNameBuffer_), "%s",
                         workspaceManager_.currentWorkspaceName().c_str());
                showSaveAsDialog_ = true;
            }
            if (ImGui::MenuItem("Delete")) {
                showDeleteConfirm_ = true;
            }
            if (ImGui::MenuItem("Reset")) {
                workspaceManager_.resetWorkspace();
            }
            ImGui::EndMenu();
        }

        // Debug controls on the right side
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 200);
        renderControls(backend);

        ImGui::EndMainMenuBar();
    }
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
            // Refresh all windows after step
            memoryInspector_.requestRefresh();
            stackView_.requestRefresh();
            disassemblyView_.requestRefresh();
            executionTrace_.requestRefresh();
            ioInspector_.requestRefresh();
            vectorScreen_.requestRefresh();
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
            // Refresh all windows after pause
            memoryInspector_.requestRefresh();
            stackView_.requestRefresh();
            disassemblyView_.requestRefresh();
            executionTrace_.requestRefresh();
            ioInspector_.requestRefresh();
            vectorScreen_.requestRefresh();
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
        // Refresh all windows after reset
        memoryInspector_.requestRefresh();
        stackView_.requestRefresh();
        disassemblyView_.requestRefresh();
        executionTrace_.requestRefresh();
        ioInspector_.requestRefresh();
        vectorScreen_.requestRefresh();
        histNeedsRefresh_ = true;
    }
}

// ---------------------------------------------------------------------------
// Layout — Cascade / Tile (Stage 5.1)
// ---------------------------------------------------------------------------

void DebuggerGui::layoutCascade()
{
    // Collect visible window names (same order as render pass)
    struct WinInfo { const char *name; bool *visible; };
    WinInfo wins[] = {
        {"CPU Registers", &showCpuRegisters_},
        {"Instruction History", &showInstructionHistory_},
        {"Vector Screen", &vectorScreen_.getVisibleRef()},
        {"Memory Inspector", &memoryInspector_.getVisibleRef()},
        {"Memory Map", &memoryMap_.getVisibleRef()},
        {"Disassembly", &disassemblyView_.getVisibleRef()},
        {"Stack View", &stackView_.getVisibleRef()},
        {"Breakpoints", &breakpointsWindow_.getVisibleRef()},
        {"Execution Trace", &executionTrace_.getVisibleRef()},
        {"I/O & Hardware Inspector", &ioInspector_.getVisibleRef()},
        {"Functions", &functionsWindow_.getVisibleRef()},
        {"Cross References", &xrefsWindow_.getVisibleRef()},
        {"Call Graph", &callGraphWindow_.getVisibleRef()},
        {"Search", &searchWindow_.getVisibleRef()},
        {"Keyboard", &keyboardWindow_.getVisibleRef()},
        {"Sound", &soundWindow_.getVisibleRef()},
    };

    ImGuiViewport *vp = ImGui::GetMainViewport();
    float baseX = vp->WorkPos.x + 40.0f;
    float baseY = vp->WorkPos.y + 40.0f;
    const int step = 24;
    int offset = 0;

    cascadePos_.clear();
    for (auto &w : wins) {
        if (!w.visible || !*w.visible) continue;
        cascadePos_[w.name] = {baseX + offset, baseY + offset};
        offset += step;
    }
    cascadeRequested_ = true;
}

void DebuggerGui::applyCascade()
{
    struct WinInfo { const char *name; bool *visible; };
    WinInfo wins[] = {
        {"CPU Registers", &showCpuRegisters_},
        {"Instruction History", &showInstructionHistory_},
        {"Vector Screen", &vectorScreen_.getVisibleRef()},
        {"Memory Inspector", &memoryInspector_.getVisibleRef()},
        {"Memory Map", &memoryMap_.getVisibleRef()},
        {"Disassembly", &disassemblyView_.getVisibleRef()},
        {"Stack View", &stackView_.getVisibleRef()},
        {"Breakpoints", &breakpointsWindow_.getVisibleRef()},
        {"Execution Trace", &executionTrace_.getVisibleRef()},
        {"I/O & Hardware Inspector", &ioInspector_.getVisibleRef()},
        {"Functions", &functionsWindow_.getVisibleRef()},
        {"Cross References", &xrefsWindow_.getVisibleRef()},
        {"Call Graph", &callGraphWindow_.getVisibleRef()},
        {"Search", &searchWindow_.getVisibleRef()},
        {"Keyboard", &keyboardWindow_.getVisibleRef()},
        {"Sound", &soundWindow_.getVisibleRef()},
    };

    for (auto &w : wins) {
        if (!w.visible || !*w.visible) continue;
        auto it = cascadePos_.find(w.name);
        if (it == cascadePos_.end()) continue;

        // Undock from dockspace
        ImGuiWindow *win = ImGui::FindWindowByName(w.name);
        if (win) {
            ImGui::DockContextQueueUndockWindow(ImGui::GetCurrentContext(), win);
        }
        // Set position and size BEFORE Begin()
        ImGui::SetNextWindowPos(ImVec2(it->second.x, it->second.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_Always);
    }
    cascadePos_.clear();
    cascadeRequested_ = false;
}

void DebuggerGui::layoutTile()
{
    // Collect visible window names
    struct WinInfo { const char *name; bool *visible; };
    std::vector<WinInfo> visible;
    WinInfo all[] = {
        {"CPU Registers", &showCpuRegisters_},
        {"Instruction History", &showInstructionHistory_},
        {"Vector Screen", &vectorScreen_.getVisibleRef()},
        {"Memory Inspector", &memoryInspector_.getVisibleRef()},
        {"Memory Map", &memoryMap_.getVisibleRef()},
        {"Disassembly", &disassemblyView_.getVisibleRef()},
        {"Stack View", &stackView_.getVisibleRef()},
        {"Breakpoints", &breakpointsWindow_.getVisibleRef()},
        {"Execution Trace", &executionTrace_.getVisibleRef()},
        {"I/O & Hardware Inspector", &ioInspector_.getVisibleRef()},
        {"Functions", &functionsWindow_.getVisibleRef()},
        {"Cross References", &xrefsWindow_.getVisibleRef()},
        {"Call Graph", &callGraphWindow_.getVisibleRef()},
        {"Search", &searchWindow_.getVisibleRef()},
        {"Keyboard", &keyboardWindow_.getVisibleRef()},
    };
    for (auto &w : all) {
        if (w.visible && *w.visible) visible.push_back(w);
    }
    if (visible.empty()) return;

    // Use the saved main dockspace ID (set during render)
    ImGuiID dsId = mainDockId_;
    if (dsId == 0) return;

    int n = (int)visible.size();
    if (n == 1) {
        ImGui::DockBuilderDockWindow(visible[0].name, dsId);
        return;
    }

    // Split dockspace: left column (1/3) and right area (2/3)
    ImGuiID leftId, rightId;
    ImGui::DockBuilderSplitNode(dsId, ImGuiDir_Left, 0.33f, &leftId, &rightId);

    // Split right area into top-right and bottom-right
    ImGuiID topRightId, bottomRightId;
    ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Up, 0.5f, &topRightId, &bottomRightId);

    // Distribute windows evenly across the three regions
    int perRegion = (n + 2) / 3;
    for (int i = 0; i < n; i++) {
        ImGuiID target;
        if (i < perRegion) target = leftId;
        else if (i < 2 * perRegion) target = topRightId;
        else target = bottomRightId;
        ImGui::DockBuilderDockWindow(visible[i].name, target);
    }

    ImGui::DockBuilderFinish(dsId);
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

    // Vector Screen hover coordinates (Stage 5.1)
    if (vectorScreen_.isHoveringScreen()) {
        ImGui::SameLine();
        ImGui::Text("Screen: 512x256  X: %d  Y: %d",
                    vectorScreen_.hoverScreenX(), vectorScreen_.hoverScreenY());
    } else if (vectorScreen_.isHoveringBorder()) {
        ImGui::SameLine();
        ImGui::Text("Screen: 512x256  (border)");
    }
}
