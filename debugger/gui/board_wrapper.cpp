#include "board_wrapper.h"
#include "options.h"
#include "util.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

BoardWrapper::BoardWrapper()
    : tape_player(wav)
    , tw(timer)
    , aw(ay)
    , soundnik(tw, aw)
    , io(memory, keyboard, timer, fdc, ay, tape_player)
    , filler(memory, io, tv)
    , board(memory, io, filler, soundnik, tv, tape_player)
{
}

BoardWrapper::~BoardWrapper()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

void BoardWrapper::init()
{
    if (initialized_) {
        return;
    }
    
    printf("BoardWrapper::init() — initializing all components\n");
    
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
    
    printf("BoardWrapper::init() — initialization complete\n");
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void BoardWrapper::shutdown()
{
    if (!initialized_) {
        return;
    }
    
    printf("BoardWrapper::shutdown() — shutting down all components\n");
    
    // Cleanup in reverse order (if needed)
    // Most components don't need explicit cleanup
    
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// ROM loading
// ---------------------------------------------------------------------------

bool BoardWrapper::loadRom(const std::string &path, uint32_t org)
{
    printf("BoardWrapper::loadRom() — loading %s at %04x\n", path.c_str(), org);
    
    // Load ROM file
    std::vector<uint8_t> rom_data = util::load_binfile(path);
    if (rom_data.empty()) {
        printf("BoardWrapper::loadRom() — failed to load %s\n", path.c_str());
        return false;
    }
    
    printf("BoardWrapper::loadRom() — loaded %zu bytes\n", rom_data.size());
    
    // Load into Memory
    memory.init_from_vector(rom_data, org);
    
    // Reset Board in LOADROM mode
    Options.pc = org;  // Set PC for reset
    board.reset(Board::ResetMode::LOADROM);
    
    printf("BoardWrapper::loadRom() — ROM loaded and Board reset\n");
    
    return true;
}
