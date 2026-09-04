#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

#include "idebug_backend.h"
#include "debug_target.h"

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
// DebugBackend
//
// Implements IDebugBackend (GUI facade) and works through IDebugTarget
// (emulator adapter). No direct access to Board, Memory, CPU, or IO.
// ---------------------------------------------------------------------------

class DebugBackend : public IDebugBackend
{
public:
    explicit DebugBackend(IDebugTarget &target);

    ~DebugBackend();

    // -- Target integration (Stage 3.13a) -----------------------------------

    void attachTarget(IDebugTarget *target);
    IDebugTarget *getTarget() const { return target_; }

    // -- IDebugBackend: ROM -------------------------------------------------

    bool loadRom(const std::string &path, uint32_t org = 0) override;

    // -- IDebugBackend: memory access ---------------------------------------

    uint8_t readMemory(uint16_t address) override;
    MemorySnapshot readMemorySnapshot(uint16_t start, size_t size) override;
    bool writeMemoryByte(uint16_t address, uint8_t value) override;
    bool writeMemory(uint16_t address, const uint8_t* data, size_t size) override;

    // -- IDebugBackend: register write --------------------------------------

    bool writeRegister(RegisterId id, uint16_t value) override;

    // -- IDebugBackend: state queries ---------------------------------------

    CpuState       getCpuState()    const override;
    DebuggerState  getState()       const override;
    bool           isPaused()       const override;
    StopReason     getStopReason()  const override;

    // -- IDebugBackend: execution control -----------------------------------

    void requestRun()      override;
    void requestPause()    override;
    void requestStep()     override;
    void requestReset()    override;
    void requestQuit()     override;
    void stepInstruction() override;  // IDebugBackend: execute 1 instruction (void)

    // Extended step that returns detailed result (for tests/tracing)
    StepResult stepInstructionDetailed();

    // -- IDebugBackend: breakpoints -----------------------------------------

    int  addBreakpoint(uint16_t address) override;
    bool removeBreakpoint(uint16_t address) override;
    bool setBreakpointEnabled(uint16_t address, bool enabled) override;
    bool hasBreakpoint(uint16_t address) const override;
    std::vector<DebuggerBreakpoint> getBreakpoints() const override;
    void clearBreakpoints() override;

    // Legacy: remove by id (backward compat with tests)
    void removeBreakpoint(int id);

    // -- IDebugBackend: history ---------------------------------------------

    uint64_t instructionSequence() const override;
    size_t   instructionHistorySize() const override;
    std::vector<InstructionEvent> instructionHistorySnapshot() const override;

    size_t   memoryHistorySize() const;
    std::vector<struct MemoryAccessEvent> memoryHistorySnapshot() const;

    size_t   ioHistorySize() const override;
    std::vector<IoAccessEvent> ioHistorySnapshot() const override;

    void clearHistory() override;
    void clearIoHistory() override;

    // -- IDebugBackend: screen ----------------------------------------------

    ScreenSnapshot     screenSnapshot() const override;
    VideoModeSnapshot  videoModeSnapshot() const override;
    VramWriteSnapshot  vramWriteSnapshot() const override;

    // -- IDebugBackend: activity --------------------------------------------

    ActivitySnapshot activitySnapshot() const override;
    void clearActivityCounters() override;

    // -- IDebugBackend: live activity (Stage 5.2) ----------------------------

    LiveActivitySnapshot liveActivitySnapshot() const override;

    // -- IDebugBackend: symbols ---------------------------------------------

    SymbolDatabase       &symbolDatabase() override;
    const SymbolDatabase &symbolDatabase() const override;

    // -- debug logging (backward compatibility) -----------------------------

    std::function<void(const char *line)> onTrace;

    // -- event callbacks ----------------------------------------------------

    std::function<void(const InstructionEvent &)> onInstruction;

    // -- instrumentation hooks ----------------------------------------------

    void onMemoryRead(uint32_t virt, uint32_t phys, bool stack, uint8_t value);
    void onMemoryWrite(uint32_t virt, uint32_t phys, bool stack, uint8_t value);
    void onIoInput(uint8_t port, uint8_t value);
    void onIoOutput(uint8_t port, uint8_t value);

    void setInstrumentationEnabled(bool enabled);
    bool isInstrumentationEnabled() const;

    // -- memory statistics --------------------------------------------------

    std::vector<struct MemoryStats> memoryStatsSnapshot() const;

    // -- emulation thread API (not part of IDebugBackend) --------------------

    void runUntilPause();
    void processOneCommand();
    void waitForCompletion();
    bool isQuitRequested() const;

    // Legacy execution (used internally, not via IDebugBackend)
    void run();
    void pause();
    void reset();

private:
    IDebugTarget *target_;

    DebuggerState state_;

    std::map<int, DebuggerBreakpoint> breakpoints_;
    int nextId_;

    bool pauseRequested_;

    StopReason stopReason_ = StopReason::None;
    bool       skipBreakpoint_ = false;

    bool checkBreakpoint();
    void syncBreakpointsToTarget();

    void formatTraceLine(char *buf, size_t bufsize,
                         uint16_t pc, uint8_t opcode,
                         const uint8_t *operandBytes) const;

    // -- Stage 2 internals --------------------------------------------------

    uint64_t instructionSequence_;
    bool     instrumentationEnabled_;
    int      fetchRemaining_;

    struct Impl;
    Impl *impl_;

    void installMemoryCallbacks();

    // -- Execution loops ----------------------------------------------------

    void executeFramesTarget_();
    void executeFramesNoTarget_();

    // -- Stage 3.1: thread-safe command protocol ------------------------------

    enum class PendingCommand { None, Step, Run, Pause, Reset, Quit, MemoryWrite, RegisterWrite };

    mutable std::mutex      commandMutex_;
    std::condition_variable commandCv_;
    std::condition_variable resultCv_;
    PendingCommand          pendingCommand_ = PendingCommand::None;
    bool                    stepCompleted_  = false;
    bool                    quitRequested_  = false;
    mutable std::mutex      stateMutex_;

    std::atomic<bool>       running_{false};
    std::atomic<bool>       breakRequested_{false};

    // -- memory write command state -----------------------------------------

    uint16_t                writeAddress_   = 0;
    std::vector<uint8_t>    writeData_;
    bool                    writeResult_    = false;

    void processWriteCommand();
    void executeWriteMemory();

    // -- register write command state ---------------------------------------

    RegisterId  writeRegId_    = RegisterId::AF;
    uint16_t    writeRegValue_ = 0;
    bool        writeRegResult_ = false;
    uint16_t    lastPc_        = 0;

    void processRegisterWrite();
    void executeRegisterWrite();

    std::map<int, DebuggerBreakpoint>::iterator findBreakpointByAddress(uint16_t address);
    std::map<int, DebuggerBreakpoint>::const_iterator findBreakpointByAddress(uint16_t address) const;

    // -- Symbol database ----------------------------------------------------

    SymbolDatabase symbols_;

    // -- Activity counters --------------------------------------------------

    uint64_t executeCount_[65536];

    // -- VRAM write tracking ------------------------------------------------

    VramWriteInfo vramLastWrite_[256];
    mutable std::mutex vramWriteMutex_;

    // -- IO port tracking ---------------------------------------------------

    uint8_t ioPA_ = 0xFF;
    uint8_t ioPB_ = 0xFF;
    mutable std::mutex ioRegMutex_;

    // -- Screen mutex -------------------------------------------------------

    mutable std::mutex screenMutex_;

};
