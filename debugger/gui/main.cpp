// Vector-06C Debugger — GUI entry point
//
// Creates a DebugAdapter (full emulator) + DebugBackend, an emulation thread,
// and runs the Dear ImGui main loop.

#include "gui.h"
#include "backend.h"
#include "debug_adapter.h"
#include "i8080_hal.h"

#include <cstdio>
#include <thread>

// ---------------------------------------------------------------------------
// HAL — connects the CPU core to the emulator components.
//
// This replaces src/hal.cpp for the debugger_gui target.
// All functions delegate to DebugAdapter's components via static accessors
// and additionally notify DebugBackend for instrumentation (I/O tracking).
// ---------------------------------------------------------------------------

static DebugBackend *g_backend = nullptr;

// Called from Board::init() — binds adapter components for HAL functions.
// This runs before DebugAdapter::bindHal(), so we set the static pointers
// directly here. bindHal() becomes a no-op if already set.
void i8080_hal_bind(Memory &_mem, IO &_io, Board &_board)
{
    DebugAdapter::setHalPointers(&_mem, &_io, &_board);
}

int i8080_hal_memory_read_byte(int addr)
{
    return DebugAdapter::halMemory()->read(addr, false);
}

void i8080_hal_memory_write_byte(int addr, int value)
{
    DebugAdapter::halMemory()->write(addr, value, false);
}

int i8080_hal_memory_read_word(int addr, bool stack)
{
    Memory *mem = DebugAdapter::halMemory();
    return mem->read(addr, stack)
         | (mem->read(addr + 1, stack) << 8);
}

void i8080_hal_memory_write_word(int addr, int word, bool stack)
{
    Memory *mem = DebugAdapter::halMemory();
    mem->write(addr, word & 0xff, stack);
    mem->write(addr + 1, word >> 8, stack);
}

int i8080_hal_io_input(int port)
{
    int value = DebugAdapter::halIo()->input(port);
    if (g_backend) g_backend->onIoInput((uint8_t)port, (uint8_t)value);
    return value;
}

void i8080_hal_io_output(int port, int value)
{
    DebugAdapter::halIo()->output(port, value);
    if (g_backend) g_backend->onIoOutput((uint8_t)port, (uint8_t)value);
}

void i8080_hal_iff(int on)
{
    DebugAdapter::halBoard()->interrupt(on != 0);
}

// Timer is not needed for the debugger — execution is driven by
// DebugBackend::runUntilPause() / processOneCommand(), not by SDL events.
void create_timer() {}

// Called from Soundnik::callback() when audio buffer is empty.
// Forward to Board::onframetimer() to keep timing behaviour consistent.
uint32_t timer_callback(uint32_t interval, void * param)
{
    Board *board = DebugAdapter::halBoard();
    if (board) board->onframetimer();
    return interval;
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
    
    // Configure for debugger mode: OpenGL mode allocates direct pixel buffer
    // that we can read via screenSnapshot() for the Vector Screen window.
    Options.opengl = true;
    
    // --- Create full emulator (Board + all dependencies) ---
    DebugAdapter adapter;
    adapter.init();
    adapter.bindHal();   // sets static pointers used by HAL functions above
    
    // --- Create DebugBackend and attach Board ---
    DebugBackend backend(adapter.memory);
    backend.attachBoard(&adapter.board);
    g_backend = &backend;
    
    // --- Load ROM from command line if provided ---
    if (argc > 1) {
        std::string rom_path = argv[1];
        uint32_t org = 0;
        
        if (argc > 2) {
            // Explicit address provided
            org = std::strtoul(argv[2], nullptr, 0);
        } else {
            // Auto-detect load address based on file extension
            // .rom files load at 0x0100 (after interrupt vectors)
            // .r0m files load at 0x0000 (raw memory image)
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
        gui.beginFrame();
        gui.render(backend);
        gui.endFrame();
    }

    // --- Shutdown ---
    backend.requestQuit();
    emuThread.join();
    gui.shutdown();
    adapter.shutdown();

    g_backend = nullptr;

    printf("Vector-06C Debugger — shutdown complete\n");
    
    return 0;
}
