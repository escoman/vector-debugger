#pragma once

#include "debugger_types.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Agent Types — Stage 5.3.1 / Stage 6.1
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
// ErrorCode — Stage 6.1
//
// Uniform error classification for all Agent API operations.
// Designed for future JSON/MCP serialization.
// ---------------------------------------------------------------------------

enum class ErrorCode
{
    None = 0,
    InvalidArgument,
    InvalidAddress,
    InvalidRange,
    NoRomLoaded,
    NotPaused,
    NotRunning,
    OperationFailed,
    Timeout,
    Unsupported,
    NotFound            // Stage 6.3: object not found by valid key (symbol, breakpoint)
};

// ---------------------------------------------------------------------------
// AgentApiResult<T> — Stage 6.1
//
// Uniform result wrapper for all Agent API operations.
// Every operation returns success/failure + typed value or error info.
// JSON-ready: no pointers, no std::function, no internal types.
//
// Usage:
//   auto result = api.someOperation();
//   if (result.success) { use(result.value); }
//   else { handle(result.error_code, result.error_message); }
// ---------------------------------------------------------------------------

template <typename T>
struct AgentApiResult
{
    bool        success = false;
    T           value{};
    ErrorCode   error_code = ErrorCode::None;
    std::string error_message;

    // Factory: success
    static AgentApiResult ok(T val) {
        AgentApiResult r;
        r.success = true;
        r.value = std::move(val);
        return r;
    }

    // Factory: success (void-like, no value)
    static AgentApiResult ok() {
        AgentApiResult r;
        r.success = true;
        return r;
    }

    // Factory: failure
    static AgentApiResult fail(ErrorCode code, std::string message) {
        AgentApiResult r;
        r.success = false;
        r.error_code = code;
        r.error_message = std::move(message);
        return r;
    }
};

// Specialization for void — no value field used
// Usage: AgentApiResult<void> for operations that don't return data
template <>
struct AgentApiResult<void>
{
    bool        success = false;
    ErrorCode   error_code = ErrorCode::None;
    std::string error_message;

    static AgentApiResult ok() {
        AgentApiResult r;
        r.success = true;
        return r;
    }

    static AgentApiResult fail(ErrorCode code, std::string message) {
        AgentApiResult r;
        r.success = false;
        r.error_code = code;
        r.error_message = std::move(message);
        return r;
    }
};

// ---------------------------------------------------------------------------
// AgentScreenSnapshot — Stage 6.3
//
// MCP-ready screen snapshot. No dependency on IDebugBackend::ScreenSnapshot.
// ---------------------------------------------------------------------------

struct AgentScreenSnapshot
{
    std::vector<uint32_t> pixels;  // ARGB8888
    int width  = 0;
    int height = 0;
};

// ---------------------------------------------------------------------------
// Safe maximum limits — Stage 6.3
//
// Prevent unbounded memory allocation from Agent API requests.
// ---------------------------------------------------------------------------

namespace AgentLimits {
    static const size_t MAX_DISASSEMBLY_COUNT  = 10000;
    static const size_t MAX_SYMBOLS_LIMIT      = 10000;
    static const size_t MAX_CALL_GRAPH_LIMIT   = 10000;
    static const size_t MAX_STACK_LIMIT        = 1000;
    static const size_t MAX_TRACE_ENTRIES      = 100000;
    static const size_t MAX_HISTORY_ENTRIES    = 100000;
    static const size_t MAX_RDB_OBJECTS_LIMIT  = 10000;
    static const size_t MAX_MEMORY_READ_RANGE  = 65536;  // 64 KB (Stage 6.20: full address space)
    static const size_t MAX_CODE_ANALYSIS_INSTRUCTIONS = 10000;
    static const size_t MAX_ANALYSIS_ENTRY_POINTS = 256;
    static const size_t MAX_MEMORY_ACCESS_LOG  = 50000;  // Stage 6.20: bounded access log
    static const size_t MAX_MEMORY_SNAPSHOTS   = 16;     // Stage 6.20: max concurrent snapshots
}

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

// ---------------------------------------------------------------------------
// DisassembledInstructionResult — Stage 6.1 (§10)
//
// Single instruction from disassemble(address, count).
// JSON-ready: no pointers, no internal types.
// ---------------------------------------------------------------------------

struct DisassembledInstructionResult
{
    uint16_t    address = 0;
    uint16_t    next_address = 0;
    std::vector<uint8_t> bytes;
    std::string mnemonic;
    std::string operands;
    std::string text;   // "MNEMONIC OPERANDS"
};

// ---------------------------------------------------------------------------
// InstructionHistoryEntry — Stage 6.1 (§11)
//
// Single entry from getInstructionHistory(count).
// Distinct from getExecutionTrace() — this is the last N executed
// instructions, not a filtered/limited trace.
// ---------------------------------------------------------------------------

struct InstructionHistoryEntry
{
    uint16_t address = 0;
    std::vector<uint8_t> bytes;
    std::string disassembly;
    uint16_t next_address = 0;
};

// ---------------------------------------------------------------------------
// StackEntry — Stage 6.1 (§13)
//
// Single entry from getStack(limit).
// Reads 16-bit values from memory starting at SP.
// ---------------------------------------------------------------------------

struct StackEntry
{
    uint16_t    address = 0;     // memory address of this stack slot
    uint16_t    value = 0;       // 16-bit value at this address
    std::string symbol;          // symbol name if known, empty otherwise
};

// ---------------------------------------------------------------------------
// MemoryMapBlock — Stage 6.1 (§18)
//
// Single block from getMemoryMap().
// The full 64 KB is divided into 256 blocks of 256 bytes each.
// ---------------------------------------------------------------------------

struct MemoryMapBlock
{
    uint16_t start = 0;
    uint16_t end = 0;            // inclusive
    enum class Classification { Unknown, Code, Data };
    Classification classification = Classification::Unknown;
    uint64_t read_activity = 0;
    uint64_t write_activity = 0;
    uint64_t execute_activity = 0;
    bool     has_content = false; // true if any byte != 0
};

// ---------------------------------------------------------------------------
// ScreenInfoResult — Stage 6.1 (§20)
//
// Structured screen state from getScreenInfo().
// No pixel data — just mode parameters.
// ---------------------------------------------------------------------------

struct ScreenInfoResult
{
    int      width = 0;
    int      height = 0;
    int      visible_width = 0;
    int      visible_height = 0;
    bool     mode512 = false;
    int      scroll_value = 0;
    uint16_t vram_base = 0xC000;
    int      pixels_per_byte = 8;
};

// ---------------------------------------------------------------------------
// VramPlaneInfo — Stage 6.1 (§19)
//
// Describes one VRAM bit-plane region.
// ---------------------------------------------------------------------------

struct VramPlaneInfo
{
    int      plane = 0;          // 0..3
    uint16_t address = 0;        // base address of this plane
    uint16_t size = 0;           // size in bytes (typically 8192)
};

// ---------------------------------------------------------------------------
// VramInfoResult — Stage 6.1 (§19)
//
// Result of getVramInfo().
// Describes the VRAM layout for the current video mode.
// ---------------------------------------------------------------------------

struct VramInfoResult
{
    bool     mode512 = false;
    uint16_t vram_base = 0xC000;
    int      scroll_value = 0;
    std::vector<VramPlaneInfo> planes;
};

// ---------------------------------------------------------------------------
// SymbolInfo — Stage 6.1 (§15)
//
// Single entry from getSymbols().
// ---------------------------------------------------------------------------

struct SymbolInfo
{
    uint16_t    address = 0;
    std::string name;
    enum class Type { Function, Label };
    Type        type = Type::Function;
    std::string comment;
};

// ---------------------------------------------------------------------------
// XrefResult — Stage 6.1 (§16)
//
// Single entry from getXrefs().
// ---------------------------------------------------------------------------

struct XrefResult
{
    uint16_t from = 0;
    uint16_t to = 0;
};

// ---------------------------------------------------------------------------
// CallGraphEdge — Stage 6.1 (§17)
//
// Single entry from getCallGraph().
// ---------------------------------------------------------------------------

struct CallGraphEdge
{
    uint16_t from = 0;
    uint16_t to = 0;
};

// ---------------------------------------------------------------------------
// LoadRomResult — Stage 6.1 (§4)
//
// Structured result of loadRomInfo().
// ---------------------------------------------------------------------------

struct LoadRomResult
{
    std::string path;
    uint32_t    origin = 0;
    uint16_t    pc = 0;
};

// ---------------------------------------------------------------------------
// DebugStateResult — Stage 6.1 (§21)
//
// High-level debugger state from getDebugState().
// One-call snapshot for the AI agent.
// ---------------------------------------------------------------------------

struct DebugStateResult
{
    bool        running = false;
    CpuState    cpu{};
    std::vector<DebuggerBreakpoint> breakpoints;
    std::string current_instruction;   // disassembly at PC
    std::string current_function;      // symbol name at PC (if any)
};

// ---------------------------------------------------------------------------
// RDB types — Stage 6.11
// ---------------------------------------------------------------------------

struct RdbInfoResult
{
    std::string path;
    std::string platform;
    int         version = 0;
    bool        loaded = false;
    bool        dirty = false;
    bool        existsOnDisk = false;
    size_t      objectCount = 0;
    std::string romFile;
    uint64_t    romSize = 0;
    std::string romSha256;
};

struct RdbObjectResult
{
    uint16_t    address = 0;
    std::string type;        // "Function", "Variable", "Label", etc.
    std::string name;
    uint32_t    size = 0;
    bool        hasSize = false;
    std::string comment;
    std::vector<uint16_t> links;
    std::map<std::string, std::string> properties;  // simplified: all as strings
};

// ---------------------------------------------------------------------------
// Runtime Memory Access types — Stage 6.20
//
// Runtime Memory Access Map: 256 blocks × 256 bytes covering 64K.
// Tracks READ/WRITE/FETCH with per-block counters.
// These are evidence from a concrete execution session, not static facts.
// ---------------------------------------------------------------------------

struct RuntimeAccessBlock
{
    uint16_t address = 0;      // block start address (aligned to 256)
    bool read   = false;
    bool write  = false;
    bool fetch  = false;
    uint64_t read_count  = 0;
    uint64_t write_count = 0;
    uint64_t fetch_count = 0;
};

// ---------------------------------------------------------------------------
// Runtime Access Log Entry — Stage 6.20
//
// Single entry from the bounded runtime memory access log.
// Each entry records an individual memory access with PC attribution.
// ---------------------------------------------------------------------------

struct RuntimeAccessLogEntry
{
    uint16_t address = 0;
    enum Type { Read, Write, Fetch };
    Type type = Read;
    uint16_t pc = 0;
    uint8_t value = 0;
};

// ---------------------------------------------------------------------------
// Memory Snapshot types — Stage 6.20
//
// Snapshots capture the full 64K (or a subrange) address space at a point
// in time.  They are independent of RDB and are invalidated on ROM load.
// ---------------------------------------------------------------------------

struct MemorySnapshotData
{
    uint32_t snapshot_id = 0;
    uint16_t start_address = 0;
    std::vector<uint8_t> data;
};

struct MemorySnapshotDiff
{
    struct ChangedRange
    {
        uint16_t address = 0;
        size_t   size    = 0;
    };
    std::vector<ChangedRange> changed_ranges;
};

// ---------------------------------------------------------------------------
// DisassembledRangeInstruction — Stage 6.18
//
// Single instruction from disassembleRange(address, size).
// Includes branch target and type information for control-flow instructions.
// ---------------------------------------------------------------------------

struct DisassembledRangeInstruction
{
    uint16_t    address = 0;
    std::vector<uint8_t> bytes;
    std::string mnemonic;
    std::string operands;
    uint8_t     size = 0;
    std::optional<uint16_t> branch_target;  // null for non-branch instructions
    std::string branch_type;                // "JMP", "JCC", "CALL", "RET", "RST", or empty
};

// ---------------------------------------------------------------------------
// DisassembleRangeResult — Stage 6.18
//
// Result of disassembleRange(address, size).
// Contains sequentially disassembled instructions from a memory range.
// ---------------------------------------------------------------------------

struct DisassembleRangeResult
{
    uint16_t startAddress = 0;
    uint16_t size = 0;
    std::vector<DisassembledRangeInstruction> instructions;
    bool incomplete_instruction = false;  // true if last instruction extends beyond range
};
