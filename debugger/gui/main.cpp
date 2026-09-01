// Vector-06C Debugger — GUI entry point
//
// Creates a BoardWrapper (full emulator) + DebugBackend, an emulation thread,
// and runs the Dear ImGui main loop.

#include "gui.h"
#include "backend.h"
#include "board_wrapper.h"
#include "i8080.h"
#include "i8080_hal.h"

#include <cstdio>
#include <thread>

// ---------------------------------------------------------------------------
// HAL — connects the CPU core to BoardWrapper's Memory instance.
// This is the same HAL used by the test binary, but here it serves the
// real debugger GUI with full Board integration.
// ---------------------------------------------------------------------------

static Memory       *g_memory = nullptr;
static DebugBackend *g_backend = nullptr;

int i8080_hal_memory_read_byte(int addr)
{
    return g_memory->read(addr, false);
}

void i8080_hal_memory_write_byte(int addr, int value)
{
    g_memory->write(addr, value, false);
}

int i8080_hal_memory_read_word(int addr, bool stack)
{
    return g_memory->read(addr, stack)
         | (g_memory->read(addr + 1, stack) << 8);
}

void i8080_hal_memory_write_word(int addr, int word, bool stack)
{
    g_memory->write(addr, word & 0xff, stack);
    g_memory->write(addr + 1, word >> 8, stack);
}

int i8080_hal_io_input(int port)
{
    if (g_backend) g_backend->onIoInput((uint8_t)port, 0xff);
    return 0xff;
}

void i8080_hal_io_output(int port, int value)
{
    if (g_backend) g_backend->onIoOutput((uint8_t)port, (uint8_t)value);
}

void i8080_hal_iff(int on)
{
    // Interrupt flip-flop — not stored for now.
    (void)on;
}

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
    
    // --- Create full emulator (Board + all dependencies) ---
    BoardWrapper wrapper;
    wrapper.init();
    
    g_memory = &wrapper.memory;
    
    // --- Create DebugBackend and attach Board ---
    DebugBackend backend(wrapper.memory);
    backend.attachBoard(&wrapper.board);
    g_backend = &backend;
    
    // --- Load ROM from command line if provided ---
    if (argc > 1) {
        std::string rom_path = argv[1];
        uint32_t org = 0;
        if (argc > 2) {
            org = std::strtoul(argv[2], nullptr, 0);
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
        gui.beginFrame();
        gui.render(backend, wrapper.memory);
        gui.endFrame();
    }

    // --- Shutdown ---
    backend.requestQuit();
    emuThread.join();
    gui.shutdown();
    wrapper.shutdown();

    g_memory  = nullptr;
    g_backend = nullptr;

    printf("Vector-06C Debugger — shutdown complete\n");
    
    return 0;
}
