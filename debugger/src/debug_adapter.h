#pragma once

#include "debug_target.h"
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

#include <string>

// ---------------------------------------------------------------------------
// DebugAdapter
//
// Adapter layer between Debugger Core and Vector Emulator.
// Implements IDebugTarget — the ONLY component with direct access to
// emulator internals (Board, Memory, CPU, IO, TV, etc.).
//
// Also owns the HAL function definitions (i8080_hal_*) that connect
// the CPU core to emulator components.
// ---------------------------------------------------------------------------

class DebugAdapter : public IDebugTarget
{
public:
    DebugAdapter();
    ~DebugAdapter() override;

    void init();
    void shutdown();

    // -- IDebugTarget implementation ----------------------------------------

    uint8_t readMemory(uint16_t addr) override;
    uint8_t peekMemory(uint16_t addr) override;
    uint8_t readMemoryRaw(uint16_t addr) override;
    void    writeMemory(uint16_t addr, uint8_t val) override;
    void setMemoryCallbacks(MemoryReadCallback onRead,
                            MemoryWriteCallback onWrite) override;

    CpuState getCpuState() override;
    void     writeCpuRegister(int reg, uint16_t val) override;

    void stepInstruction() override;
    void executeFrame() override;
    void reset(bool loadRom) override;

    void debuggerBreak() override;
    void debuggerContinue() override;
    void debuggerAttached() override;
    void debuggerDetached() override;
    void setPollCallback(std::function<void()> cb) override;

    void syncBreakpoints(const DebuggerBreakpoint *bps, size_t count) override;

    ScreenData screenSnapshot() override;
    PaletteSnapshot paletteSnapshot() const override;

    void pressKey(int scancode) override;
    void releaseKey(int scancode) override;

    bool loadRom(const std::string &path, uint32_t org) override;
    void initCpu(uint16_t pc, uint16_t sp) override;

    // -- HAL binding --------------------------------------------------------

    // Bind HAL callbacks to this adapter's components.
    void bindHal();

    // Set static HAL pointers from external code.
    static void setHalPointers(Memory *mem, IO *io, Board *board);

    // Static accessors for HAL functions.
    static Memory* halMemory() { return s_memory; }
    static IO*     halIo()     { return s_io; }
    static Board*  halBoard()  { return s_board; }

private:
    // -- Emulator components (hidden from outside) ----------------------------

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

    bool initialized_ = false;

    MemoryReadCallback  memReadCb_;
    MemoryWriteCallback memWriteCb_;
    std::function<void(uint32_t,uint32_t,bool,uint8_t)> prevMemOnRead_;
    std::function<void(uint32_t,uint32_t,bool,uint8_t)> prevMemOnWrite_;

    static Memory       *s_memory;
    static IO           *s_io;
    static Board        *s_board;
};
