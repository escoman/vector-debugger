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
// BoardWrapper
//
// Holds all dependencies required by Board and provides initialization.
// This is a convenience class for the GUI to manage the full emulator state.
// ---------------------------------------------------------------------------

class BoardWrapper
{
public:
    BoardWrapper();
    ~BoardWrapper();
    
    // Initialize all components
    void init();
    
    // Shutdown all components
    void shutdown();
    
    // Load ROM file and reset Board
    bool loadRom(const std::string &path, uint32_t org = 0);
    
    // Public components (accessible for DebugBackend and GUI)
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
};
