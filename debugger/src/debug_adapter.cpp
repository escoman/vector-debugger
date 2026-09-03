#include "debug_adapter.h"
#include "backend.h"       // for DebuggerBreakpoint
#include "debug_memory.h"
#include "i8080.h"
#include "i8080_hal.h"
#include "options.h"
#include "util.h"
#include "globaldefs.h"

#include <cstdio>

using namespace i8080cpu;

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

Memory       *DebugAdapter::s_memory = nullptr;
IO           *DebugAdapter::s_io     = nullptr;
Board        *DebugAdapter::s_board  = nullptr;

// ---------------------------------------------------------------------------
// HAL functions — connect the CPU core to emulator components.
//
// These replace src/hal.cpp for the debugger target.
// I/O functions are instrumented via DebugAdapter static accessors.
// The application layer (main.cpp) sets g_backend for I/O tracking.
// ---------------------------------------------------------------------------

// Forward declaration — defined in application layer (main.cpp)
// The application provides a global backend pointer for I/O instrumentation.
class DebugBackend;
extern DebugBackend *g_adapter_backend;

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
    if (g_adapter_backend) g_adapter_backend->onIoInput((uint8_t)port, (uint8_t)value);
    return value;
}

void i8080_hal_io_output(int port, int value)
{
    DebugAdapter::halIo()->output(port, value);
    if (g_adapter_backend) g_adapter_backend->onIoOutput((uint8_t)port, (uint8_t)value);
}

void i8080_hal_iff(int on)
{
    DebugAdapter::halBoard()->interrupt(on != 0);
}

// Timer is not needed for the debugger — execution is driven by
// DebugBackend::runUntilPause(), not by SDL events.
void create_timer() {}

uint32_t timer_callback(uint32_t interval, void * param)
{
    Board *board = DebugAdapter::halBoard();
    if (board) board->onframetimer();
    return interval;
}

// ---------------------------------------------------------------------------
// HAL binding
// ---------------------------------------------------------------------------

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
    if (initialized_) return;

    filler.init();
    soundnik.init(nullptr);
    tv.init();
    board.init();
    fdc.init();

    keyboard.onreset = [this](bool blkvvod) {
        board.reset(blkvvod ?
                Board::ResetMode::BLKVVOD : Board::ResetMode::BLKSBR);
    };

    board.reset(Board::ResetMode::BLKVVOD);

    initialized_ = true;
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------

void DebugAdapter::shutdown()
{
    if (!initialized_) return;

    s_memory = nullptr;
    s_io     = nullptr;
    s_board  = nullptr;

    initialized_ = false;
}

// ---------------------------------------------------------------------------
// IDebugTarget: Memory access
// ---------------------------------------------------------------------------

uint8_t DebugAdapter::readMemory(uint16_t addr)
{
    return memory.read(addr, false);
}

uint8_t DebugAdapter::peekMemory(uint16_t addr)
{
    return DebugMemoryAccess::peek(memory, addr);
}

void DebugAdapter::writeMemory(uint16_t addr, uint8_t val)
{
    memory.write(addr, val, false);
}

// ---------------------------------------------------------------------------
// IDebugTarget: CPU state
// ---------------------------------------------------------------------------

CpuState DebugAdapter::getCpuState()
{
    CpuState s;
    s.pc    = static_cast<uint16_t>(i8080_pc());
    s.sp    = static_cast<uint16_t>(i8080_regs_sp());
    s.a     = static_cast<uint8_t>(i8080_regs_a());
    s.b     = static_cast<uint8_t>(i8080_regs_b());
    s.c     = static_cast<uint8_t>(i8080_regs_c());
    s.d     = static_cast<uint8_t>(i8080_regs_d());
    s.e     = static_cast<uint8_t>(i8080_regs_e());
    s.h     = static_cast<uint8_t>(i8080_regs_h());
    s.l     = static_cast<uint8_t>(i8080_regs_l());
    s.flags = static_cast<uint8_t>(i8080_regs_f());
    s.iff   = i8080_iff();
    s.cycles     = static_cast<uint32_t>(i8080_cycles());
    s.ei_pending = false;
    s.last_pc    = 0;
    return s;
}

void DebugAdapter::writeCpuRegister(int reg, uint16_t val)
{
    // Register encoding matches DebugBackend::RegisterId enum:
    // 0=AF, 1=BC, 2=DE, 3=HL, 4=SP, 5=PC
    switch (reg) {
        case 0: i8080_setreg_a((val >> 8) & 0xFF); i8080_setreg_f(val & 0xFF); break;
        case 1: i8080_setreg_b((val >> 8) & 0xFF); i8080_setreg_c(val & 0xFF); break;
        case 2: i8080_setreg_d((val >> 8) & 0xFF); i8080_setreg_e(val & 0xFF); break;
        case 3: i8080_setreg_h((val >> 8) & 0xFF); i8080_setreg_l(val & 0xFF); break;
        case 4: i8080_setreg_sp(val); break;
        case 5: i8080_jump(val); break;
    }
}

// ---------------------------------------------------------------------------
// IDebugTarget: Execution control
// ---------------------------------------------------------------------------

void DebugAdapter::stepInstruction()
{
    board.single_step(false);  // false = don't update screen
}

void DebugAdapter::executeFrame()
{
    board.execute_frame_with_cadence(false, false);
}

void DebugAdapter::reset(bool loadRom)
{
    if (loadRom) {
        board.reset(Board::ResetMode::LOADROM);
    } else {
        board.reset(Board::ResetMode::BLKSBR);
    }
}

// ---------------------------------------------------------------------------
// IDebugTarget: Debugger control
// ---------------------------------------------------------------------------

void DebugAdapter::debuggerBreak()      { board.debugger_break(); }
void DebugAdapter::debuggerContinue()   { board.debugger_continue(); }
void DebugAdapter::debuggerAttached()   { board.debugger_attached(); }
void DebugAdapter::debuggerDetached()   { board.debugger_detached(); }

void DebugAdapter::setPollCallback(std::function<void()> cb)
{
    board.poll_debugger = cb;
}

// ---------------------------------------------------------------------------
// IDebugTarget: Breakpoints
// ---------------------------------------------------------------------------

void DebugAdapter::syncBreakpoints(const DebuggerBreakpoint *bps, size_t count)
{
    // Board::insert_breakpoint() adds to its internal list.
    // type=0 (software breakpoint), kind=1.
    for (size_t i = 0; i < count; ++i) {
        if (bps[i].enabled) {
            board.insert_breakpoint(0, bps[i].address, 1);
        }
    }
}

// ---------------------------------------------------------------------------
// IDebugTarget: Screen
// ---------------------------------------------------------------------------

ScreenData DebugAdapter::screenSnapshot()
{
    ScreenData data;
    uint32_t *pixels = tv.pixels();
    if (!pixels) return data;

    data.width  = DEFAULT_SCREEN_WIDTH;
    data.height = DEFAULT_SCREEN_HEIGHT;
    size_t total = static_cast<size_t>(data.width) * data.height;
    data.pixels.assign(pixels, pixels + total);
    return data;
}

// ---------------------------------------------------------------------------
// IDebugTarget: ROM / init
// ---------------------------------------------------------------------------

bool DebugAdapter::loadRom(const std::string &path, uint32_t org)
{
    std::vector<uint8_t> rom_data = util::load_binfile(path);
    if (rom_data.empty()) {
        printf("DebugAdapter::loadRom(): failed to load %s\n", path.c_str());
        return false;
    }

    printf("DebugAdapter::loadRom(): loaded %s (%zu bytes) at %04x\n",
           path.c_str(), rom_data.size(), org);

    memory.init_from_vector(rom_data, org);

    Options.pc = org;
    board.reset(Board::ResetMode::LOADROM);

    return true;
}

void DebugAdapter::initCpu(uint16_t pc, uint16_t sp)
{
    i8080_jump(pc);
    i8080_setreg_sp(sp);
    i8080_init();
}
