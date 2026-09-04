#pragma once

#include "debugger_types.h"
#include "symbol_database.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <future>
#include <string>
#include <vector>

// Forward declarations for event types (defined in events.h)
struct InstructionEvent;
struct MemoryAccessEvent;
struct IoAccessEvent;

// ---------------------------------------------------------------------------
// IDebugBackend — interface for the GUI layer.
//
// GUI code communicates ONLY through this interface.
// DebugBackend is the concrete implementation.
// No dependency on Board, Memory, CPU, IO, or any emulator internals.
// ---------------------------------------------------------------------------

class IDebugBackend
{
public:
    virtual ~IDebugBackend() = default;

    // -- Register identifiers -----------------------------------------------

    enum class RegisterId { AF, BC, DE, HL, SP, PC };

    // -- Snapshot types -----------------------------------------------------

    struct ScreenSnapshot
    {
        std::vector<uint32_t> pixels;  // ARGB8888
        int width  = 0;
        int height = 0;
    };

    struct ActivitySnapshot
    {
        std::vector<uint64_t> executeCount;
        std::vector<uint64_t> readCount;
        std::vector<uint64_t> writeCount;
    };

    struct VideoModeSnapshot
    {
        bool mode512 = false;
        int screenWidth  = 576;
        int screenHeight = 288;
        int visibleWidth  = 512;
        int visibleHeight = 256;
        int borderLeft = 32;
        int borderTop  = 16;
        int scrollValue = 0;
        uint16_t vramBase = 0xC000;
        int pixelsPerByte = 8;
    };

    struct VramWriteInfo
    {
        uint8_t  value    = 0;
        uint16_t pc       = 0;
        uint64_t sequence = 0;
    };

    struct VramWriteSnapshot
    {
        std::vector<VramWriteInfo> lastWrite;
    };

    // -- State queries ------------------------------------------------------

    virtual CpuState    getCpuState()    const = 0;
    virtual DebuggerState getState()     const = 0;
    virtual bool        isPaused()       const = 0;
    virtual StopReason  getStopReason()  const = 0;

    // -- Execution control (from GUI thread) --------------------------------

    virtual void requestRun()    = 0;
    virtual void requestPause()  = 0;
    virtual void requestStep()   = 0;
    virtual void requestReset()  = 0;
    virtual void requestQuit()   = 0;
    virtual void stepInstruction() = 0;

    // -- Memory access ------------------------------------------------------

    virtual uint8_t        readMemory(uint16_t address) = 0;
    virtual MemorySnapshot readMemorySnapshot(uint16_t start, size_t size) = 0;
    virtual bool           writeMemoryByte(uint16_t address, uint8_t value) = 0;
    virtual bool           writeMemory(uint16_t address, const uint8_t* data, size_t size) = 0;

    // -- Register write -----------------------------------------------------

    virtual bool writeRegister(RegisterId id, uint16_t value) = 0;

    // -- Breakpoints --------------------------------------------------------

    virtual int  addBreakpoint(uint16_t address) = 0;
    virtual bool removeBreakpoint(uint16_t address) = 0;
    virtual bool setBreakpointEnabled(uint16_t address, bool enabled) = 0;
    virtual bool hasBreakpoint(uint16_t address) const = 0;
    virtual std::vector<DebuggerBreakpoint> getBreakpoints() const = 0;
    virtual void clearBreakpoints() = 0;

    // -- History (thread-safe snapshots) ------------------------------------

    virtual uint64_t instructionSequence() const = 0;
    virtual size_t   instructionHistorySize() const = 0;
    virtual std::vector<InstructionEvent> instructionHistorySnapshot() const = 0;

    virtual size_t   memoryHistorySize() const = 0;
    virtual std::vector<MemoryAccessEvent> memoryHistorySnapshot() const = 0;

    virtual size_t   ioHistorySize() const = 0;
    virtual std::vector<IoAccessEvent> ioHistorySnapshot() const = 0;

    virtual void clearHistory() = 0;
    virtual void clearIoHistory() = 0;

    // -- Screen -------------------------------------------------------------

    virtual ScreenSnapshot     screenSnapshot() const = 0;
    virtual VideoModeSnapshot  videoModeSnapshot() const = 0;
    virtual VramWriteSnapshot  vramWriteSnapshot() const = 0;

    // -- Palette ------------------------------------------------------------

    virtual PaletteSnapshot paletteSnapshot() const = 0;

    // -- Sound --------------------------------------------------------------

    virtual SoundSnapshot soundSnapshot() const = 0;
    virtual void setMuted(bool muted) = 0;

    // -- Activity -----------------------------------------------------------

    virtual ActivitySnapshot activitySnapshot() const = 0;
    virtual void clearActivityCounters() = 0;

    // -- Live Activity (Stage 5.2 — Memory Map) -----------------------------

    struct LiveBlockState
    {
        std::chrono::steady_clock::time_point lastReadTime;
        std::chrono::steady_clock::time_point lastWriteTime;
    };

    struct LiveActivitySnapshot
    {
        LiveBlockState blocks[256];  // 256 blocks of 256 bytes each
    };

    virtual LiveActivitySnapshot liveActivitySnapshot() const = 0;

    // -- Symbols (read-only access for analysis) ----------------------------

    virtual SymbolDatabase       &symbolDatabase() = 0;
    virtual const SymbolDatabase &symbolDatabase() const = 0;

    // -- Symbol commands (Stage 5.3.1 — through command protocol) -----------

    virtual CommandResult requestCreateFunction(uint16_t addr, const std::string &name) = 0;
    virtual CommandResult requestRenameSymbol(uint16_t addr, const std::string &name) = 0;
    virtual CommandResult requestSetComment(uint16_t addr, const std::string &comment) = 0;
    virtual CommandResult requestRemoveSymbol(uint16_t addr) = 0;
    virtual CommandResult requestAddLabel(uint16_t addr, const std::string &name) = 0;

    // -- Breakpoint commands (Stage 5.3.1 — through command protocol) -------

    virtual CommandResult requestAddBreakpoint(uint16_t addr) = 0;
    virtual CommandResult requestRemoveBreakpoint(uint16_t addr) = 0;
    virtual CommandResult requestSetBreakpointEnabled(uint16_t addr, bool enabled) = 0;

    // -- Trace execution (Stage 5.3.2 — through Command Queue) --------------

    struct TraceExecutionParams
    {
        uint16_t startPc = 0;
        uint32_t maxInstructions = 10000;
        bool stopOnRet = true;
        bool stopOnCallerReturn = true;
        uint16_t callerReturnAddress = 0;
    };

    struct TraceExecutionResult
    {
        uint64_t startSequence = 0;
        uint64_t endSequence = 0;
        uint32_t instructionsExecuted = 0;
        uint16_t exitPc = 0;
        uint16_t entrySp = 0;
        uint16_t exitSp = 0;
        uint16_t minSp = 0;
        uint16_t maxSp = 0;
        ExitReason exitReason = ExitReason::Unknown;
    };

    // Asynchronous trace execution — goes through Command Queue.
    // Returns a future that resolves when the Emulation Thread completes the trace.
    virtual std::future<TraceExecutionResult>
        requestExecuteTrace(const TraceExecutionParams &params) = 0;

    // -- ROM ----------------------------------------------------------------

    virtual bool loadRom(const std::string &path, uint32_t org = 0) = 0;

    // -- Keyboard injection -------------------------------------------------

    virtual void pressKey(int scancode) = 0;
    virtual void releaseKey(int scancode) = 0;
};
