#pragma once

#include "agent_types.h"
#include "agent_log.h"
#include "idebug_backend.h"
#include "events.h"
#include "code_analyzer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// AgentApi — Stage 6.3
//
// AI Agent interface to the Vector-06C Debugger.
//
// All operations go through IDebugBackend — no direct access to Board,
// Memory, CPU, IO, TV, or any emulator internals.
//
// Stage 6.3 Contract:
//   - All public operations that can fail return AgentApiResult<T>
//   - ErrorCode is used consistently (including NotFound)
//   - No CommandResult exposed in public API
//   - No IDebugBackend types in public result structures
//   - std::optional<uint16_t> for getCallGraph (no $0000 ambiguity)
//   - Single loadRom() method (no loadRomInfo duplication)
// ---------------------------------------------------------------------------

class AgentApi
{
public:
    explicit AgentApi(IDebugBackend &backend);

    // -- Execution control (Stage 6.3: AgentApiResult<void>) -----------------

    AgentApiResult<void> run();
    AgentApiResult<void> pause();
    AgentApiResult<void> step();
    AgentApiResult<void> reset();
    bool isRunning() const;

    // -- CPU state (Stage 6.3: AgentApiResult<CpuState>) ---------------------

    AgentApiResult<CpuState> getCpuState();

    // -- Memory access (Stage 6.3: AgentApiResult + overflow checks) ---------

    AgentApiResult<std::vector<uint8_t>> readMemory(uint16_t address, size_t size);
    AgentApiResult<void> writeMemory(uint16_t address, const std::vector<uint8_t> &data);

    // -- I/O ports (Stage 6.3: uniform AgentApiResult) -----------------------

    AgentApiResult<uint8_t> readIo(uint8_t port);
    AgentApiResult<void> writeIo(uint8_t port, uint8_t value);

    // -- Breakpoints (Stage 6.3: AgentApiResult<void>) -----------------------

    AgentApiResult<void> setBreakpoint(uint16_t address);
    AgentApiResult<void> clearBreakpoint(uint16_t address);
    AgentApiResult<void> setBreakpointEnabled(uint16_t address, bool enabled);
    AgentApiResult<std::vector<DebuggerBreakpoint>> listBreakpoints();
    AgentApiResult<void> clearAllBreakpoints();

    // -- Registers -----------------------------------------------------------

    AgentApiResult<void> setRegister(const std::string &name, uint16_t value);

    // -- Disassembly ---------------------------------------------------------

    AgentApiResult<std::vector<DisassembledInstructionResult>>
    disassemble(uint16_t address, size_t count);

    // -- Code Analysis (Stage 6.16, 6.19) ------------------------------------

    AgentApiResult<CodeAnalysisResult>
    analyzeCode(uint16_t startAddress, size_t maxInstructions);

    AgentApiResult<CodeAnalysisResult>
    analyzeCode(const std::vector<uint16_t>& entryPoints, size_t maxInstructions);

    // -- Range Disassembly (Stage 6.18) --------------------------------------

    AgentApiResult<DisassembleRangeResult>
    disassembleRange(uint16_t address, uint16_t size);

    // -- Instruction History -------------------------------------------------

    AgentApiResult<std::vector<InstructionHistoryEntry>>
    getInstructionHistory(size_t count);

    // -- Trace / I/O (Stage 6.3: AgentApiResult) -----------------------------

    AgentApiResult<std::vector<InstructionEvent>> getExecutionTrace(size_t maxEntries = 1000);
    AgentApiResult<std::vector<IoAccessEvent>>    getIoTrace(size_t maxEntries = 1000);

    // -- Screen (Stage 6.3: AgentScreenSnapshot, no IDebugBackend types) -----

    AgentApiResult<AgentScreenSnapshot> getScreen();

    // -- Annotations (Stage 6.3: AgentApiResult<void>) -----------------------

    AgentApiResult<void> createFunction(uint16_t address, uint16_t size = 0);
    AgentApiResult<void> renameFunction(uint16_t address, const std::string &name);
    AgentApiResult<void> setFunctionComment(uint16_t address, const std::string &comment);
    AgentApiResult<void> deleteFunction(uint16_t address);
    AgentApiResult<void> addLabel(uint16_t address, const std::string &name);
    AgentApiResult<void> setComment(uint16_t address, const std::string &comment);
    AgentApiResult<void> applyAnnotation(const Annotation &annotation);

    // -- High-level analysis (Stage 6.3: AgentApiResult) ---------------------

    AgentApiResult<FunctionContext> getFunctionContext(uint16_t address);
    AgentApiResult<TraceResult>     traceFunction(uint16_t address);

    // -- Stack ---------------------------------------------------------------

    AgentApiResult<std::vector<StackEntry>>
    getStack(size_t limit);

    // -- Memory Map ----------------------------------------------------------

    AgentApiResult<std::vector<MemoryMapBlock>>
    getMemoryMap();

    // -- Screen Info ---------------------------------------------------------

    AgentApiResult<ScreenInfoResult> getScreenInfo();

    // -- VRAM Info -----------------------------------------------------------

    AgentApiResult<VramInfoResult> getVramInfo();

    // -- Symbols -------------------------------------------------------------

    AgentApiResult<std::vector<SymbolInfo>>
    getSymbols(size_t limit = 0);

    AgentApiResult<SymbolInfo> getFunction(uint16_t address);

    // -- Xrefs ---------------------------------------------------------------

    AgentApiResult<std::vector<XrefResult>>
    getXrefs(uint16_t address);

    // -- Call Graph (Stage 6.3: std::optional<uint16_t> — no $0000 ambiguity) -

    AgentApiResult<std::vector<CallGraphEdge>>
    getCallGraph(std::optional<uint16_t> address = std::nullopt, size_t limit = 0);

    // -- ROM (Stage 6.3: single loadRom method) ------------------------------

    AgentApiResult<LoadRomResult> loadRom(const std::string &path, uint32_t org = 0);

    // -- Debug State ---------------------------------------------------------

    AgentApiResult<DebugStateResult> getDebugState();

    // -- ROM Database (Stage 6.11) -------------------------------------------

    AgentApiResult<RdbInfoResult> getRdbInfo();

    AgentApiResult<std::vector<RdbObjectResult>> listRdbObjects(size_t limit = 0);

    AgentApiResult<RdbObjectResult> getRdbObject(uint16_t address);

    AgentApiResult<RdbObjectResult> findRdbObject(const std::string &name);

    AgentApiResult<void> addRdbObject(uint16_t address, const std::string &name,
                                      const std::string &type = "Label",
                                      uint32_t size = 0);

    AgentApiResult<void> updateRdbObject(uint16_t address, const std::string &name,
                                         const std::string &type,
                                         uint32_t size, bool hasSize);

    AgentApiResult<void> removeRdbObject(uint16_t address);

    AgentApiResult<void> setRdbComment(uint16_t address, const std::string &comment);

    AgentApiResult<void> setRdbProperty(uint16_t address,
                                        const std::string &propName,
                                        const std::string &propValue);

    AgentApiResult<void> saveRdb();

    AgentApiResult<void> reloadRdb();

    // -- RDB Links (Stage 6.13) -----------------------------------------------

    AgentApiResult<void> addRdbLink(uint16_t sourceAddress, uint16_t targetAddress);

    AgentApiResult<void> removeRdbLink(uint16_t sourceAddress, uint16_t targetAddress);

    AgentApiResult<std::vector<uint16_t>> getRdbLinks(uint16_t sourceAddress);

    // -- Agent log -----------------------------------------------------------

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
