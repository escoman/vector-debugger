#include "debug_adapter.h"
#include "options.h"
#include "util.h"

// ---------------------------------------------------------------------------
// HAL binding — set static pointers used by HAL functions.
//
// The actual i8080_hal_* function definitions live in the application layer
// (gui/main.cpp for the GUI target, or test files for test targets).
// Each target provides its own HAL implementation that uses these pointers.
// ---------------------------------------------------------------------------

// Static member definitions
Memory       *DebugAdapter::s_memory = nullptr;
IO           *DebugAdapter::s_io     = nullptr;
Board        *DebugAdapter::s_board  = nullptr;

void DebugAdapter::bindHal()
{
    s_memory = &memory;
    s_io     = &io;
    s_board  = &board;
}

void DebugAdapter::setHalPointers(Memory *mem, IO *io, Board *board)
{
    s_memory = mem;
    s_io     = io;
    s_board  = board;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DebugAdapter::DebugAdapter()
    : tape_player(wav)
    , tw(timer)
    , aw(ay)
    , soundnik(tw, aw)
    , io(memory, keyboard, timer, fdc, ay, tape_player)
    , filler(memory, io, tv)
    , board(memory, io, filler, soundnik, tv, tape_player)
{
}

DebugAdapter::~DebugAdapter()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void DebugAdapter::init()
{
    if (initialized_) {
        return;
    }
    
    // Initialize components in correct order
    filler.init();
    soundnik.init(nullptr);  // No WavRecorder for now
    tv.init();
    board.init();
    fdc.init();
    
    // Set up keyboard reset handler
    keyboard.onreset = [this](bool blkvvod) {
        board.reset(blkvvod ? 
                Board::ResetMode::BLKVVOD : Board::ResetMode::BLKSBR);
    };
    
    // Initial reset with bootrom
    board.reset(Board::ResetMode::BLKVVOD);
    
    initialized_ = true;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void DebugAdapter::shutdown()
{
    if (!initialized_) {
        return;
    }
    
    // Cleanup in reverse order (if needed)
    // Most components don't need explicit cleanup
    
    s_memory = nullptr;
    s_io     = nullptr;
    s_board  = nullptr;
    
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// ROM loading
// ---------------------------------------------------------------------------

bool DebugAdapter::loadRom(const std::string &path, uint32_t org)
{
    // Load ROM file
    std::vector<uint8_t> rom_data = util::load_binfile(path);
    if (rom_data.empty()) {
        return false;
    }
    
    // Load into Memory
    memory.init_from_vector(rom_data, org);
    
    // Reset Board in LOADROM mode
    Options.pc = org;  // Set PC for reset
    board.reset(Board::ResetMode::LOADROM);
    
    return true;
}
