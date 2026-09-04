#pragma once

#include "agent_types.h"
#include "agent_log.h"
#include "idebug_backend.h"
#include "events.h"

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// AgentApi — Stage 5.3.1
//
// AI Agent interface to the Vector-06C Debugger.
//
// All operations go through IDebugBackend — no direct access to Board,
// Memory, CPU, IO, TV, or any emulator internals.
//
// State-changing operations use the Backend command protocol:
//   - Breakpoint mutations: requestAddBreakpoint / requestRemoveBreakpoint
//   - Annotation mutations: requestCreateFunction / requestRenameSymbol etc.
//
// Read-only operations (getCpuState, readMemory, symbolDatabase() const)
// are safe snapshot queries.
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

    // -- Breakpoints (through command protocol) -----------------------------

    CommandResult setBreakpoint(uint16_t address);
    CommandResult clearBreakpoint(uint16_t address);
    CommandResult setBreakpointEnabled(uint16_t address, bool enabled);
    std::vector<DebuggerBreakpoint> listBreakpoints();

    // -- Trace / I/O / VRAM -------------------------------------------------

    std::vector<InstructionEvent> getExecutionTrace(size_t maxEntries = 1000);
    std::vector<IoAccessEvent>    getIoTrace(size_t maxEntries = 1000);

    // -- Screen -------------------------------------------------------------

    IDebugBackend::ScreenSnapshot getScreen();

    // -- Annotations (through command protocol) -----------------------------

    CommandResult createFunction(uint16_t address, uint16_t size = 0);
    CommandResult renameFunction(uint16_t address, const std::string &name);
    CommandResult setFunctionComment(uint16_t address, const std::string &comment);
    CommandResult deleteFunction(uint16_t address);
    CommandResult addLabel(uint16_t address, const std::string &name);
    CommandResult setComment(uint16_t address, const std::string &comment);
    CommandResult applyAnnotation(const Annotation &annotation);

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

    // Disassemble a function starting at 'address' until RET/HLT/unconditional
    // JMP or the next known symbol.  Returns the instruction list.
    std::vector<FunctionContext::Instruction> disassembleFunction(uint16_t address);

    // Estimate function size by linear disassembly until RET/HLT/JMP.
    uint16_t estimateFunctionSize(uint16_t address);

    // Collect attributed memory/IO/VRAM events from history using
    // instruction sequence range [startSeq, endSeq).
    void collectTraceEvents(
        uint64_t startSeq, uint64_t endSeq,
        std::vector<TraceMemoryAccess> &memReads,
        std::vector<TraceMemoryAccess> &memWrites,
        std::vector<TraceIoAccess> &ioReads,
        std::vector<TraceIoAccess> &ioWrites,
        std::vector<TraceVramWrite> &vramWrites);

    // Timer helper for logging.
    static double elapsedMs(std::chrono::steady_clock::time_point start);
};
