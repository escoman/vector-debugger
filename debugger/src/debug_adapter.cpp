#include "debug_adapter.h"
#include "backend.h"       // for DebuggerBreakpoint
#include "debug_memory.h"
#include "rom_load_address.h"
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

    // In the main emulator, onframetimer triggers frame execution via the
    // event queue. In the debugger, the emulation thread runs independently,
    // so we set it to a no-op to avoid std::bad_function_call from the
    // SDL audio callback.
    board.onframetimer = []() { /* no-op in debugger */ };

    // Unpause SDL audio device to start receiving callbacks
    soundnik.pause(0);

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

uint8_t DebugAdapter::readMemoryRaw(uint16_t addr)
{
    return memory.peek(addr, false);
}

void DebugAdapter::writeMemory(uint16_t addr, uint8_t val)
{
    memory.write(addr, val, false);
}

// ---------------------------------------------------------------------------
// IDebugTarget: Memory instrumentation callbacks
// ---------------------------------------------------------------------------

void DebugAdapter::setMemoryCallbacks(MemoryReadCallback onRead,
                                      MemoryWriteCallback onWrite)
{
    memReadCb_  = onRead;
    memWriteCb_ = onWrite;

    if (onRead) {
        // Save previous callbacks for chaining
        prevMemOnRead_  = memory.onread;
        prevMemOnWrite_ = memory.onwrite;

        memory.onread = [this](uint32_t virt, uint32_t phys,
                               bool stack, uint8_t value) {
            if (memReadCb_) memReadCb_(virt, phys, stack, value);
            if (prevMemOnRead_) prevMemOnRead_(virt, phys, stack, value);
        };

        memory.onwrite = [this](uint32_t virt, uint32_t phys,
                                bool stack, uint8_t value) {
            if (memWriteCb_) memWriteCb_(virt, phys, stack, value);
            if (prevMemOnWrite_) prevMemOnWrite_(virt, phys, stack, value);
        };
    } else {
        // Clear: restore previous callbacks
        memory.onread  = prevMemOnRead_;
        memory.onwrite = prevMemOnWrite_;
        prevMemOnRead_  = nullptr;
        prevMemOnWrite_ = nullptr;
    }
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
    board.single_step(true);  // true = update screen (for Vector Screen snapshot)
}

void DebugAdapter::executeFrame()
{
    board.execute_frame_with_cadence(true, false);
}

void DebugAdapter::reset(bool loadRom)
{
    if (loadRom) {
        board.reset(Board::ResetMode::LOADROM);
    } else {
        // BLK+СБР: detach boot ROM, reset CPU to hardware start state.
        // PC=0 is set by i8080_init() inside Board::reset().
        board.reset(Board::ResetMode::BLKSBR);
        // Set SP to a known value (matches LOADROM behavior).
        i8080_setreg_sp(0xc300);
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

PaletteSnapshot DebugAdapter::paletteSnapshot() const
{
    PaletteSnapshot snap;
    snap.count = 16;
    for (int i = 0; i < 16; ++i) {
        uint8_t raw = io.RawPaletteByte(i);
        // Vector-06C palette byte: B:7-6 G:5-3 R:2-0
        int r3 = raw & 0x07;         // 3 bits, 0-7
        int g3 = (raw >> 3) & 0x07;  // 3 bits, 0-7
        int b2 = (raw >> 6) & 0x03;  // 2 bits, 0-3
        snap.entries[i].r = static_cast<uint8_t>(r3 * 255 / 7);
        snap.entries[i].g = static_cast<uint8_t>(g3 * 255 / 7);
        snap.entries[i].b = static_cast<uint8_t>(b2 * 255 / 3);
        snap.entries[i].rawByte = raw;
    }
    return snap;
}

SoundSnapshot DebugAdapter::soundSnapshot() const
{
    SoundSnapshot snap;
    snap.available = true;

    // AY registers are accessed via IO ports 0x14 (data) and 0x15 (address)
    IO &ioRef = const_cast<IO&>(io);

    // Read all 16 AY registers
    for (int i = 0; i < 16; ++i) {
        ioRef.realoutput(0x15, i);   // Select register
        snap.registers[i] = static_cast<uint8_t>(ioRef.input(0x14));
    }

    // Parse mixer register (7):
    //   bits 0-2: tone enable (inverted: 0=enabled, 1=disabled)
    //   bits 3-5: noise enable (inverted)
    uint8_t mixer = snap.registers[7];
    snap.toneAEnabled = !(mixer & 0x01);
    snap.toneBEnabled = !(mixer & 0x02);
    snap.toneCEnabled = !(mixer & 0x04);
    snap.noiseAEnabled = !(mixer & 0x08);
    snap.noiseBEnabled = !(mixer & 0x10);
    snap.noiseCEnabled = !(mixer & 0x20);

    return snap;
}

void DebugAdapter::setMuted(bool muted)
{
    // Use soundnik.pause() to mute/unmute audio output
    // pause(1) = paused (muted), pause(0) = playing
    soundnik.pause(muted ? 1 : 0);
}

// ---------------------------------------------------------------------------
// IDebugTarget: Keyboard injection
// ---------------------------------------------------------------------------

void DebugAdapter::pressKey(int scancode)
{
    keyboard.apply_key(static_cast<SDL_Scancode>(scancode), false);
}

void DebugAdapter::releaseKey(int scancode)
{
    keyboard.apply_key(static_cast<SDL_Scancode>(scancode), true);
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

    // Auto-detect load address from file extension if org not explicitly given
    if (org == 0 && path.find('.') != std::string::npos) {
        org = getRomLoadAddress(path);
    }

    printf("DebugAdapter::loadRom(): loaded %s (%zu bytes) at %04x\n",
           path.c_str(), rom_data.size(), org);

    memory.init_from_vector(rom_data, org);

    // Reset CPU: boot ROM detached, PC=0, SP=0xc300 (BLK+СБР semantics).
    // We must NOT set PC to the ROM load address — after BLK+СБР the CPU
    // always starts at PC=0000 and the program is reached via vector table.
    Options.pc = 0;
    board.reset(Board::ResetMode::LOADROM);
    // LOADROM sets SP=0xc300 and i8080_init() sets PC=0.

    return true;
}

void DebugAdapter::initCpu(uint16_t pc, uint16_t sp)
{
    i8080_jump(pc);
    i8080_setreg_sp(sp);
    i8080_init();
}
