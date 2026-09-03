#pragma once

#include "debugger_types.h"

#include <cstdint>
#include <functional>
#include <string>

class Memory;

// ---------------------------------------------------------------------------
// IDebugTarget — interface between Debugger Core and the emulator Adapter.
//
// DebugBackend communicates ONLY through this interface.
// The real implementation is DebugAdapter (full Vector emulator).
// Test implementations (NoBoardTarget) provide CPU + Memory only.
// ---------------------------------------------------------------------------

class IDebugTarget
{
public:
    virtual ~IDebugTarget() = default;

    // -- Memory access ------------------------------------------------------

    virtual uint8_t readMemory(uint16_t addr) = 0;
    virtual uint8_t peekMemory(uint16_t addr) = 0;  // read without callbacks
    virtual void    writeMemory(uint16_t addr, uint8_t val) = 0;

    // Raw Memory pointer for callback installation.
    // Returns nullptr when direct memory callbacks are not needed
    // (e.g. real emulator uses HAL instrumentation instead).
    virtual Memory* rawMemory() { return nullptr; }

    // -- CPU state ----------------------------------------------------------

    virtual CpuState getCpuState() = 0;
    virtual void     writeCpuRegister(int reg, uint16_t val) = 0;

    // -- Execution control --------------------------------------------------

    virtual void stepInstruction() = 0;
    virtual void executeFrame() = 0;
    virtual void reset(bool loadRom) = 0;

    // -- Debugger control ---------------------------------------------------

    virtual void debuggerBreak() = 0;
    virtual void debuggerContinue() = 0;
    virtual void debuggerAttached() = 0;
    virtual void debuggerDetached() = 0;
    virtual void setPollCallback(std::function<void()> cb) = 0;

    // -- Breakpoints --------------------------------------------------------

    virtual void syncBreakpoints(const struct DebuggerBreakpoint *bps, size_t count) = 0;

    // -- Screen -------------------------------------------------------------

    virtual ScreenData screenSnapshot() = 0;

    // -- ROM / init ---------------------------------------------------------

    virtual bool loadRom(const std::string &path, uint32_t org) = 0;
    virtual void initCpu(uint16_t pc, uint16_t sp) = 0;
};
