#pragma once

// ---------------------------------------------------------------------------
// MockAgentBackend — Stage 5.3
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

    void requestRun() override { state_ = DebuggerState::Running; }
    void requestPause() override { state_ = DebuggerState::Paused; }
    void requestStep() override {}
    void requestReset() override {
        cpu_.pc = 0x0100;
        state_ = DebuggerState::Paused;
    }
    void requestQuit() override { state_ = DebuggerState::Stopped; }
    void stepInstruction() override {
        // Simulate: advance PC by instruction length
        uint8_t opcode = memory_[cpu_.pc];
        uint8_t len = opcode_info::get_length(opcode);
        cpu_.pc += len;
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

    // -- Breakpoints --------------------------------------------------------

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

    // -- History ------------------------------------------------------------

    uint64_t instructionSequence() const override { return seq_; }
    size_t instructionHistorySize() const override { return instrHistory_.size(); }
    std::vector<InstructionEvent> instructionHistorySnapshot() const override {
        return instrHistory_;
    }

    size_t ioHistorySize() const override { return ioHistory_.size(); }
    std::vector<IoAccessEvent> ioHistorySnapshot() const override {
        return ioHistory_;
    }

    void clearHistory() override { instrHistory_.clear(); }
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

    // -- Symbols ------------------------------------------------------------
    SymbolDatabase &symbolDatabase() override { return symbols_; }
    const SymbolDatabase &symbolDatabase() const override { return symbols_; }

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
    std::vector<IoAccessEvent> ioHistory_;

    ScreenSnapshot screenSnap_;
    VideoModeSnapshot videoSnap_;

    std::vector<uint64_t> executeCount_;
    std::vector<uint64_t> readCount_;
    std::vector<uint64_t> writeCount_;

    SymbolDatabase symbols_;
};
