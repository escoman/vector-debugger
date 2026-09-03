#pragma once

#include "debug_target.h"

class Memory;

// ---------------------------------------------------------------------------
// NoBoardTarget — minimal IDebugTarget for tests.
//
// Provides CPU (i8080 global state) + Memory without requiring Board.
// Replaces the old DEBUGGER_NO_BOARD conditional compilation in DebugBackend.
// ---------------------------------------------------------------------------

class NoBoardTarget : public IDebugTarget
{
public:
    NoBoardTarget(Memory &memory);
    ~NoBoardTarget() override;

    uint8_t readMemory(uint16_t addr) override;
    uint8_t peekMemory(uint16_t addr) override;
    void    writeMemory(uint16_t addr, uint8_t val) override;
    Memory* rawMemory() override;

    CpuState getCpuState() override;
    void     writeCpuRegister(int reg, uint16_t val) override;

    void stepInstruction() override;
    void executeFrame() override;
    void reset(bool loadRom) override;

    void debuggerBreak() override {}
    void debuggerContinue() override {}
    void debuggerAttached() override {}
    void debuggerDetached() override {}
    void setPollCallback(std::function<void()> cb) override {}

    void syncBreakpoints(const struct DebuggerBreakpoint *bps, size_t count) override {}

    ScreenData screenSnapshot() override { return {}; }

    bool loadRom(const std::string &path, uint32_t org) override;
    void initCpu(uint16_t pc, uint16_t sp) override;

private:
    Memory &memory_;
    bool    cpuInitialized_ = false;
};
