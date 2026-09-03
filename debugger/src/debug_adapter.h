#pragma once

#include "memory.h"
#include "vio.h"
#include "tv.h"
#include "board.h"
#include "keyboard.h"
#include "8253.h"
#include "sound.h"
#include "ay.h"
#include "wav.h"
#include "fd1793.h"

// ---------------------------------------------------------------------------
// DebugAdapter
//
// Adapter layer between Debugger Core and Vector Emulator.
// Holds all dependencies required by Board and provides initialization.
// This is the ONLY component that has direct access to emulator internals.
// ---------------------------------------------------------------------------

class DebugAdapter
{
public:
    DebugAdapter();
    ~DebugAdapter();
    
    // Initialize all components
    void init();
    
    // Shutdown all components
    void shutdown();
    
    // Load ROM file and reset Board
    bool loadRom(const std::string &path, uint32_t org = 0);
    
    // Bind HAL callbacks (i8080_hal_*) to this adapter's components.
    // Must be called after init().
    void bindHal();
    
    // Set static HAL pointers from external code (called by i8080_hal_bind
    // from Board::init()). Allows HAL functions to work before bindHal().
    static void setHalPointers(Memory *mem, IO *io, Board *board);
    
    // Static accessors for HAL functions defined in the application layer.
    static Memory* halMemory() { return s_memory; }
    static IO*     halIo()     { return s_io; }
    static Board*  halBoard()  { return s_board; }
    
    // Public components (accessible for DebugBackend)
    Memory memory;
    FD1793 fdc;
    Wav wav;
    WavPlayer tape_player;
    Keyboard keyboard;
    I8253 timer;
    TimerWrapper tw;
    AY ay;
    AYWrapper aw;
    Soundnik soundnik;
    IO io;
    TV tv;
    PixelFiller filler;
    Board board;
    
private:
    bool initialized_ = false;
    
    // Static pointers set by bindHal(), used by HAL functions.
    static Memory       *s_memory;
    static IO           *s_io;
    static Board        *s_board;
};
