#pragma once

#include "agent_types.h"
#include "agent_log.h"
#include "idebug_backend.h"
#include "events.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// AgentApi — Stage 5.3
//
// AI Agent interface to the Vector-06C Debugger.
//
// All operations go through IDebugBackend — no direct access to Board,
// Memory, CPU, IO, TV, or any emulator internals.
//
// Thread safety: state-changing operations (run, pause, step, reset,
// writeMemory, setBreakpoint) use the existing DebugBackend command
// protocol which serializes access to the emulation thread.
// ---------------------------------------------------------------------------

class AgentApi
{
public:
    explicit AgentApi(IDebugBackend &backend);

    // -- Execution control --------------------------------------------------

    void run();
    void pause();
    void step();
    void reset();

    // -- CPU state ----------------------------------------------------------

    CpuState getCpuState();

    // -- Memory access ------------------------------------------------------

    std::vector<uint8_t> readMemory(uint16_t address, size_t size);
    bool writeMemory(uint16_t address, const std::vector<uint8_t> &data);

    // -- Breakpoints --------------------------------------------------------

    int  setBreakpoint(uint16_t address);
    bool clearBreakpoint(uint16_t address);
    std::vector<DebuggerBreakpoint> listBreakpoints();

    // -- Trace / I/O / VRAM -------------------------------------------------

    std::vector<InstructionEvent> getExecutionTrace(size_t maxEntries = 1000);
    std::vector<IoAccessEvent>    getIoTrace(size_t maxEntries = 1000);
    IDebugBackend::ActivitySnapshot getVramActivity();

    // -- Screen -------------------------------------------------------------

    IDebugBackend::ScreenSnapshot getScreen();

    // -- Annotations (direct apply to SymbolDatabase) -----------------------

    bool createFunction(uint16_t address, uint16_t size = 0);
    bool renameFunction(uint16_t address, const std::string &name);
    bool setFunctionComment(uint16_t address, const std::string &comment);
    bool deleteFunction(uint16_t address);
    bool addLabel(uint16_t address, const std::string &name);
    bool setComment(uint16_t address, const std::string &comment);
    bool applyAnnotation(const Annotation &annotation);

    // -- High-level analysis ------------------------------------------------

    FunctionContext getFunctionContext(uint16_t address);
    TraceResult     traceFunction(uint16_t address);

    // -- ROM ----------------------------------------------------------------

    bool loadRom(const std::string &path, uint32_t org = 0);

    // -- Agent log ----------------------------------------------------------

    const AgentLog &log() const;
    void clearLog();

private:
    IDebugBackend &backend_;
    AgentLog       log_;

    // -- Internal helpers ---------------------------------------------------

    // Disassemble a function starting at 'address' until RET/HLT or
    // the next known symbol.  Returns the instruction list.
    std::vector<FunctionContext::Instruction> disassembleFunction(uint16_t address);

    // Estimate function size by linear disassembly until RET/HLT.
    uint16_t estimateFunctionSize(uint16_t address);

    // Analyze stack balance from execution trace for a function range.
    FunctionContext::StackBehavior analyzeStackBalance(
        uint16_t funcStart, uint16_t funcEnd);

    // Collect memory reads/writes from activity snapshot for an address range.
    void collectMemoryAccess(
        uint16_t start, uint16_t end,
        const IDebugBackend::ActivitySnapshot &activity,
        std::vector<FunctionContext::MemoryAccess> &reads,
        std::vector<FunctionContext::MemoryAccess> &writes);

    // Collect I/O accesses from IO history for instructions within range.
    void collectIoAccess(
        uint16_t funcStart, uint16_t funcEnd,
        const std::vector<IoAccessEvent> &ioHistory,
        std::vector<FunctionContext::IoAccess> &ioAccesses);

    // Collect VRAM writes from activity snapshot.
    void collectVramWrites(
        const IDebugBackend::ActivitySnapshot &activity,
        std::vector<FunctionContext::VramWrite> &vramWrites);

    // Timer helper for logging.
    static double elapsedMs(std::chrono::steady_clock::time_point start);
};
