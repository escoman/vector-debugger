#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <condition_variable>

// Forward declaration — avoid pulling full memory.h into debugger headers
class Memory;
#ifndef DEBUGGER_NO_BOARD
class Board;
#endif

// ---------------------------------------------------------------------------
// Memory snapshot for Inspector (Stage 3.3)
// ---------------------------------------------------------------------------

struct MemorySnapshot
{
    uint16_t start;
    std::vector<uint8_t> data;
};

// ---------------------------------------------------------------------------
// CPU state snapshot
// ---------------------------------------------------------------------------

struct CpuState
{
    uint16_t pc;
    uint16_t sp;

    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;

    uint8_t flags;   // S Z AC P CY packed as in F register

    bool iff;        // interrupt flip-flop

    // Stage 3.6 — additional CPU state
    uint32_t cycles;       // cycles of last instruction
    bool     ei_pending;   // EI pending flag
    uint16_t last_pc;      // PC before last step
};

// ---------------------------------------------------------------------------
// Result of a single-instruction step (kept for backward compatibility)
// ---------------------------------------------------------------------------

struct StepResult
{
    uint16_t pcBefore;
    uint16_t pcAfter;

    uint8_t opcode;
    uint8_t length;      // instruction length in bytes (1, 2 or 3)
    uint32_t cycles;     // CPU cycles consumed

    CpuState before;
    CpuState after;
};

// ---------------------------------------------------------------------------
// Debugger state machine
// ---------------------------------------------------------------------------

enum class DebuggerState
{
    Running,
    Paused,
    Stopped
};

// ---------------------------------------------------------------------------
// DebugBackend
//
// Minimal debugger backend that works directly with the real CPU core
// and memory of vector06sdl.  No GUI dependency.
//
// Stage 2 additions:
//   - InstructionEvent / MemoryAccessEvent / IoAccessEvent recording
//   - Ring-buffer history for all three event types
//   - Per-address cumulative memory statistics
//   - 8080 disassembler API
// ---------------------------------------------------------------------------

class DebugBackend
{
public:
    explicit DebugBackend(Memory &memory);
    ~DebugBackend();

    // -- Board integration (Stage 3.2) --------------------------------------

#ifndef DEBUGGER_NO_BOARD
    // Attach real Board for full emulator integration.
    // When Board is attached, stepInstruction() uses Board::single_step()
    // and runUntilPause() uses Board::execute_frame().
    void attachBoard(Board *board);
    Board *getBoard() const { return board_; }

    // Load ROM file into memory and reset Board.
    bool loadRom(const std::string &path, uint32_t org = 0);
#endif

    // -- memory access (Stage 3.3, for Memory Inspector) -------------------

    // Read single byte through DebugMemoryAccess (respects banking)
    uint8_t readMemory(uint16_t address);

    // Read memory range as snapshot
    MemorySnapshot readMemorySnapshot(uint16_t start, size_t size);

    // -- memory write (Stage 3.5, through emulation thread) ----------------

    // Write single byte. Posts command to emulation thread, blocks until done.
    // Returns false if not Paused or address invalid.
    bool writeMemoryByte(uint16_t address, uint8_t value);

    // Write range of bytes. Posts command to emulation thread, blocks until done.
    // Returns false if not Paused or any address invalid.
    bool writeMemory(uint16_t address, const uint8_t* data, size_t size);

    // -- register write (Stage 3.6, through emulation thread) --------------

    // Register identifiers for writeRegister()
    enum class RegisterId { AF, BC, DE, HL, SP, PC };

    // Write a CPU register. Posts command to emulation thread, blocks until done.
    // Returns false if not Paused or invalid register.
    bool writeRegister(RegisterId id, uint16_t value);

    // -- state queries ------------------------------------------------------

    CpuState       getCpuState() const;
    DebuggerState  getState()    const;
    bool           isPaused()    const;

    // -- execution control --------------------------------------------------

    void run();       // run until breakpoint or pause()
    void pause();     // request pause (next instruction boundary)
    void reset();     // soft-reset CPU + clear all debug state

    // Execute exactly one 8080 instruction and return detailed result.
    StepResult stepInstruction();

    // -- breakpoints --------------------------------------------------------

    int  addBreakpoint(uint16_t address);
    void removeBreakpoint(int id);
    void clearBreakpoints();

    // -- debug logging (Stage 1, kept for backward compatibility) -----------

    std::function<void(const char *line)> onTrace;

    // -- event callbacks (Stage 2) ------------------------------------------

    // Called after every executed instruction with the full event.
    std::function<void(const struct InstructionEvent &)> onInstruction;

    // -- instrumentation hooks (Stage 2) ------------------------------------

    // These are installed as Memory::onread / onwrite in the constructor.
    // They can also be called manually from a test HAL.
    void onMemoryRead(uint32_t virt, uint32_t phys, bool stack, uint8_t value);
    void onMemoryWrite(uint32_t virt, uint32_t phys, bool stack, uint8_t value);

    // I/O hooks — call from IO::onread / onwrite adapters.
    void onIoInput(uint8_t port, uint8_t value);
    void onIoOutput(uint8_t port, uint8_t value);

    // -- Instrumentation enable/disable (Stage 2.1) --------------------------

    void setInstrumentationEnabled(bool enabled);
    bool isInstrumentationEnabled() const;

    // -- history access (Stage 2.1, thread-safe snapshots) --------------------

    uint64_t instructionSequence() const;

    size_t   instructionHistorySize() const;
    std::vector<struct InstructionEvent> instructionHistorySnapshot() const;

    size_t   memoryHistorySize() const;
    std::vector<struct MemoryAccessEvent> memoryHistorySnapshot() const;

    size_t   ioHistorySize() const;
    std::vector<struct IoAccessEvent> ioHistorySnapshot() const;

    void clearHistory();

    // -- memory statistics (Stage 2.1, thread-safe snapshot) ------------------

    std::vector<struct MemoryStats> memoryStatsSnapshot() const;

    // -- thread-safe command API (Stage 3.1, for GUI) -------------------------

    // Called from GUI thread to request execution actions.
    // The emulation thread calls runUntilPause() to process them.
    void requestStep();       // execute exactly 1 instruction (blocks until done)
    void requestRun();        // start continuous execution
    void requestPause();      // request pause at next instruction boundary
    void requestReset();      // request CPU reset
    void waitForCompletion(); // block until current step/reset completes

    // Called from emulation thread — runs until paused or breakpoint.
    void runUntilPause();

    // Process one pending command (called from emulation thread loop).
    // Also callable from tests to synchronously process posted commands.
    void processOneCommand();

    // Signal the emulation thread to quit.
    void requestQuit();
    bool isQuitRequested() const;

private:
    Memory &memory_;
#ifndef DEBUGGER_NO_BOARD
    Board *board_ = nullptr;  // optional, for real emulator mode
#endif

    DebuggerState state_;

    // Breakpoints: id → address
    std::map<int, uint16_t> breakpoints_;
    int nextId_;

    bool pauseRequested_;

    bool checkBreakpoint();

    void formatTraceLine(char *buf, size_t bufsize,
                         uint16_t pc, uint8_t opcode,
                         const uint8_t *operandBytes) const;

    // -- Stage 2 internals --------------------------------------------------

    uint64_t instructionSequence_;
    bool     instrumentationEnabled_;
    int      fetchRemaining_;   // fetch window: counts down during instruction execution

    // Ring buffers (heap-allocated to avoid large stack frames)
    struct Impl;
    Impl *impl_;

    // Saved previous Memory callbacks (for chaining)
    std::function<void(uint32_t,uint32_t,bool,uint8_t)> prevOnRead_;
    std::function<void(uint32_t,uint32_t,bool,uint8_t)> prevOnWrite_;

    void installMemoryCallbacks();

    // -- Stage 3.1: thread-safe command protocol ------------------------------

    enum class PendingCommand { None, Step, Run, Pause, Reset, Quit, MemoryWrite, RegisterWrite };

    mutable std::mutex      commandMutex_;
    std::condition_variable commandCv_;
    std::condition_variable resultCv_;
    PendingCommand          pendingCommand_ = PendingCommand::None;
    bool                    stepCompleted_  = false;
    bool                    quitRequested_  = false;
    mutable std::mutex      stateMutex_;  // protects state_ for cross-thread reads

    // -- Stage 3.5: memory write command state --------------------------------

    uint16_t                writeAddress_   = 0;
    std::vector<uint8_t>    writeData_;
    bool                    writeResult_    = false;

    void processWriteCommand();
    void executeWriteMemory();

    // -- Stage 3.6: register write command state ------------------------------

    RegisterId  writeRegId_    = RegisterId::AF;
    uint16_t    writeRegValue_ = 0;
    bool        writeRegResult_ = false;
    uint16_t    lastPc_        = 0;  // PC before last stepInstruction

    void processRegisterWrite();
    void executeRegisterWrite();
};
