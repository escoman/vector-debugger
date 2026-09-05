#pragma once

#include "debugger_types.h"

#include <cstdint>
#include <functional>
#include <string>

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
    virtual uint8_t peekMemory(uint16_t addr) = 0;  // read without callbacks (paused only)
    virtual uint8_t readMemoryRaw(uint16_t addr) = 0;  // read without callbacks (thread-safe)
    virtual void    writeMemory(uint16_t addr, uint8_t val) = 0;

    // Memory instrumentation callbacks.
    // DebugBackend provides lambdas that record memory access events.
    // The target installs them on the underlying Memory (if any).
    // Pass nullptr to clear (called by DebugBackend destructor).
    using MemoryReadCallback  = std::function<void(uint32_t virt, uint32_t phys, bool stack, uint8_t value)>;
    using MemoryWriteCallback = std::function<void(uint32_t virt, uint32_t phys, bool stack, uint8_t value)>;

    virtual void setMemoryCallbacks(MemoryReadCallback onRead,
                                    MemoryWriteCallback onWrite) {}

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

    // -- Palette ------------------------------------------------------------

    virtual PaletteSnapshot paletteSnapshot() const { return {}; }

    // -- Sound --------------------------------------------------------------

    virtual SoundSnapshot soundSnapshot() const { return {}; }
    virtual void setMuted(bool muted) { (void)muted; }

    // -- Keyboard injection -------------------------------------------------

    virtual void pressKey(int scancode) {}
    virtual void releaseKey(int scancode) {}
    virtual bool isRuslatMode() const { return false; }

    // -- ROM / init ---------------------------------------------------------

    virtual bool loadRom(const std::string &path, uint32_t org) = 0;
    virtual void initCpu(uint16_t pc, uint16_t sp) = 0;

    // -- Frame pacing -------------------------------------------------------
    // Real targets (DebugAdapter) execute full frames and need 50 Hz pacing.
    // Test targets (NoBoardTarget) execute one instruction per "frame" and
    // should run at full speed.

    virtual bool framePacingEnabled() const { return true; }
};
