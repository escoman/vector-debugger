#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Agent Types — Stage 5.3
//
// Data structures used by the Agent API for high-level analysis results,
// annotations, and operation logging.
//
// No dependency on Board, Memory, CPU, IO, ImGui, or SDL.
// Only depends on standard library types.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// FunctionContext — result of getFunctionContext()
//
// Contains all available information about a function at a given address:
// disassembled instructions, cross-references, memory/IO/VRAM access
// statistics, and stack behavior analysis.
// ---------------------------------------------------------------------------

struct FunctionContext
{
    uint16_t address = 0;
    uint16_t size = 0;            // 0 if unknown
    std::string name;             // from SymbolDatabase or auto-name
    std::string comment;

    // -- Disassembled instruction -------------------------------------------

    struct Instruction
    {
        uint16_t address = 0;
        std::string text;         // e.g. "CALL 0x8230"
        uint8_t  bytes[3] = {};
        uint8_t  length = 0;
    };
    std::vector<Instruction> instructions;

    // -- Cross-references ---------------------------------------------------

    std::vector<uint16_t> callers;    // addresses that CALL this function
    std::vector<uint16_t> callees;    // addresses this function CALLs

    // -- Memory access statistics -------------------------------------------

    struct MemoryAccess
    {
        uint16_t address = 0;
        uint64_t count = 0;
    };
    std::vector<MemoryAccess> memoryReads;
    std::vector<MemoryAccess> memoryWrites;

    // -- I/O access statistics ----------------------------------------------

    struct IoAccess
    {
        uint8_t port = 0;
        uint64_t count = 0;
        bool isOutput = false;
    };
    std::vector<IoAccess> ioAccesses;

    // -- VRAM write statistics ----------------------------------------------

    struct VramWrite
    {
        uint16_t vramAddr = 0;
        uint64_t count = 0;
    };
    std::vector<VramWrite> vramWrites;

    // -- Stack behavior -----------------------------------------------------

    enum StackBehavior { Balanced, Unbalanced, Unknown };
    StackBehavior stackBehavior = Unknown;
};

// ---------------------------------------------------------------------------
// TraceResult — result of traceFunction()
//
// Contains dynamic execution data for a single function invocation:
// entry/exit PCs, execution count, called functions, and resource access.
// ---------------------------------------------------------------------------

struct TraceResult
{
    uint16_t entryPc = 0;
    uint16_t exitPc = 0;
    uint64_t executionCount = 0;

    std::vector<uint16_t> calledFunctions;

    uint16_t spEntry = 0;
    uint16_t spExit = 0;

    std::vector<FunctionContext::MemoryAccess> memoryReads;
    std::vector<FunctionContext::MemoryAccess> memoryWrites;
    std::vector<FunctionContext::IoAccess> ioAccesses;
    std::vector<FunctionContext::VramWrite> vramWrites;
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
};
