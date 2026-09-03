#include "backend.h"
#include "events.h"
#include "opcode_info.h"
#include "ring_buffer.h"
#include "debug_memory.h"

#include "i8080.h"
#include "i8080_hal.h"
#include "memory.h"
#ifndef DEBUGGER_NO_BOARD
#include "board.h"
#include "tv.h"
#include "util.h"
#endif

#include <cstdio>
#include <cstring>
#include <climits>

using namespace i8080cpu;

// ---------------------------------------------------------------------------
// Default ring-buffer capacities
// ---------------------------------------------------------------------------

static const size_t DEFAULT_INSTRUCTION_HISTORY = 10000;
static const size_t DEFAULT_MEMORY_HISTORY      = 50000;
static const size_t DEFAULT_IO_HISTORY          = 10000;

// ---------------------------------------------------------------------------
// Impl — heap-allocated ring buffers + memory statistics
// ---------------------------------------------------------------------------

struct DebugBackend::Impl
{
    RingBuffer<InstructionEvent>  instrHistory;
    RingBuffer<MemoryAccessEvent> memHistory;
    RingBuffer<IoAccessEvent>     ioHistory;
    MemoryStats                   memStats[65536];

    Impl()
        : instrHistory(DEFAULT_INSTRUCTION_HISTORY)
        , memHistory(DEFAULT_MEMORY_HISTORY)
        , ioHistory(DEFAULT_IO_HISTORY)
    {
        clearStats();
    }

    void clearStats()
    {
        for (int i = 0; i < 65536; ++i) {
            memStats[i].reads = 0;
            memStats[i].writes = 0;
            memStats[i].lastReadSequence = UINT64_MAX;
            memStats[i].lastWriteSequence = UINT64_MAX;
        }
    }
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DebugBackend::DebugBackend(Memory &memory)
    : memory_(memory)
    , state_(DebuggerState::Paused)
    , nextId_(1)
    , pauseRequested_(false)
    , instructionSequence_(0)
    , instrumentationEnabled_(true)
    , fetchRemaining_(0)
    , impl_(new Impl())
{
    clearActivityCounters();
    installMemoryCallbacks();
}

DebugBackend::~DebugBackend()
{
    // Restore previous callbacks before destroying
    memory_.onread  = prevOnRead_;
    memory_.onwrite = prevOnWrite_;
    delete impl_;
}

// ---------------------------------------------------------------------------
// Board integration (Stage 3.2)
// ---------------------------------------------------------------------------

#ifndef DEBUGGER_NO_BOARD

void DebugBackend::attachBoard(Board *board)
{
    board_ = board;
}

bool DebugBackend::loadRom(const std::string &path, uint32_t org)
{
    // Load ROM file
    std::vector<uint8_t> rom_data = util::load_binfile(path);
    if (rom_data.empty()) {
        printf("DebugBackend::loadRom(): failed to load %s\n", path.c_str());
        return false;
    }
    
    printf("DebugBackend::loadRom(): loaded %s (%zu bytes) at %04x\n",
           path.c_str(), rom_data.size(), org);
    
    // Load into Memory
    memory_.init_from_vector(rom_data, org);
    
    // Reset Board in LOADROM mode
    if (board_) {
        Options.pc = org;  // Set PC for reset
        board_->reset(Board::ResetMode::LOADROM);
    } else {
        // No Board attached, just reset CPU
        i8080_jump(org);
        i8080_setreg_sp(0xc300);
        i8080_init();
    }
    
    // Clear debug history
    clearHistory();
    instructionSequence_ = 0;
    
    // Set state to Paused
    state_ = DebuggerState::Paused;
    pauseRequested_ = false;
    
    return true;
}

#endif // DEBUGGER_NO_BOARD

// ---------------------------------------------------------------------------
// Memory callback installation (with chaining)
// ---------------------------------------------------------------------------

void DebugBackend::installMemoryCallbacks()
{
    // Save existing callbacks for chaining
    prevOnRead_  = memory_.onread;
    prevOnWrite_ = memory_.onwrite;

    DebugBackend *self = this;

    memory_.onread = [self](uint32_t virt, uint32_t phys,
                            bool stack, uint8_t value) {
        self->onMemoryRead(virt, phys, stack, value);
        if (self->prevOnRead_) {
            self->prevOnRead_(virt, phys, stack, value);
        }
    };

    memory_.onwrite = [self](uint32_t virt, uint32_t phys,
                             bool stack, uint8_t value) {
        self->onMemoryWrite(virt, phys, stack, value);
        if (self->prevOnWrite_) {
            self->prevOnWrite_(virt, phys, stack, value);
        }
    };
}

// ---------------------------------------------------------------------------
// Memory access (Stage 3.3 — for Memory Inspector)
// ---------------------------------------------------------------------------

uint8_t DebugBackend::readMemory(uint16_t address)
{
    return DebugMemoryAccess::peek(memory_, address);
}

MemorySnapshot DebugBackend::readMemorySnapshot(uint16_t start, size_t size)
{
    MemorySnapshot snapshot;
    snapshot.start = start;
    snapshot.data.reserve(size);
    
    for (size_t i = 0; i < size; ++i) {
        uint16_t addr = static_cast<uint16_t>((start + i) & 0xFFFF);
        snapshot.data.push_back(DebugMemoryAccess::peek(memory_, addr));
    }
    
    return snapshot;
}

// ---------------------------------------------------------------------------
// Memory write (Stage 3.5 — through emulation thread)
// ---------------------------------------------------------------------------

bool DebugBackend::writeMemoryByte(uint16_t address, uint8_t value)
{
    return writeMemory(address, &value, 1);
}

bool DebugBackend::writeMemory(uint16_t address, const uint8_t* data, size_t size)
{
    if (!data || size == 0) return false;

    // Post write command for emulation thread
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        writeAddress_ = address;
        writeData_.assign(data, data + size);
        writeResult_  = false;
        pendingCommand_ = PendingCommand::MemoryWrite;
        stepCompleted_  = false;
    }
    commandCv_.notify_one();

    // Wait for the emulation thread to complete the write
    std::unique_lock<std::mutex> lock(commandMutex_);
    resultCv_.wait(lock, [this]{ return stepCompleted_; });

    return writeResult_;
}

void DebugBackend::processWriteCommand()
{
    // Check state — write only allowed when Paused.
    // State check happens in the emulation thread context.
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (state_ != DebuggerState::Paused) {
        writeResult_ = false;
        return;
    }

    executeWriteMemory();
    writeResult_ = true;
}

void DebugBackend::executeWriteMemory()
{
    // Write through Memory::write() — handles virtual address translation
    // and banking automatically. Do NOT use Memory::buffer().
    for (size_t i = 0; i < writeData_.size(); ++i) {
        uint16_t addr = static_cast<uint16_t>((writeAddress_ + i) & 0xFFFF);
        memory_.write(addr, writeData_[i], false);
    }
}

// ---------------------------------------------------------------------------
// Register write (Stage 3.6 — through emulation thread)
// ---------------------------------------------------------------------------

bool DebugBackend::writeRegister(RegisterId id, uint16_t value)
{
    // Post register write command for emulation thread
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        writeRegId_     = id;
        writeRegValue_  = value;
        writeRegResult_ = false;
        pendingCommand_ = PendingCommand::RegisterWrite;
        stepCompleted_  = false;
    }
    commandCv_.notify_one();

    // Wait for the emulation thread to complete the write
    std::unique_lock<std::mutex> lock(commandMutex_);
    resultCv_.wait(lock, [this]{ return stepCompleted_; });

    return writeRegResult_;
}

void DebugBackend::processRegisterWrite()
{
    // Check state — write only allowed when Paused.
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (state_ != DebuggerState::Paused) {
        writeRegResult_ = false;
        return;
    }

    executeRegisterWrite();
    writeRegResult_ = true;
}

void DebugBackend::executeRegisterWrite()
{
    switch (writeRegId_) {
        case RegisterId::AF:
            i8080_setreg_a((writeRegValue_ >> 8) & 0xFF);
            i8080_setreg_f(writeRegValue_ & 0xFF);
            break;
        case RegisterId::BC:
            i8080_setreg_b((writeRegValue_ >> 8) & 0xFF);
            i8080_setreg_c(writeRegValue_ & 0xFF);
            break;
        case RegisterId::DE:
            i8080_setreg_d((writeRegValue_ >> 8) & 0xFF);
            i8080_setreg_e(writeRegValue_ & 0xFF);
            break;
        case RegisterId::HL:
            i8080_setreg_h((writeRegValue_ >> 8) & 0xFF);
            i8080_setreg_l(writeRegValue_ & 0xFF);
            break;
        case RegisterId::SP:
            i8080_setreg_sp(writeRegValue_);
            break;
        case RegisterId::PC:
            i8080_jump(writeRegValue_);
            break;
    }
}

// ---------------------------------------------------------------------------
// CPU state
// ---------------------------------------------------------------------------

CpuState DebugBackend::getCpuState() const
{
    CpuState s;
    s.pc    = static_cast<uint16_t>(i8080_pc());
    s.sp    = static_cast<uint16_t>(i8080_regs_sp());
    s.a     = static_cast<uint8_t>(i8080_regs_a());
    s.b     = static_cast<uint8_t>(i8080_regs_b());
    s.c     = static_cast<uint8_t>(i8080_regs_c());
    s.d     = static_cast<uint8_t>(i8080_regs_d());
    s.e     = static_cast<uint8_t>(i8080_regs_e());
    s.h     = static_cast<uint8_t>(i8080_regs_h());
    s.l     = static_cast<uint8_t>(i8080_regs_l());
    s.flags = static_cast<uint8_t>(i8080_regs_f());
    s.iff   = i8080_iff();
    // Stage 3.6 — additional CPU state
    s.cycles     = static_cast<uint32_t>(i8080_cycles());
    // Not exposed by the current emulator core.
    // Do not modify src/ just for debugger access.
    s.ei_pending = false;
    s.last_pc    = lastPc_;
    return s;
}

DebuggerState DebugBackend::getState() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

bool DebugBackend::isPaused() const
{
    return state_ == DebuggerState::Paused;
}

// ---------------------------------------------------------------------------
// Execution control
// ---------------------------------------------------------------------------

void DebugBackend::reset()
{
#ifndef DEBUGGER_NO_BOARD
    if (board_) {
        // Use Board's reset for full emulator integration
        board_->reset(Board::ResetMode::BLKSBR);
    } else
#endif
    {
        // Direct CPU reset (for tests)
        i8080_init();
    }
    
    state_ = DebuggerState::Paused;
    pauseRequested_ = false;
    stopReason_ = StopReason::Reset;

    // Clear all debug state (but NOT breakpoints — they survive reset)
    instructionSequence_ = 0;
    impl_->instrHistory.clear();
    impl_->memHistory.clear();
    impl_->ioHistory.clear();
    impl_->clearStats();
    clearActivityCounters();
}

StepResult DebugBackend::stepInstruction()
{
    StepResult r;

    // Track last_pc before execution (Stage 3.6)
    lastPc_ = static_cast<uint16_t>(i8080_pc());
    stopReason_ = StopReason::Step;

    // --- before ---
    r.before   = getCpuState();
    r.pcBefore = r.before.pc;

    // Use DebugMemoryAccess::peek() — performs bank translation without triggering callbacks.
    r.opcode  = DebugMemoryAccess::peek(memory_, r.pcBefore);
    r.length  = opcode_info::get_length(r.opcode);

    // Capture operand bytes BEFORE execution (for trace formatting).
    uint8_t operandBytes[2] = {0, 0};
    if (r.length >= 2) operandBytes[0] = DebugMemoryAccess::peek(memory_, (r.pcBefore + 1) & 0xffff);
    if (r.length >= 3) operandBytes[1] = DebugMemoryAccess::peek(memory_, (r.pcBefore + 2) & 0xffff);

    // Set fetch window: the first r.length memory reads are Fetches.
    fetchRemaining_ = r.length;

    // --- execute exactly one 8080 instruction ---
    int cycles;
#ifndef DEBUGGER_NO_BOARD
    if (board_) {
        // Use Board's single_step for full emulator integration
        board_->single_step(false);  // false = don't update screen
        // Board doesn't return cycles directly, estimate from instruction
        cycles = i8080_cycles();  // This may not be accurate
    } else
#endif
    {
        // Direct CPU execution (for tests)
        int report_opcode = 0;
        cycles = i8080_instruction(&report_opcode);
    }

    // Fetch window should be consumed; reset defensively.
    fetchRemaining_ = 0;

    // --- after ---
    r.after   = getCpuState();
    r.pcAfter = r.after.pc;
    r.cycles  = static_cast<uint32_t>(cycles > 0 ? cycles : 0);

    if (!instrumentationEnabled_) {
        // Skip all recording when instrumentation is disabled.
        return r;
    }

    // --- record InstructionEvent ---
    InstructionEvent ie;
    ie.sequence  = instructionSequence_;
    ie.pcBefore  = r.pcBefore;
    ie.pcAfter   = r.pcAfter;
    ie.opcode    = r.opcode;
    ie.length    = r.length;
    ie.cycles    = cycles;
    ie.operandBytes[0] = operandBytes[0];
    ie.operandBytes[1] = operandBytes[1];
    ie.before    = r.before;
    ie.after     = r.after;

    impl_->instrHistory.push(ie);

    // Stage 4.2: increment per-address execute counter
    executeCount_[r.pcBefore]++;

    // --- trace output (Stage 1 backward compatibility) ---
    if (onTrace) {
        char line[128];
        formatTraceLine(line, sizeof(line), r.pcBefore, r.opcode, operandBytes);
        onTrace(line);
    }

    // --- event callback (Stage 2) ---
    if (onInstruction) {
        onInstruction(ie);
    }

    // Advance sequence AFTER recording
    instructionSequence_++;

    return r;
}

void DebugBackend::run()
{
    state_ = DebuggerState::Running;
    pauseRequested_ = false;

    // "Run after breakpoint" step-over:
    // If PC is currently at an enabled breakpoint, skip it on first check.
    skipBreakpoint_ = checkBreakpoint();

    while (state_ == DebuggerState::Running) {
        // Check breakpoint BEFORE executing the instruction at PC.
        if (checkBreakpoint()) {
            if (skipBreakpoint_) {
                skipBreakpoint_ = false;
                stepInstruction();  // step past the breakpoint
                continue;
            }
            stopReason_ = StopReason::Breakpoint;
            state_ = DebuggerState::Paused;
            break;
        }

        // Check if pause was requested externally.
        if (pauseRequested_) {
            stopReason_ = StopReason::UserPause;
            state_ = DebuggerState::Paused;
            pauseRequested_ = false;
            break;
        }

        // Same instrumentation path as stepInstruction()
        stepInstruction();
    }
}

void DebugBackend::pause()
{
    pauseRequested_ = true;
    if (state_ == DebuggerState::Running) {
        // Will be consumed at the next iteration in run().
    }
}

// ---------------------------------------------------------------------------
// Breakpoints (Stage 3.7)
// ---------------------------------------------------------------------------

std::map<int, DebuggerBreakpoint>::iterator
DebugBackend::findBreakpointByAddress(uint16_t address)
{
    for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
        if (it->second.address == address) return it;
    }
    return breakpoints_.end();
}

std::map<int, DebuggerBreakpoint>::const_iterator
DebugBackend::findBreakpointByAddress(uint16_t address) const
{
    for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
        if (it->second.address == address) return it;
    }
    return breakpoints_.end();
}

int DebugBackend::addBreakpoint(uint16_t address)
{
    // Check for duplicate
    if (findBreakpointByAddress(address) != breakpoints_.end()) {
        return -1;  // duplicate
    }
    int id = nextId_++;
    breakpoints_[id] = { address, true };
    return id;
}

bool DebugBackend::removeBreakpoint(uint16_t address)
{
    auto it = findBreakpointByAddress(address);
    if (it == breakpoints_.end()) return false;
    breakpoints_.erase(it);
    return true;
}

void DebugBackend::removeBreakpoint(int id)
{
    breakpoints_.erase(id);
}

bool DebugBackend::setBreakpointEnabled(uint16_t address, bool enabled)
{
    auto it = findBreakpointByAddress(address);
    if (it == breakpoints_.end()) return false;
    it->second.enabled = enabled;
    return true;
}

bool DebugBackend::hasBreakpoint(uint16_t address) const
{
    auto it = findBreakpointByAddress(address);
    return it != breakpoints_.end() && it->second.enabled;
}

std::vector<DebuggerBreakpoint> DebugBackend::getBreakpoints() const
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    std::vector<DebuggerBreakpoint> result;
    result.reserve(breakpoints_.size());
    for (auto &kv : breakpoints_) {
        result.push_back(kv.second);
    }
    return result;
}

void DebugBackend::clearBreakpoints()
{
    breakpoints_.clear();
}

StopReason DebugBackend::getStopReason() const
{
    return stopReason_;
}

bool DebugBackend::checkBreakpoint()
{
    uint16_t pc = static_cast<uint16_t>(i8080_pc());
    for (auto &kv : breakpoints_) {
        if (kv.second.enabled && kv.second.address == pc) {
            return true;
        }
    }
    return false;
}

void DebugBackend::syncBreakpointsToBoard()
{
#ifndef DEBUGGER_NO_BOARD
    if (!board_) return;

    // Add all enabled debugger breakpoints to Board.
    // Board::insert_breakpoint() adds to its internal list.
    // Board::check_breakpoint() checks PC against this list.
    // We use type=0 (software breakpoint), kind=1.
    for (auto &kv : breakpoints_) {
        if (kv.second.enabled) {
            board_->insert_breakpoint(0, kv.second.address, 1);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Instrumentation hooks — called from Memory callbacks and test HAL
// ---------------------------------------------------------------------------

void DebugBackend::onMemoryRead(uint32_t virt, uint32_t phys,
                                 bool stack, uint8_t value)
{
    if (!instrumentationEnabled_) return;

    MemoryAccessEvent ev;
    ev.instructionSequence = instructionSequence_;

    // Fetch window: the first N reads after stepInstruction() are Fetches.
    if (fetchRemaining_ > 0) {
        ev.type = MemoryAccessType::Fetch;
        fetchRemaining_--;
    } else {
        ev.type = MemoryAccessType::Read;
    }

    ev.virt   = static_cast<uint16_t>(virt & 0xffff);
    ev.phys   = phys;
    ev.value  = value;
    ev.stack  = stack;

    impl_->memHistory.push(ev);

    // Update statistics
    uint16_t addr = ev.virt;
    impl_->memStats[addr].reads++;
    impl_->memStats[addr].lastReadSequence = instructionSequence_;
}

void DebugBackend::onMemoryWrite(uint32_t virt, uint32_t phys,
                                  bool stack, uint8_t value)
{
    if (!instrumentationEnabled_) return;

    MemoryAccessEvent ev;
    ev.instructionSequence = instructionSequence_;
    ev.type   = MemoryAccessType::Write;
    ev.virt   = static_cast<uint16_t>(virt & 0xffff);
    ev.phys   = phys;
    ev.value  = value;
    ev.stack  = stack;

    impl_->memHistory.push(ev);

    // Update statistics
    uint16_t addr = ev.virt;
    impl_->memStats[addr].writes++;
    impl_->memStats[addr].lastWriteSequence = instructionSequence_;

    // Enhanced Vector Screen: track VRAM writes (0xC000..0xC0FF)
    if (addr >= 0xC000 && addr < 0xC100) {
        std::lock_guard<std::mutex> lock(vramWriteMutex_);
        int idx = addr - 0xC000;
        vramLastWrite_[idx].value    = value;
        vramLastWrite_[idx].pc       = static_cast<uint16_t>(i8080_pc());
        vramLastWrite_[idx].sequence = instructionSequence_;
    }
}

void DebugBackend::onIoInput(uint8_t port, uint8_t value)
{
    if (!instrumentationEnabled_) return;

    IoAccessEvent ev;
    ev.instructionSequence = instructionSequence_;
    ev.type  = IoAccessType::In;
    ev.port  = port;
    ev.value = value;

    impl_->ioHistory.push(ev);
}

void DebugBackend::onIoOutput(uint8_t port, uint8_t value)
{
    if (!instrumentationEnabled_) return;

    IoAccessEvent ev;
    ev.instructionSequence = instructionSequence_;
    ev.type  = IoAccessType::Out;
    ev.port  = port;
    ev.value = value;

    impl_->ioHistory.push(ev);

    // Enhanced Vector Screen: track PPI port writes for video mode.
    // Port 0x03 → PA (ScrollStart), Port 0x02 → PB (Mode512 = bit 4).
    // This mirrors IO::realoutput() register updates through the HAL path.
    if (port == 0x03) {
        std::lock_guard<std::mutex> lock(ioRegMutex_);
        ioPA_ = value;
    } else if (port == 0x02) {
        std::lock_guard<std::mutex> lock(ioRegMutex_);
        ioPB_ = value;
    }
}

// ---------------------------------------------------------------------------
// Instrumentation enable/disable
// ---------------------------------------------------------------------------

void DebugBackend::setInstrumentationEnabled(bool enabled)
{
    instrumentationEnabled_ = enabled;
}

bool DebugBackend::isInstrumentationEnabled() const
{
    return instrumentationEnabled_;
}

// ---------------------------------------------------------------------------
// History access (Stage 2.1 — thread-safe snapshots)
// ---------------------------------------------------------------------------

uint64_t DebugBackend::instructionSequence() const
{
    return instructionSequence_;
}

size_t DebugBackend::instructionHistorySize() const
{
    return impl_->instrHistory.size();
}

std::vector<InstructionEvent> DebugBackend::instructionHistorySnapshot() const
{
    return impl_->instrHistory.snapshot();
}

size_t DebugBackend::memoryHistorySize() const
{
    return impl_->memHistory.size();
}

std::vector<MemoryAccessEvent> DebugBackend::memoryHistorySnapshot() const
{
    return impl_->memHistory.snapshot();
}

size_t DebugBackend::ioHistorySize() const
{
    return impl_->ioHistory.size();
}

std::vector<IoAccessEvent> DebugBackend::ioHistorySnapshot() const
{
    return impl_->ioHistory.snapshot();
}

void DebugBackend::clearHistory()
{
    impl_->instrHistory.clear();
    impl_->memHistory.clear();
    impl_->ioHistory.clear();
}

void DebugBackend::clearIoHistory()
{
    impl_->ioHistory.clear();
}

// ---------------------------------------------------------------------------
// Memory statistics (Stage 2.1 — thread-safe snapshot)
// ---------------------------------------------------------------------------

std::vector<MemoryStats> DebugBackend::memoryStatsSnapshot() const
{
    // Copy the entire 65536-entry array.
    // This is ~2MB; acceptable for UI refresh at low frequency.
    return std::vector<MemoryStats>(
        impl_->memStats, impl_->memStats + 65536);
}

// ---------------------------------------------------------------------------
// Stage 4.2: Symbol database
// ---------------------------------------------------------------------------

SymbolDatabase &DebugBackend::symbolDatabase()
{
    return symbols_;
}

const SymbolDatabase &DebugBackend::symbolDatabase() const
{
    return symbols_;
}

// ---------------------------------------------------------------------------
// Stage 4.2: Screen snapshot
// ---------------------------------------------------------------------------

DebugBackend::ScreenSnapshot DebugBackend::screenSnapshot() const
{
    ScreenSnapshot snap;

#ifndef DEBUGGER_NO_BOARD
    if (!board_) return snap;

    std::lock_guard<std::mutex> lock(screenMutex_);

    TV &tv = board_->get_tv();
    uint32_t *pixels = tv.pixels();
    if (!pixels) return snap;

    snap.width  = DEFAULT_SCREEN_WIDTH;
    snap.height = DEFAULT_SCREEN_HEIGHT;
    size_t total = static_cast<size_t>(snap.width) * snap.height;
    snap.pixels.assign(pixels, pixels + total);
#endif

    return snap;
}

// ---------------------------------------------------------------------------
// Stage 4.2: Activity snapshot
// ---------------------------------------------------------------------------

DebugBackend::ActivitySnapshot DebugBackend::activitySnapshot() const
{
    ActivitySnapshot snap;
    snap.executeCount.assign(executeCount_, executeCount_ + 65536);
    snap.readCount.resize(65536);
    snap.writeCount.resize(65536);
    for (int i = 0; i < 65536; ++i) {
        snap.readCount[i]  = impl_->memStats[i].reads;
        snap.writeCount[i] = impl_->memStats[i].writes;
    }
    return snap;
}

void DebugBackend::clearActivityCounters()
{
    std::memset(executeCount_, 0, sizeof(executeCount_));

    // Also clear VRAM write tracking
    {
        std::lock_guard<std::mutex> lock(vramWriteMutex_);
        for (int i = 0; i < 256; ++i) {
            vramLastWrite_[i] = VramWriteInfo();
        }
    }

    // Reset tracked IO port values to defaults
    {
        std::lock_guard<std::mutex> lock(ioRegMutex_);
        ioPA_ = 0xFF;
        ioPB_ = 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Enhanced Vector Screen: Video mode snapshot
// ---------------------------------------------------------------------------

DebugBackend::VideoModeSnapshot DebugBackend::videoModeSnapshot() const
{
    VideoModeSnapshot snap;

#ifndef DEBUGGER_NO_BOARD
    if (!board_) return snap;

    // Framebuffer dimensions from TV
    snap.screenWidth  = DEFAULT_SCREEN_WIDTH;
    snap.screenHeight = DEFAULT_SCREEN_HEIGHT;

    // Read video mode from tracked IO port values (not from IO directly).
    // ioPB_ bit 4 = Mode512, ioPA_ = ScrollStart (PA).
    // These are updated in onIoOutput() via the HAL instrumentation path.
    {
        std::lock_guard<std::mutex> lock(ioRegMutex_);
        snap.mode512     = (ioPB_ & 0x10) != 0;
        snap.scrollValue  = ioPA_;
    }

    snap.visibleWidth  = snap.mode512 ? 512 : 256;
    snap.visibleHeight = 256;
    snap.pixelsPerByte = snap.mode512 ? 4 : 8;

    // Border offsets: (screenWidth - visibleWidth) / 2 for horizontal,
    // (screenHeight - visibleHeight) / 2 for vertical
    snap.borderLeft = (snap.screenWidth - snap.visibleWidth) / 2;
    snap.borderTop  = (snap.screenHeight - snap.visibleHeight) / 2;
#endif

    return snap;
}

// ---------------------------------------------------------------------------
// Enhanced Vector Screen: VRAM write snapshot
// ---------------------------------------------------------------------------

DebugBackend::VramWriteSnapshot DebugBackend::vramWriteSnapshot() const
{
    VramWriteSnapshot snap;
    std::lock_guard<std::mutex> lock(vramWriteMutex_);
    snap.lastWrite.assign(vramLastWrite_, vramLastWrite_ + 256);
    return snap;
}

// ---------------------------------------------------------------------------
// Debug trace formatting (Stage 1, kept for backward compatibility)
// ---------------------------------------------------------------------------

void DebugBackend::formatTraceLine(char *buf, size_t bufsize,
                                   uint16_t pc, uint8_t opcode,
                                   const uint8_t *operandBytes) const
{
    uint8_t len = opcode_info::get_length(opcode);

    if (len == 1) {
        snprintf(buf, bufsize, "PC=%04X  %02X", pc, opcode);
    } else if (len == 2) {
        snprintf(buf, bufsize, "PC=%04X  %02X %02X", pc, opcode, operandBytes[0]);
    } else { // len == 3
        // operandBytes[0] = low byte, operandBytes[1] = high byte (little-endian)
        snprintf(buf, bufsize, "PC=%04X  %02X %02X%02X",
                 pc, opcode, operandBytes[1], operandBytes[0]);
    }
}

// ---------------------------------------------------------------------------
// Thread-safe command API (Stage 3.1 — for GUI)
// ---------------------------------------------------------------------------

void DebugBackend::requestStep()
{
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        pendingCommand_ = PendingCommand::Step;
        stepCompleted_  = false;
    }
    commandCv_.notify_one();

    // Wait for the emulation thread to complete the step.
    std::unique_lock<std::mutex> lock(commandMutex_);
    resultCv_.wait(lock, [this]{ return stepCompleted_; });
}

void DebugBackend::requestRun()
{
    // "Run after breakpoint" step-over:
    // If PC is currently at an enabled breakpoint, skip it on first check.
    skipBreakpoint_ = checkBreakpoint();

    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        state_ = DebuggerState::Running;
        pauseRequested_ = false;
    }
    running_.store(true, std::memory_order_release);
    commandCv_.notify_one();
}

void DebugBackend::requestPause()
{
    running_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        pauseRequested_ = true;
    }
#ifndef DEBUGGER_NO_BOARD
    // Break out of current frame immediately
    if (board_) board_->debugger_break();
#endif
    commandCv_.notify_one();
}

void DebugBackend::requestReset()
{
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        pendingCommand_ = PendingCommand::Reset;
        stepCompleted_  = false;
    }
    commandCv_.notify_one();

    std::unique_lock<std::mutex> lock(commandMutex_);
    resultCv_.wait(lock, [this]{ return stepCompleted_; });
}

void DebugBackend::waitForCompletion()
{
    std::unique_lock<std::mutex> lock(commandMutex_);
    resultCv_.wait(lock, [this]{ return stepCompleted_; });
}

void DebugBackend::requestQuit()
{
    {
        std::lock_guard<std::mutex> lock(commandMutex_);
        quitRequested_  = true;
        pendingCommand_ = PendingCommand::Quit;
    }
    commandCv_.notify_one();
}

bool DebugBackend::isQuitRequested() const
{
    std::lock_guard<std::mutex> lock(commandMutex_);
    return quitRequested_;
}

void DebugBackend::processOneCommand()
{
    std::unique_lock<std::mutex> lock(commandMutex_);
    commandCv_.wait(lock, [this]{
        return pendingCommand_ != PendingCommand::None ||
               running_.load(std::memory_order_acquire) ||
               quitRequested_;
    });

    if (quitRequested_) return;

    // If running flag is set, just return — the main loop handles execution
    if (running_.load(std::memory_order_acquire)) return;

    PendingCommand cmd = pendingCommand_;
    pendingCommand_ = PendingCommand::None;

    if (cmd == PendingCommand::Quit) {
#ifndef DEBUGGER_NO_BOARD
        if (board_) {
            board_->debugger_detached();
        }
#endif
        return;
    }
    else if (cmd == PendingCommand::Step) {
        stepInstruction();
        stepCompleted_ = true;
        resultCv_.notify_one();
        return;
    }
    else if (cmd == PendingCommand::Reset) {
        running_.store(false, std::memory_order_release);
        reset();
        stepCompleted_ = true;
        resultCv_.notify_one();
        return;
    }
    else if (cmd == PendingCommand::MemoryWrite) {
        lock.unlock();
        processWriteCommand();
        {
            std::lock_guard<std::mutex> lk(commandMutex_);
            stepCompleted_ = true;
        }
        resultCv_.notify_one();
        return;
    }
    else if (cmd == PendingCommand::RegisterWrite) {
        lock.unlock();
        processRegisterWrite();
        {
            std::lock_guard<std::mutex> lk(commandMutex_);
            stepCompleted_ = true;
        }
        resultCv_.notify_one();
        return;
    }
    // Run and Pause are handled via running_ atomic, not command queue
}

// ---------------------------------------------------------------------------
// executeFramesBoard_ — run frames until pause/breakpoint (Board attached)
// ---------------------------------------------------------------------------

#ifndef DEBUGGER_NO_BOARD
void DebugBackend::executeFramesBoard_()
{
    // Sync debugger breakpoints to Board before running
    syncBreakpointsToBoard();

    // Handle "run after breakpoint" step-over
    if (skipBreakpoint_ && checkBreakpoint()) {
        skipBreakpoint_ = false;
        stepInstruction();  // step past the breakpoint
    }

    while (running_.load(std::memory_order_acquire)) {
        if (quitRequested_) {
            board_->debugger_break();
            board_->debugger_detached();
            return;
        }

        board_->debugger_continue();
        uint16_t pcBeforeFrame = static_cast<uint16_t>(i8080_pc());
        board_->execute_frame_with_cadence(false, false);
        uint16_t pcAfterFrame = static_cast<uint16_t>(i8080_pc());

        // If PC didn't change, Board stopped (breakpoint or debugger_interrupt)
        if (pcAfterFrame == pcBeforeFrame) {
            if (checkBreakpoint()) {
                stopReason_ = StopReason::Breakpoint;
                running_.store(false, std::memory_order_release);
                {
                    std::lock_guard<std::mutex> lk(commandMutex_);
                    state_ = DebuggerState::Paused;
                }
                break;
            }
            // If no breakpoint but PC didn't change, debugger_interrupt was set
            // (e.g. by poll_debugger for pause/step). Just continue the loop —
            // running_ will be checked.
        }

        // Check pause requested (from GUI or poll_debugger)
        if (pauseRequested_ || !running_.load(std::memory_order_acquire)) {
            running_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(commandMutex_);
                stopReason_ = StopReason::UserPause;
                state_ = DebuggerState::Paused;
                pauseRequested_ = false;
            }
            break;
        }

        // Also check our own breakpoint list
        if (checkBreakpoint()) {
            stopReason_ = StopReason::Breakpoint;
            running_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(commandMutex_);
                state_ = DebuggerState::Paused;
            }
            break;
        }
    }
}
#endif // DEBUGGER_NO_BOARD

// ---------------------------------------------------------------------------
// executeFramesNoBoard_ — run instructions until pause/breakpoint (no Board)
// ---------------------------------------------------------------------------

void DebugBackend::executeFramesNoBoard_()
{
    while (running_.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> lk(commandMutex_);
            if (quitRequested_) return;
        }
        if (checkBreakpoint()) {
            if (skipBreakpoint_) {
                skipBreakpoint_ = false;
                stepInstruction();  // step past the breakpoint
                continue;
            }
            stopReason_ = StopReason::Breakpoint;
            running_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(commandMutex_);
                state_ = DebuggerState::Paused;
            }
            break;
        }
        if (pauseRequested_ || !running_.load(std::memory_order_acquire)) {
            running_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(commandMutex_);
                stopReason_ = StopReason::UserPause;
                state_ = DebuggerState::Paused;
                pauseRequested_ = false;
            }
            break;
        }
        stepInstruction();
    }
}

void DebugBackend::runUntilPause()
{
#ifndef DEBUGGER_NO_BOARD
    // If Board is attached, set up poll_debugger callback for command checking
    if (board_) {
        board_->poll_debugger = [this]() {
            std::lock_guard<std::mutex> lock(commandMutex_);
            
            if (quitRequested_ || pendingCommand_ == PendingCommand::Quit) {
                board_->debugger_break();
                return;
            }
            
            if (pendingCommand_ == PendingCommand::Step) {
                pendingCommand_ = PendingCommand::None;
                stepInstruction();
                stepCompleted_ = true;
                resultCv_.notify_one();
                board_->debugger_break();
                return;
            }
            
            // Check if pause was requested (via running_ flag or pauseRequested_)
            if (!running_.load(std::memory_order_relaxed) || pauseRequested_) {
                pauseRequested_ = true;
                board_->debugger_break();
                return;
            }

            // MemoryWrite while running: reject — must be Paused.
            if (pendingCommand_ == PendingCommand::MemoryWrite) {
                writeResult_ = false;
                pendingCommand_ = PendingCommand::None;
                stepCompleted_ = true;
                resultCv_.notify_one();
                return;
            }

            // RegisterWrite while running: reject — must be Paused.
            if (pendingCommand_ == PendingCommand::RegisterWrite) {
                writeRegResult_ = false;
                pendingCommand_ = PendingCommand::None;
                stepCompleted_ = true;
                resultCv_.notify_one();
                return;
            }
        };
        
        // Enable debugging mode for breakpoint checking
        board_->debugger_attached();
    }
#endif
    
    // Main emulation loop (PSP-style)
    while (true) {
        // Wait for a command or running flag
        processOneCommand();

        if (quitRequested_) {
#ifndef DEBUGGER_NO_BOARD
            if (board_) board_->debugger_detached();
#endif
            return;
        }

        // If running, execute frames until pause/breakpoint
        if (running_.load(std::memory_order_acquire)) {
#ifndef DEBUGGER_NO_BOARD
            if (board_) {
                executeFramesBoard_();
            } else
#endif
            {
                executeFramesNoBoard_();
            }

            // After execution stopped, update state
            {
                std::lock_guard<std::mutex> lk(commandMutex_);
                if (state_ != DebuggerState::Paused) {
                    state_ = DebuggerState::Paused;
                }
            }
        }
    }
}
