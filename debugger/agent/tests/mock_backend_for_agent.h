#pragma once

// ---------------------------------------------------------------------------
// MockAgentBackend — Stage 5.3.1
//
// Minimal IDebugBackend implementation for Agent API unit tests.
// No Board, SDL, or emulator dependency.
// Provides controllable test data for all Agent API operations.
// ---------------------------------------------------------------------------

#include "idebug_backend.h"
#include "events.h"
#include "symbol_database.h"
#include "opcode_info.h"

#include <cstdint>
#include <cstring>
#include <future>
#include <map>
#include <vector>

class MockAgentBackend : public IDebugBackend
{
public:
    MockAgentBackend() {
        // Default: paused state
        cpu_.pc = 0x0100;
        cpu_.sp = 0xF800;
        cpu_.a = 0x42;
        cpu_.flags = 0x00;

        // Initialize memory to zero
        memory_.resize(65536, 0x00);

        // Place a simple program at 0x0100:
        // 0x0100: LXI SP, 0xF800   (31 00 F8)
        // 0x0103: MVI A, 0x55      (3E 55)
        // 0x0105: CALL 0x0200      (CD 00 02)
        // 0x0108: HLT              (76)
        memory_[0x0100] = 0x31; memory_[0x0101] = 0x00; memory_[0x0102] = 0xF8;
        memory_[0x0103] = 0x3E; memory_[0x0104] = 0x55;
        memory_[0x0105] = 0xCD; memory_[0x0106] = 0x00; memory_[0x0107] = 0x02;
        memory_[0x0108] = 0x76;

        // Place a subroutine at 0x0200:
        // 0x0200: PUSH H           (E5)
        // 0x0202: MVI H, 0xC0      (26 C0)
        // 0x0204: MVI L, 0x00      (2E 00)
        // 0x0206: MOV M, A         (77)
        // 0x0207: POP H            (E1)
        // 0x0208: RET              (C9)
        memory_[0x0200] = 0xE5;
        memory_[0x0202] = 0x26; memory_[0x0203] = 0xC0;
        memory_[0x0204] = 0x2E; memory_[0x0205] = 0x00;
        memory_[0x0206] = 0x77;
        memory_[0x0207] = 0xE1;
        memory_[0x0208] = 0xC9;

        // Pre-populate some activity counters
        executeCount_.resize(65536, 0);
        readCount_.resize(65536, 0);
        writeCount_.resize(65536, 0);

        executeCount_[0x0100] = 1;
        executeCount_[0x0103] = 1;
        executeCount_[0x0105] = 1;
        executeCount_[0x0200] = 1;
        executeCount_[0x0202] = 1;
        executeCount_[0x0204] = 1;
        executeCount_[0x0206] = 1;
        executeCount_[0x0207] = 1;
        executeCount_[0x0208] = 1;

        writeCount_[0xC000] = 1;  // VRAM write
    }

    // -- State queries ------------------------------------------------------

    CpuState getCpuState() const override { return cpu_; }
    DebuggerState getState() const override { return state_; }
    bool isPaused() const override { return state_ == DebuggerState::Paused; }
    StopReason getStopReason() const override { return stopReason_; }

    // -- Execution control --------------------------------------------------

    void requestRun() override {
        // Simulate running until breakpoint or HLT (synchronous for tests)
        state_ = DebuggerState::Running;
        while (state_ == DebuggerState::Running) {
            uint16_t pc = cpu_.pc;

            // Check breakpoint
            if (hasBreakpoint(pc)) {
                state_ = DebuggerState::Paused;
                stopReason_ = StopReason::Breakpoint;
                return;
            }

            // Execute one instruction (simplified 8080)
            simulateStep();

            // Check HLT
            if (halted_) {
                state_ = DebuggerState::Paused;
                stopReason_ = StopReason::Step;
                return;
            }

            // Safety limit
            if (++runStepCount_ > 100000) {
                state_ = DebuggerState::Paused;
                stopReason_ = StopReason::UserPause;
                return;
            }
        }
    }

    void requestPause() override { state_ = DebuggerState::Paused; }
    void requestStep() override { simulateStep(); }
    void requestReset() override {
        cpu_.pc = 0x0100;
        cpu_.sp = 0xF800;
        state_ = DebuggerState::Paused;
        halted_ = false;
        runStepCount_ = 0;
    }
    void requestQuit() override { state_ = DebuggerState::Stopped; }

    void stepInstruction() override {
        simulateStep();
    }

    // -- Memory access ------------------------------------------------------

    uint8_t readMemory(uint16_t address) override {
        return memory_[address];
    }

    MemorySnapshot readMemorySnapshot(uint16_t start, size_t size) override {
        MemorySnapshot snap;
        snap.start = start;
        snap.data.resize(size);
        for (size_t i = 0; i < size; ++i) {
            snap.data[i] = memory_[static_cast<uint16_t>(start + i)];
        }
        return snap;
    }

    bool writeMemoryByte(uint16_t address, uint8_t value) override {
        memory_[address] = value;
        return true;
    }

    bool writeMemory(uint16_t address, const uint8_t* data, size_t size) override {
        for (size_t i = 0; i < size; ++i) {
            memory_[static_cast<uint16_t>(address + i)] = data[i];
        }
        return true;
    }

    // -- Register write -----------------------------------------------------

    bool writeRegister(RegisterId id, uint16_t value) override {
        switch (id) {
            case RegisterId::AF: cpu_.a = value >> 8; cpu_.flags = value & 0xFF; break;
            case RegisterId::BC: cpu_.b = value >> 8; cpu_.c = value & 0xFF; break;
            case RegisterId::DE: cpu_.d = value >> 8; cpu_.e = value & 0xFF; break;
            case RegisterId::HL: cpu_.h = value >> 8; cpu_.l = value & 0xFF; break;
            case RegisterId::SP: cpu_.sp = value; break;
            case RegisterId::PC: cpu_.pc = value; break;
        }
        return true;
    }

    // -- Breakpoints (direct — for backward compat) -------------------------

    int addBreakpoint(uint16_t address) override {
        for (auto &kv : breakpoints_) {
            if (kv.second.address == address) return -1;
        }
        int id = nextBpId_++;
        breakpoints_[id] = {address, true};
        return id;
    }

    bool removeBreakpoint(uint16_t address) override {
        for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
            if (it->second.address == address) {
                breakpoints_.erase(it);
                return true;
            }
        }
        return false;
    }

    bool setBreakpointEnabled(uint16_t address, bool enabled) override {
        for (auto &kv : breakpoints_) {
            if (kv.second.address == address) {
                kv.second.enabled = enabled;
                return true;
            }
        }
        return false;
    }

    bool hasBreakpoint(uint16_t address) const override {
        for (auto &kv : breakpoints_) {
            if (kv.second.address == address && kv.second.enabled) return true;
        }
        return false;
    }

    std::vector<DebuggerBreakpoint> getBreakpoints() const override {
        std::vector<DebuggerBreakpoint> result;
        for (auto &kv : breakpoints_) result.push_back(kv.second);
        return result;
    }

    void clearBreakpoints() override { breakpoints_.clear(); }

    // -- Breakpoint commands (Stage 5.3.1 — direct in mock) -----------------

    CommandResult requestAddBreakpoint(uint16_t addr) override {
        int id = addBreakpoint(addr);
        CommandResult r;
        r.success = (id >= 0);
        if (!r.success) { r.error = "duplicate"; r.status = CommandResult::Failed; }
        return r;
    }

    CommandResult requestRemoveBreakpoint(uint16_t addr) override {
        bool ok = removeBreakpoint(addr);
        CommandResult r;
        r.success = ok;
        if (!ok) { r.error = "not found"; r.status = CommandResult::Failed; }
        return r;
    }

    CommandResult requestSetBreakpointEnabled(uint16_t addr, bool enabled) override {
        bool ok = setBreakpointEnabled(addr, enabled);
        CommandResult r;
        r.success = ok;
        if (!ok) { r.error = "not found"; r.status = CommandResult::Failed; }
        return r;
    }

    // -- History ------------------------------------------------------------

    uint64_t instructionSequence() const override { return seq_; }
    size_t instructionHistorySize() const override { return instrHistory_.size(); }
    std::vector<InstructionEvent> instructionHistorySnapshot() const override {
        return instrHistory_;
    }

    size_t memoryHistorySize() const override { return memHistory_.size(); }
    std::vector<MemoryAccessEvent> memoryHistorySnapshot() const override {
        return memHistory_;
    }

    size_t ioHistorySize() const override { return ioHistory_.size(); }
    std::vector<IoAccessEvent> ioHistorySnapshot() const override {
        return ioHistory_;
    }

    void clearHistory() override {
        instrHistory_.clear();
        memHistory_.clear();
    }
    void clearIoHistory() override { ioHistory_.clear(); }

    // -- Screen -------------------------------------------------------------

    ScreenSnapshot screenSnapshot() const override { return screenSnap_; }
    VideoModeSnapshot videoModeSnapshot() const override { return videoSnap_; }
    VramWriteSnapshot vramWriteSnapshot() const override { return {}; }

    // -- Palette ------------------------------------------------------------

    PaletteSnapshot paletteSnapshot() const override { return {}; }

    // -- Sound --------------------------------------------------------------

    SoundSnapshot soundSnapshot() const override { return {}; }
    void setMuted(bool) override {}

    // -- Activity -----------------------------------------------------------

    ActivitySnapshot activitySnapshot() const override {
        return {executeCount_, readCount_, writeCount_};
    }

    void clearActivityCounters() override {
        std::fill(executeCount_.begin(), executeCount_.end(), 0);
        std::fill(readCount_.begin(), readCount_.end(), 0);
        std::fill(writeCount_.begin(), writeCount_.end(), 0);
    }

    // -- Live Activity ------------------------------------------------------
    LiveActivitySnapshot liveActivitySnapshot() const override { return {}; }

    // -- Symbols (read-only) ------------------------------------------------
    // -- Symbols (read-only) ------------------------------------------------
    SymbolDatabase &symbolDatabase() override { return symbols_; }
    const SymbolDatabase &symbolDatabase() const override { return symbols_; }

    // -- Symbol commands (Stage 5.3.1 — direct in mock) ---------------------

    CommandResult requestCreateFunction(uint16_t addr, const std::string &name) override {
        bool ok = symbols_.addSymbol(addr, name, SymbolType::Function);
        CommandResult r; r.success = ok;
        if (!ok) { r.error = "exists"; r.status = CommandResult::Failed; }
        return r;
    }

    CommandResult requestRenameSymbol(uint16_t addr, const std::string &name) override {
        bool ok = symbols_.renameSymbol(addr, name);
        CommandResult r; r.success = ok;
        if (!ok) { r.error = "not found"; r.status = CommandResult::Failed; }
        return r;
    }

    CommandResult requestSetComment(uint16_t addr, const std::string &comment) override {
        bool ok = symbols_.setComment(addr, comment);
        CommandResult r; r.success = ok;
        if (!ok) { r.error = "not found"; r.status = CommandResult::Failed; }
        return r;
    }

    CommandResult requestRemoveSymbol(uint16_t addr) override {
        bool ok = symbols_.removeSymbol(addr);
        CommandResult r; r.success = ok;
        if (!ok) { r.error = "not found"; r.status = CommandResult::Failed; }
        return r;
    }

    CommandResult requestAddLabel(uint16_t addr, const std::string &name) override {
        bool ok = symbols_.addSymbol(addr, name, SymbolType::Label);
        CommandResult r; r.success = ok;
        if (!ok) { r.error = "exists"; r.status = CommandResult::Failed; }
        return r;
    }

    // -- Trace execution (Stage 5.3.2 — through queue) -----------------------

    std::future<TraceExecutionResult>
    requestExecuteTrace(const TraceExecutionParams &params) override {
        // Mock: execute synchronously, deliver via promise/future
        auto promise = std::make_shared<std::promise<TraceExecutionResult>>();
        auto future = promise->get_future();

        TraceExecutionResult result;
        result.startSequence = seq_;
        result.entrySp = cpu_.sp;
        result.minSp = cpu_.sp;
        result.maxSp = cpu_.sp;
        result.exitReason = ExitReason::Unknown;

        for (uint32_t i = 0; i < params.maxInstructions; ++i) {
            uint16_t pc = cpu_.pc;
            uint8_t opcode = memory_[pc];

            simulateStep();

            // Track SP bounds
            if (cpu_.sp < result.minSp) result.minSp = cpu_.sp;
            if (cpu_.sp > result.maxSp) result.maxSp = cpu_.sp;

            // Check RET
            if (params.stopOnRet && opcode == 0xC9) {
                result.exitReason = ExitReason::Ret;
                result.exitPc = pc;
                break;
            }

            // Check HLT
            if (opcode == 0x76) {
                result.exitReason = ExitReason::Halt;
                result.exitPc = pc;
                break;
            }

            // Check caller return
            if (params.stopOnCallerReturn && params.callerReturnAddress != 0
                && cpu_.pc == params.callerReturnAddress) {
                result.exitReason = ExitReason::CallerReturn;
                result.exitPc = pc;
                break;
            }
        }

        if (result.exitReason == ExitReason::Unknown) {
            result.exitReason = ExitReason::Timeout;
            result.exitPc = cpu_.pc;
        }

        result.endSequence = seq_;
        result.instructionsExecuted = static_cast<uint32_t>(result.endSequence - result.startSequence);
        result.exitSp = cpu_.sp;

        promise->set_value(result);
        return future;
    }

    // -- ROM ----------------------------------------------------------------
    bool loadRom(const std::string &, uint32_t) override { return true; }

    // -- Keyboard -----------------------------------------------------------
    void pressKey(int) override {}
    void releaseKey(int) override {}

    // -- Test data setters --------------------------------------------------

    void setCpuState(const CpuState &cpu) { cpu_ = cpu; }
    void setState(DebuggerState s) { state_ = s; }

    void setMemory(uint16_t addr, const std::vector<uint8_t> &data) {
        for (size_t i = 0; i < data.size(); ++i) {
            memory_[static_cast<uint16_t>(addr + i)] = data[i];
        }
    }

    void addInstructionEvent(const InstructionEvent &ev) {
        instrHistory_.push_back(ev);
    }

    void addMemoryEvent(const MemoryAccessEvent &ev) {
        memHistory_.push_back(ev);
    }

    void addIoEvent(const IoAccessEvent &ev) {
        ioHistory_.push_back(ev);
    }

private:
    CpuState cpu_{};
    DebuggerState state_ = DebuggerState::Paused;
    StopReason stopReason_ = StopReason::UserPause;

    std::vector<uint8_t> memory_;

    std::map<int, DebuggerBreakpoint> breakpoints_;
    int nextBpId_ = 1;

    uint64_t seq_ = 100;
    std::vector<InstructionEvent> instrHistory_;
    std::vector<MemoryAccessEvent> memHistory_;
    std::vector<IoAccessEvent> ioHistory_;

    ScreenSnapshot screenSnap_;
    VideoModeSnapshot videoSnap_;

    std::vector<uint64_t> executeCount_;
    std::vector<uint64_t> readCount_;
    std::vector<uint64_t> writeCount_;

    SymbolDatabase symbols_;

    bool halted_ = false;
    uint32_t runStepCount_ = 0;

    // Simplified 8080 step for mock — handles key instructions
    void simulateStep() {
        uint16_t pc = cpu_.pc;
        uint8_t opcode = memory_[pc];
        uint8_t len = opcode_info::get_length(opcode);

        // Record instruction event
        InstructionEvent ie;
        ie.sequence = seq_;
        ie.pcBefore = pc;
        ie.opcode = opcode;
        ie.length = len;
        ie.before = cpu_;
        if (len >= 2) ie.operandBytes[0] = memory_[pc + 1];
        if (len >= 3) ie.operandBytes[1] = memory_[pc + 2];

        // Simulate key instructions
        switch (opcode) {
        case 0xC9: // RET
            // Pop PC from stack
            cpu_.pc = memory_[cpu_.sp] | (memory_[cpu_.sp + 1] << 8);
            cpu_.sp += 2;
            break;

        case 0xCD: // CALL addr16
        {
            uint16_t target = memory_[pc + 1] | (memory_[pc + 2] << 8);
            cpu_.sp -= 2;
            memory_[cpu_.sp] = (pc + 3) & 0xFF;
            memory_[cpu_.sp + 1] = ((pc + 3) >> 8) & 0xFF;
            cpu_.pc = target;
            break;
        }

        case 0xE5: // PUSH HL
            cpu_.sp -= 2;
            memory_[cpu_.sp] = cpu_.l;
            memory_[cpu_.sp + 1] = cpu_.h;
            cpu_.pc = pc + len;
            break;

        case 0xE1: // POP HL
            cpu_.l = memory_[cpu_.sp];
            cpu_.h = memory_[cpu_.sp + 1];
            cpu_.sp += 2;
            cpu_.pc = pc + len;
            break;

        case 0x76: // HLT
            halted_ = true;
            cpu_.pc = pc;  // PC stays at HLT
            break;

        case 0x77: // MOV M, A — write A to (HL)
        {
            uint16_t addr = (static_cast<uint16_t>(cpu_.h) << 8) | cpu_.l;
            memory_[addr] = cpu_.a;
            // Record memory write event
            MemoryAccessEvent mev;
            mev.instructionSequence = seq_;
            mev.type = MemoryAccessType::Write;
            mev.virt = addr;
            mev.value = cpu_.a;
            mev.stack = false;
            memHistory_.push_back(mev);
            cpu_.pc = pc + len;
            break;
        }

        case 0xC3: // JMP addr16
        {
            uint16_t target = memory_[pc + 1] | (memory_[pc + 2] << 8);
            cpu_.pc = target;
            break;
        }

        default:
            // Default: advance PC by instruction length
            cpu_.pc = pc + len;
            break;
        }

        ie.pcAfter = cpu_.pc;
        ie.after = cpu_;
        ie.cycles = 4;

        instrHistory_.push_back(ie);
        seq_++;
    }
};
