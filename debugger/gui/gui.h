#pragma once

#include <cstdint>
#include "memory_inspector_window.h"
#include "stack_view_window.h"
#include "breakpoints_window.h"
#include "backend.h"

// Forward declarations — avoid pulling SDL/ImGui into this header.
struct SDL_Window;
typedef void *SDL_GLContext;

class DebugBackend;
class Memory;

// ---------------------------------------------------------------------------
// DebuggerGui — minimal Dear ImGui + SDL2 + OpenGL2 GUI layer.
//
// Sits on top of DebugBackend.  Does NOT access i8080, Memory, Board, etc.
// All emulator data flows through DebugBackend snapshots/API.
// ---------------------------------------------------------------------------

class DebuggerGui
{
public:
    DebuggerGui() = default;
    ~DebuggerGui();

    // Create SDL window + GL context + init ImGui.
    // Returns false on failure.
    bool initialize(int width, int height);

    // Tear down ImGui + GL context + SDL window.
    void shutdown();

    // Begin a new ImGui frame (processes pending SDL events internally).
    void beginFrame();

    // Render all debugger panels.  Called between beginFrame/endFrame.
    void render(DebugBackend &backend, Memory &memory);

    // Finalize the frame and swap buffers.
    void endFrame();

    // True after the user requested window close.
    bool shouldQuit() const;

private:
    SDL_Window   *window_    = nullptr;
    SDL_GLContext glContext_  = nullptr;
    bool          quit_       = false;

    // Panel renderers
    void renderCpuPanel(DebugBackend &backend);
    void renderCurrentInstruction(uint16_t pc, DebugBackend &backend, Memory &memory);
    void renderInstructionHistory(DebugBackend &backend);
    void renderStatusBar(DebugBackend &backend);
    void renderControls(DebugBackend &backend);
    
    // Memory Inspector window (Stage 3.3)
    MemoryInspectorWindow memoryInspector_;
    
    // Stack View window (Stage 3.4)
    StackViewWindow stackView_;
    
    // Breakpoints window (Stage 3.7)
    BreakpointsWindow breakpointsWindow_;
    
    // CPU register editing state (Stage 3.6)
    bool editingRegister_ = false;
    DebugBackend::RegisterId editingRegId_ = DebugBackend::RegisterId::AF;
    char editRegBuffer_[8] = "";
    bool writeRegFailed_ = false;
};
