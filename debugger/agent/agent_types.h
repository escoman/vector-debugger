#pragma once

#include "debugger_types.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Agent Types — Stage 5.3.1
//
// Data structures used by the Agent API for high-level analysis results,
// annotations, and operation logging.
//
// CommandResult and ExitReason are defined in debugger_types.h (shared
// with IDebugBackend).
//
// No dependency on Board, Memory, CPU, IO, ImGui, or SDL.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Trace-attributed access types (Sections 11, 12, 13)
//
// Each access is tied to the PC of the instruction that caused it.
// These are facts from a concrete execution experiment, not global counters.
// ---------------------------------------------------------------------------

struct TraceMemoryAccess
{
    uint16_t pc = 0;
    bool hasPc = true;         // Stage 5.3.3: distinguishes PC=0000 from Unknown
    uint16_t address = 0;
    enum Type { Read, Write, Fetch };
    Type type = Read;
    uint8_t value = 0;
};

struct TraceIoAccess
{
    uint16_t pc = 0;
    bool hasPc = true;         // Stage 5.3.3: distinguishes PC=0000 from Unknown
    uint8_t port = 0;
    bool isOutput = false;
    uint8_t value = 0;
};

struct TraceVramWrite
{
    uint16_t pc = 0;
    bool hasPc = true;         // Stage 5.3.3: distinguishes PC=0000 from Unknown
    uint16_t address = 0;
    uint8_t value = 0;
};

// ---------------------------------------------------------------------------
// DataSource — provenance of a piece of information (Sections 14, 15)
//
// Every dynamic fact must have a source.  Heuristic/incomplete data must
// NOT be presented as confirmed debugger facts.
// ---------------------------------------------------------------------------

enum class DataSource
{
    Disassembler,
    SymbolDatabase,
    XrefDatabase,
    Trace,
    Unknown
};

// ---------------------------------------------------------------------------
// FunctionContext — result of getFunctionContext()
//
// Contains all available information about a function at a given address:
// disassembled instructions, cross-references, memory/IO/VRAM access
// from trace, and stack behavior analysis.
//
// Every dynamic fact has a source (DataSource).  If trace data is
// unavailable, dynamic fields are empty and source is Unknown.
// ---------------------------------------------------------------------------

struct FunctionContext
{
    uint16_t address = 0;
    uint16_t size = 0;            // 0 if unknown
    std::string name;             // from SymbolDatabase or auto-name
    std::string comment;

    // -- Disassembled instructions ------------------------------------------

    struct Instruction
    {
        uint16_t address = 0;
        std::string text;         // e.g. "CALL 0x8230"
        uint8_t  bytes[3] = {};
        uint8_t  length = 0;
    };
    std::vector<Instruction> instructions;

    // Whether the function boundary is heuristic (linear sweep without
    // full CFG).  True when the boundary was not confirmed by trace.
    bool isHeuristic = true;

    // -- Cross-references ---------------------------------------------------

    std::vector<uint16_t> callers;    // addresses that CALL this function
    std::vector<uint16_t> callees;    // addresses this function CALLs

    // -- Memory access (from trace) -----------------------------------------

    std::vector<TraceMemoryAccess> memoryReads;
    std::vector<TraceMemoryAccess> memoryWrites;
    DataSource memorySource = DataSource::Unknown;

    // -- I/O access (from trace) --------------------------------------------

    std::vector<TraceIoAccess> ioAccesses;
    DataSource ioSource = DataSource::Unknown;

    // -- VRAM writes (from trace) -------------------------------------------

    std::vector<TraceVramWrite> vramWrites;
    DataSource vramSource = DataSource::Unknown;

    // -- Stack behavior -----------------------------------------------------

    enum StackBehavior { Balanced, Unbalanced, Unknown };
    StackBehavior stackBehavior = Unknown;

    uint16_t entrySp = 0;
    uint16_t exitSp = 0;
    uint16_t minSp = 0;
    uint16_t maxSp = 0;
    int callDepth = 0;
};

// ---------------------------------------------------------------------------
// TraceResult — result of traceFunction()
//
// Contains dynamic execution data from a REAL execution experiment:
// entry/exit PCs, executed addresses, attributed memory/IO/VRAM events,
// stack tracking, and exit reason.
// ---------------------------------------------------------------------------

struct TraceResult
{
    uint16_t entryPc = 0;
    uint16_t exitPc = 0;

    uint32_t instructionCount = 0;
    uint32_t executionCount = 0;

    // All PCs executed during the trace (in order, with repeats)
    std::vector<uint16_t> executedPcs;

    // Attributed memory accesses (from the trace experiment)
    std::vector<TraceMemoryAccess> memoryReads;
    std::vector<TraceMemoryAccess> memoryWrites;

    // Attributed I/O accesses
    std::vector<TraceIoAccess> ioReads;
    std::vector<TraceIoAccess> ioWrites;

    // Attributed VRAM writes
    std::vector<TraceVramWrite> vramWrites;

    // Functions called during the trace
    std::vector<uint16_t> calledFunctions;

    // Stack tracking
    uint16_t entrySp = 0;
    uint16_t exitSp = 0;
    uint16_t minSp = 0;
    uint16_t maxSp = 0;
    int callDepth = 0;

    // How the trace ended
    ExitReason exitReason = ExitReason::Unknown;
};

// ---------------------------------------------------------------------------
// Annotation — AI proposal for symbol database changes
//
// confidence is the AI's self-assessment (0.0 = pure guess, 1.0 = certain).
// It is NOT a debugger-computed fact — it's the AI's opinion.
// ---------------------------------------------------------------------------

struct Annotation
{
    enum Type { Function, Label, Comment, Rename };
    Type type = Function;
    uint16_t address = 0;
    std::string name;
    std::string comment;
    double confidence = 1.0;
};

// ---------------------------------------------------------------------------
// AgentLogEntry — single record in the Agent operation journal
//
// Allows replay and analysis of the AI agent's decision-making process.
// ---------------------------------------------------------------------------

struct AgentLogEntry
{
    std::chrono::steady_clock::time_point timestamp;
    std::string tool;
    std::string arguments;
    std::string result;
    double executionTimeMs = 0;
    bool success = true;
    std::string error;
};
