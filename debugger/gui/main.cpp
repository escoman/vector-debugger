// Vector-06C Debugger — GUI entry point
//
// Composition root: creates DebugAdapter (emulator) + DebugBackend (core),
// an emulation thread, and runs the Dear ImGui main loop.
//
// Stage 3.13a: HAL functions moved to debug_adapter.cpp.
// main.cpp no longer contains Memory, IO, Board, or i8080_hal_* code.

#include "gui.h"
#include "backend.h"
#include "debug_adapter.h"

#include <cstdio>
#include <thread>

// ---------------------------------------------------------------------------
// Global backend pointer — used by HAL functions in debug_adapter.cpp
// for I/O instrumentation (onIoInput/onIoOutput callbacks).
// ---------------------------------------------------------------------------

DebugBackend *g_adapter_backend = nullptr;

// ---------------------------------------------------------------------------
// Emulation thread
// ---------------------------------------------------------------------------

static void emulationThreadFunc(DebugBackend &backend)
{
    backend.runUntilPause();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    printf("Vector-06C Debugger — starting up\n");

    // Configure for debugger mode: OpenGL mode allocates direct pixel buffer
    // that we can read via screenSnapshot() for the Vector Screen window.
    Options.opengl = true;

    // --- Create full emulator (Board + all dependencies) ---
    DebugAdapter adapter;
    adapter.init();
    adapter.bindHal();   // sets static pointers used by HAL functions

    // --- Create DebugBackend and attach adapter as IDebugTarget ---
    DebugBackend backend(adapter);
    g_adapter_backend = &backend;

    // --- Load ROM from command line if provided ---
    if (argc > 1) {
        std::string rom_path = argv[1];
        uint32_t org = 0;

        if (argc > 2) {
            org = std::strtoul(argv[2], nullptr, 0);
        } else {
            size_t dot_pos = rom_path.rfind('.');
            if (dot_pos != std::string::npos) {
                std::string ext = rom_path.substr(dot_pos);
                if (ext == ".rom") {
                    org = 0x0100;
                } else if (ext == ".r0m") {
                    org = 0x0000;
                }
            }
        }

        if (!backend.loadRom(rom_path, org)) {
            std::fprintf(stderr, "Failed to load ROM: %s\n", rom_path.c_str());
        }
    }

    // --- Start emulation thread ---
    std::thread emuThread(emulationThreadFunc, std::ref(backend));

    // --- Init GUI ---
    DebuggerGui gui;
    if (!gui.initialize(1024, 768)) {
        std::fprintf(stderr, "Failed to initialize GUI\n");
        backend.requestQuit();
        emuThread.join();
        return 1;
    }

    // --- Main loop ---
    while (!gui.shouldQuit()) {
        gui.beginFrame(backend);
        gui.render(backend);  // DebugBackend& implicitly converts to IDebugBackend&
        gui.endFrame();

        // Workspace init deferred to between frames (Stage 5.1 fix):
        // writeBuiltinIfMissing creates a temp ImGui context which would
        // corrupt the main frame state if called during render.
        gui.applyPendingWorkspace();
    }

    // --- Shutdown ---
    backend.requestQuit();
    emuThread.join();
    gui.shutdown();
    adapter.shutdown();

    g_adapter_backend = nullptr;

    printf("Vector-06C Debugger — shutdown complete\n");

    return 0;
}
