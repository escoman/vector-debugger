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
#include "memory.h"
#include "debug_memory.h"

#include <cstdio>

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
// Main render — assembles all panels
// ---------------------------------------------------------------------------

void DebuggerGui::render(DebugBackend &backend, Memory &memory)
{
    // Full-window layout with docking-like splits.
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("Debugger", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // -- Top: toolbar with Run / Pause buttons --
    renderControls(backend);
    ImGui::Separator();

    // -- Middle: two-column layout --
    float leftWidth = 280.0f;

    ImGui::BeginChild("LeftPanel", ImVec2(leftWidth, 0), ImGuiChildFlags_None,
                       ImGuiWindowFlags_None);
    {
        CpuState cpu = backend.getCpuState();
        renderCpuPanel(cpu);
        ImGui::Separator();
        renderCurrentInstruction(cpu.pc, backend, memory);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("RightPanel", ImVec2(0, 0), ImGuiChildFlags_None,
                       ImGuiWindowFlags_None);
    {
        renderInstructionHistory(backend);
    }
    ImGui::EndChild();

    // -- Bottom: status bar --
    ImGui::Separator();
    renderStatusBar(backend);

    ImGui::End();
    
    // Render Memory Inspector window (separate, movable window)
    memoryInspector_.render(backend);
}

// ---------------------------------------------------------------------------
// CPU Panel
// ---------------------------------------------------------------------------

void DebuggerGui::renderCpuPanel(const CpuState &s)
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "CPU");
    ImGui::Spacing();

    ImGui::Text("PC   %04X", s.pc);
    ImGui::Text("AF   %02X%02X", s.a, s.flags);
    ImGui::Text("BC   %02X%02X", s.b, s.c);
    ImGui::Text("DE   %02X%02X", s.d, s.e);
    ImGui::Text("HL   %02X%02X", s.h, s.l);
    ImGui::Text("SP   %04X", s.sp);
    ImGui::Text("IFF  %d", s.iff ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Current Instruction
// ---------------------------------------------------------------------------

void DebuggerGui::renderCurrentInstruction(uint16_t pc, DebugBackend &backend,
                                            Memory &memory)
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Current Instruction");
    ImGui::Spacing();

    // Use the disassembler with DebugMemoryAccess::peek() as the read function.
    DisasmReadFn readFn = [&memory](uint16_t addr) -> uint8_t {
        return DebugMemoryAccess::peek(memory, addr);
    };

    DisassembledInstruction instr = disassemble(pc, readFn);
    ImGui::Text("%04X: %s", pc, instr.text.c_str());
}

// ---------------------------------------------------------------------------
// Instruction History
// ---------------------------------------------------------------------------

void DebuggerGui::renderInstructionHistory(DebugBackend &backend)
{
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Instruction History");
    ImGui::Spacing();

    auto history = backend.instructionHistorySnapshot();
    if (history.empty()) {
        ImGui::TextDisabled("(no instructions executed)");
        return;
    }

    // Show last N instructions (scrollable region).
    ImGui::BeginChild("HistoryScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                       ImGuiWindowFlags_None);

    // We need a read function for disassembly.  Use the backend's memory
    // snapshot — but for simplicity, we only show pcBefore + opcode info
    // that's already in the InstructionEvent.
    for (size_t i = 0; i < history.size(); ++i) {
        const auto &ev = history[i];
        bool isLast = (i == history.size() - 1);

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

void DebuggerGui::renderControls(DebugBackend &backend)
{
    DebuggerState state = backend.getState();

    // Step button: always available when paused.
    bool paused = (state == DebuggerState::Paused);
    if (paused) {
        if (ImGui::Button("Step")) {
            backend.requestStep();
            // Refresh memory inspector after step
            memoryInspector_.requestRefresh();
        }
        ImGui::SameLine();
    }

    // Run / Pause toggle.
    if (state == DebuggerState::Running) {
        if (ImGui::Button("Pause")) {
            backend.requestPause();
            // Refresh memory inspector after pause
            memoryInspector_.requestRefresh();
        }
    } else {
        if (ImGui::Button("Run")) {
            backend.requestRun();
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        backend.requestReset();
        // Refresh memory inspector after reset
        memoryInspector_.requestRefresh();
    }
    
    // Memory Inspector toggle
    ImGui::SameLine();
    if (ImGui::Button("Memory Inspector")) {
        memoryInspector_.setVisible(!memoryInspector_.isVisible());
    }
}

// ---------------------------------------------------------------------------
// Status bar
// ---------------------------------------------------------------------------

void DebuggerGui::renderStatusBar(DebugBackend &backend)
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

    ImGui::Text("Status: %s   PC: %04X   Instructions: %llu",
                stateStr, cpu.pc, (unsigned long long)seq);
}
