#include "backend.h"
#include "events.h"
#include "opcode_info.h"
#include "ring_buffer.h"
#include "debug_memory.h"

#include <cstdio>
#include <cstring>
#include <climits>
#include <memory>
#include <thread>


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
        using TimePoint = MemoryStats::TimePoint;
        for (int i = 0; i < 65536; ++i) {
            memStats[i].reads = 0;
            memStats[i].writes = 0;
            memStats[i].lastReadSequence = UINT64_MAX;
            memStats[i].lastWriteSequence = UINT64_MAX;
            memStats[i].lastReadTime = TimePoint::min();
            memStats[i].lastWriteTime = TimePoint::min();
        }
    }
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DebugBackend::DebugBackend(IDebugTarget &target)
    : target_(&target)
    , state_(DebuggerState::Paused)
    , nextId_(1)
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
    // Clear memory callbacks before destroying
    if (target_) {
        target_->setMemoryCallbacks(nullptr, nullptr);
    }
    delete impl_;
}

// ---------------------------------------------------------------------------
// Target integration (Stage 3.13a)
// ---------------------------------------------------------------------------

void DebugBackend::attachTarget(IDebugTarget *target)
{
    target_ = target;
}

bool DebugBackend::loadRom(const std::string &path, uint32_t org)
{
    if (!target_) return false;

    // Delegate ROM loading to target (handles memory + CPU/board init)
    if (!target_->loadRom(path, org)) {
        printf("DebugBackend::loadRom(): failed to load %s\n", path.c_str());
        return false;
    }

    printf("DebugBackend::loadRom(): loaded %s at %04x\n", path.c_str(), org);

    // Clear debug history
    clearHistory();
    instructionSequence_ = 0;

    // Set state to Paused — regardless of previous running state.
    // running_ must be cleared so the emulation thread stops executing
    // frames and blocks on the command queue until the user presses RUN.
    running_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = DebuggerState::Paused;
    }
    pauseRequestedAtomic_.store(false, std::memory_order_release);

    return true;
}

// ---------------------------------------------------------------------------
// Keyboard injection
// ---------------------------------------------------------------------------

void DebugBackend::pressKey(int scancode)
{
    if (target_) target_->pressKey(scancode);
}

void DebugBackend::releaseKey(int scancode)
{
    if (target_) target_->releaseKey(scancode);
}

// ---------------------------------------------------------------------------
// Memory callback installation (with chaining)
// ---------------------------------------------------------------------------

void DebugBackend::installMemoryCallbacks()
{
    if (!target_) return;

    DebugBackend *self = this;

    IDebugTarget::MemoryReadCallback readCb =
        [self](uint32_t virt, uint32_t phys, bool stack, uint8_t value) {
            self->onMemoryRead(virt, phys, stack, value);
        };

    IDebugTarget::MemoryWriteCallback writeCb =
        [self](uint32_t virt, uint32_t phys, bool stack, uint8_t value) {
            self->onMemoryWrite(virt, phys, stack, value);
        };

    target_->setMemoryCallbacks(readCb, writeCb);
}

// ---------------------------------------------------------------------------
// Memory access (Stage 3.3 — for Memory Inspector)
// ---------------------------------------------------------------------------

uint8_t DebugBackend::readMemory(uint16_t address)
{
    return target_->readMemory(address);
}

MemorySnapshot DebugBackend::readMemorySnapshot(uint16_t start, size_t size)
{
    MemorySnapshot snapshot;
    snapshot.start = start;
    snapshot.data.reserve(size);

    for (size_t i = 0; i < size; ++i) {
        uint16_t addr = static_cast<uint16_t>((start + i) & 0xFFFF);
        snapshot.data.push_back(target_->readMemoryRaw(addr));
    }

    return snapshot;
}

// ---------------------------------------------------------------------------
// Memory write (Stage 5.3.2 — through Command Queue)
// ---------------------------------------------------------------------------

bool DebugBackend::writeMemoryByte(uint16_t address, uint8_t value)
{
    return writeMemory(address, &value, 1);
}

bool DebugBackend::writeMemory(uint16_t address, const uint8_t* data, size_t size)
{
    if (!data || size == 0) return false;

    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::MemoryWrite;
    cmd->address = address;
    cmd->writeData.assign(data, data + size);
    auto result = submitAndWait(std::move(cmd));
    return result.success;
}

// ---------------------------------------------------------------------------
// Register write (Stage 5.3.2 — through Command Queue)
// ---------------------------------------------------------------------------

bool DebugBackend::writeRegister(RegisterId id, uint16_t value)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::RegisterWrite;
    cmd->regId = id;
    cmd->regValue = value;
    auto result = submitAndWait(std::move(cmd));
    return result.success;
}

// ---------------------------------------------------------------------------
// CPU state
// ---------------------------------------------------------------------------

CpuState DebugBackend::getCpuState() const
{
    CpuState s = target_->getCpuState();
    s.last_pc = lastPc_;
    return s;
}

DebuggerState DebugBackend::getState() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

bool DebugBackend::isPaused() const
{
    // Stage 5.3.3.2: thread-safe read of state_ via stateMutex_
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_ == DebuggerState::Paused;
}

// ---------------------------------------------------------------------------
// Execution control
// ---------------------------------------------------------------------------

void DebugBackend::reset()
{
    target_->reset(false);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = DebuggerState::Paused;
    }
    pauseRequestedAtomic_.store(false, std::memory_order_release);
    stopReason_ = StopReason::Reset;

    instructionSequence_ = 0;
    impl_->instrHistory.clear();
    impl_->memHistory.clear();
    impl_->ioHistory.clear();
    impl_->clearStats();
    clearActivityCounters();
}

void DebugBackend::stepInstruction()
{
    stepInstructionDetailed();
}

StepResult DebugBackend::stepInstructionDetailed()
{
    StepResult r;

    // Track last_pc before execution (Stage 3.6)
    lastPc_ = target_->getCpuState().pc;
    stopReason_ = StopReason::Step;

    // --- before ---
    r.before   = target_->getCpuState();
    r.before.last_pc = lastPc_;
    r.pcBefore = r.before.pc;

    // Read opcode via peek (no callbacks)
    r.opcode  = target_->peekMemory(r.pcBefore);
    r.length  = opcode_info::get_length(r.opcode);

    // Capture operand bytes BEFORE execution
    uint8_t operandBytes[2] = {0, 0};
    if (r.length >= 2) operandBytes[0] = target_->peekMemory((r.pcBefore + 1) & 0xffff);
    if (r.length >= 3) operandBytes[1] = target_->peekMemory((r.pcBefore + 2) & 0xffff);

    // Set fetch window
    fetchRemaining_ = r.length;

    // --- execute exactly one 8080 instruction ---
    target_->stepInstruction();
    int cycles = target_->getCpuState().cycles;

    // Fetch window should be consumed; reset defensively.
    fetchRemaining_ = 0;

    // --- after ---
    r.after   = target_->getCpuState();
    r.after.last_pc = lastPc_;
    r.pcAfter = r.after.pc;
    r.cycles  = static_cast<uint32_t>(cycles > 0 ? cycles : 0);

    if (!instrumentationEnabled_) {
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

    // --- trace output ---
    if (onTrace) {
        char line[128];
        formatTraceLine(line, sizeof(line), r.pcBefore, r.opcode, operandBytes);
        onTrace(line);
    }

    // --- event callback ---
    if (onInstruction) {
        onInstruction(ie);
    }

    instructionSequence_++;

    return r;
}

void DebugBackend::run()
{
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        state_ = DebuggerState::Running;
    }
    pauseRequestedAtomic_.store(false, std::memory_order_release);

    skipBreakpoint_ = checkBreakpoint();

    while (true) {
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (state_ != DebuggerState::Running) break;
        }

        if (checkBreakpoint()) {
            if (skipBreakpoint_) {
                skipBreakpoint_ = false;
                stepInstruction();
                continue;
            }
            stopReason_ = StopReason::Breakpoint;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                state_ = DebuggerState::Paused;
            }
            break;
        }

        if (pauseRequestedAtomic_.load(std::memory_order_acquire)) {
            stopReason_ = StopReason::UserPause;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                state_ = DebuggerState::Paused;
            }
            pauseRequestedAtomic_.store(false, std::memory_order_release);
            break;
        }

        stepInstruction();
    }

    // Stage 5.3.3.3: Fulfill any pending Run pause promises after pausing
    fulfillPausePromises_();
}

void DebugBackend::pause()
{
    // Stage 5.3.3.1: Use atomic pause signal
    pauseRequestedAtomic_.store(true, std::memory_order_release);
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
    if (findBreakpointByAddress(address) != breakpoints_.end()) {
        return -1;
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
    uint16_t pc = target_->getCpuState().pc;
    for (auto &kv : breakpoints_) {
        if (kv.second.enabled && kv.second.address == pc) {
            return true;
        }
    }
    return false;
}

void DebugBackend::syncBreakpointsToTarget()
{
    if (!target_) return;

    std::vector<DebuggerBreakpoint> bps;
    bps.reserve(breakpoints_.size());
    for (auto &kv : breakpoints_) {
        if (kv.second.enabled) {
            bps.push_back(kv.second);
        }
    }
    target_->syncBreakpoints(bps.data(), bps.size());
}

// ---------------------------------------------------------------------------
// Instrumentation hooks
// ---------------------------------------------------------------------------

void DebugBackend::onMemoryRead(uint32_t virt, uint32_t phys,
                                 bool stack, uint8_t value)
{
    if (!instrumentationEnabled_) return;

    MemoryAccessEvent ev;
    ev.instructionSequence = instructionSequence_;

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

    uint16_t addr = ev.virt;
    impl_->memStats[addr].reads++;
    impl_->memStats[addr].lastReadSequence = instructionSequence_;
    impl_->memStats[addr].lastReadTime = MemoryStats::Clock::now();
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

    uint16_t addr = ev.virt;
    impl_->memStats[addr].writes++;
    impl_->memStats[addr].lastWriteSequence = instructionSequence_;
    impl_->memStats[addr].lastWriteTime = MemoryStats::Clock::now();

    // Enhanced Vector Screen: track VRAM writes (0xC000..0xC0FF)
    if (addr >= 0xC000 && addr < 0xC100) {
        std::lock_guard<std::mutex> lock(vramWriteMutex_);
        int idx = addr - 0xC000;
        vramLastWrite_[idx].value    = value;
        vramLastWrite_[idx].pc       = target_->getCpuState().pc;
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
    return std::vector<MemoryStats>(
        impl_->memStats, impl_->memStats + 65536);
}

// ---------------------------------------------------------------------------
// Stage 5.3.2: Symbol commands (through Command Queue)
// ---------------------------------------------------------------------------

CommandResult DebugBackend::requestCreateFunction(uint16_t addr, const std::string &name)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::CreateFunction;
    cmd->address = addr;
    cmd->name = name;
    return submitAndWait(std::move(cmd));
}

CommandResult DebugBackend::requestRenameSymbol(uint16_t addr, const std::string &name)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::RenameSymbol;
    cmd->address = addr;
    cmd->name = name;
    return submitAndWait(std::move(cmd));
}

CommandResult DebugBackend::requestSetComment(uint16_t addr, const std::string &comment)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::SetComment;
    cmd->address = addr;
    cmd->comment = comment;
    return submitAndWait(std::move(cmd));
}

CommandResult DebugBackend::requestRemoveSymbol(uint16_t addr)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::RemoveSymbol;
    cmd->address = addr;
    return submitAndWait(std::move(cmd));
}

CommandResult DebugBackend::requestAddLabel(uint16_t addr, const std::string &name)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::AddLabel;
    cmd->address = addr;
    cmd->name = name;
    return submitAndWait(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Stage 5.3.2: Breakpoint commands (through Command Queue)
// ---------------------------------------------------------------------------

CommandResult DebugBackend::requestAddBreakpoint(uint16_t addr)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::AddBreakpoint;
    cmd->address = addr;
    return submitAndWait(std::move(cmd));
}

CommandResult DebugBackend::requestRemoveBreakpoint(uint16_t addr)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::RemoveBreakpoint;
    cmd->address = addr;
    return submitAndWait(std::move(cmd));
}

CommandResult DebugBackend::requestSetBreakpointEnabled(uint16_t addr, bool enabled)
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::SetBreakpointEnabled;
    cmd->address = addr;
    cmd->enabled = enabled;
    return submitAndWait(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Stage 5.3.2: Command Queue implementation
// ---------------------------------------------------------------------------

// -- CommandQueue methods --

void DebugBackend::CommandQueue::enqueue(std::unique_ptr<Command> cmd)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(cmd));
    }
    cv_.notify_one();
}

std::unique_ptr<DebugBackend::Command> DebugBackend::CommandQueue::tryDequeue()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return nullptr;
    auto cmd = std::move(queue_.front());
    queue_.pop();
    return cmd;
}

std::unique_ptr<DebugBackend::Command> DebugBackend::CommandQueue::waitAndDequeue()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]{ return !queue_.empty(); });
    auto cmd = std::move(queue_.front());
    queue_.pop();
    return cmd;
}

bool DebugBackend::CommandQueue::empty() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

// -- submitAndWait: dual-mode command submission --

CommandResult DebugBackend::submitAndWait(std::unique_ptr<Command> cmd)
{
    // Get future before moving cmd — promise.get_future() can only be called once
    auto future = cmd->promise.get_future();

    // Stage 5.3.3.1: Test-only synchronous fallback.
    // Production code must NEVER use this path — all production goes through the queue.
    if (testSynchronous_ && !emulationLoopRunning_) {
        // Check cancellation before execution (matches production path in processCommand)
        if (cmd->cancelled.load(std::memory_order_acquire)) {
            CommandResult r;
            r.success = false;
            r.error = "cancelled";
            r.status = CommandResult::Cancelled;
            cmd->promise.set_value(r);
            return future.get();
        }
        cmd->state.store(CommandState::Executing, std::memory_order_release);
        executeCommand(*cmd);
        return future.get();
    }

    // Stage 5.3.3.1: Production path — always enqueue.
    Command *rawCmd = cmd.get();
    commandQueue_.enqueue(std::move(cmd));

    auto status = future.wait_for(std::chrono::seconds(5));
    if (status == std::future_status::timeout) {
        // Stage 5.3.3.2: Check command state before deciding on timeout.
        auto cmdState = rawCmd->state.load(std::memory_order_acquire);
        if (cmdState == CommandState::Executing) {
            // Command is already executing — must wait for completion.
            // Do NOT return Timeout for an executing state-changing command.
            return future.get();
        }
        // Stage 5.3.3.2: CAS Queued→Cancelled — mutually exclusive with Queued→Executing
        CommandState expected = CommandState::Queued;
        if (rawCmd->state.compare_exchange_strong(expected, CommandState::Cancelled,
                                                  std::memory_order_acq_rel)) {
            // Successfully cancelled — command won't execute
            CommandResult r;
            r.success = false;
            r.error = "command timed out";
            r.status = CommandResult::Timeout;
            return r;
        }
        // CAS failed — emulation thread already took the command. Wait.
        return future.get();
    }
    return future.get();
}

// -- executeCommand: runs on Emulation Thread --

void DebugBackend::executeCommand(Command &cmd)
{
    CommandResult result;
    result.success = true;
    result.status = CommandResult::Completed;

    // Stage 5.3.3: Check cancellation before state-changing operations
    if (cmd.cancelled.load(std::memory_order_acquire)) {
        result.success = false;
        result.error = "cancelled";
        result.status = CommandResult::Cancelled;
        cmd.promise.set_value(result);
        return;
    }

    switch (cmd.type) {
    case CommandType::AddBreakpoint: {
        int id = addBreakpoint(cmd.address);
        if (id >= 0) {
            result.success = true;
        } else {
            result.success = false;
            result.error = "breakpoint already exists at this address";
            result.status = CommandResult::Failed;
        }
        syncBreakpointsToTarget();
        break;
    }
    case CommandType::RemoveBreakpoint: {
        bool ok = removeBreakpoint(cmd.address);
        result.success = ok;
        if (!ok) {
            result.error = "no breakpoint at this address";
            result.status = CommandResult::Failed;
        }
        syncBreakpointsToTarget();
        break;
    }
    case CommandType::SetBreakpointEnabled: {
        bool ok = setBreakpointEnabled(cmd.address, cmd.enabled);
        result.success = ok;
        if (!ok) {
            result.error = "no breakpoint at this address";
            result.status = CommandResult::Failed;
        }
        syncBreakpointsToTarget();
        break;
    }
    case CommandType::CreateFunction: {
        bool ok = symbols_.addSymbol(cmd.address, cmd.name, SymbolType::Function);
        result.success = ok;
        if (!ok) {
            result.error = "symbol already exists at this address";
            result.status = CommandResult::Failed;
        }
        break;
    }
    case CommandType::RenameSymbol: {
        bool ok = symbols_.renameSymbol(cmd.address, cmd.name);
        result.success = ok;
        if (!ok) {
            result.error = "symbol not found";
            result.status = CommandResult::Failed;
        }
        break;
    }
    case CommandType::SetComment: {
        bool ok = symbols_.setComment(cmd.address, cmd.comment);
        result.success = ok;
        if (!ok) {
            result.error = "symbol not found";
            result.status = CommandResult::Failed;
        }
        break;
    }
    case CommandType::RemoveSymbol: {
        bool ok = symbols_.removeSymbol(cmd.address);
        result.success = ok;
        if (!ok) {
            result.error = "symbol not found";
            result.status = CommandResult::Failed;
        }
        break;
    }
    case CommandType::AddLabel: {
        bool ok = symbols_.addSymbol(cmd.address, cmd.name, SymbolType::Label);
        result.success = ok;
        if (!ok) {
            result.error = "symbol already exists at this address";
            result.status = CommandResult::Failed;
        }
        break;
    }
    case CommandType::Step: {
        stepInstruction();
        break;
    }
    case CommandType::Reset: {
        running_.store(false, std::memory_order_release);
        reset();
        break;
    }
    case CommandType::MemoryWrite: {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ == DebuggerState::Paused) {
            for (size_t i = 0; i < cmd.writeData.size(); ++i) {
                uint16_t addr = static_cast<uint16_t>((cmd.address + i) & 0xFFFF);
                target_->writeMemory(addr, cmd.writeData[i]);
            }
            result.success = true;
        } else {
            result.success = false;
            result.error = "cannot write memory while running";
            result.status = CommandResult::Failed;
        }
        break;
    }
    case CommandType::RegisterWrite: {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ == DebuggerState::Paused) {
            target_->writeCpuRegister(static_cast<int>(cmd.regId), cmd.regValue);
            result.success = true;
        } else {
            result.success = false;
            result.error = "cannot write register while running";
            result.status = CommandResult::Failed;
        }
        break;
    }
    case CommandType::ExecuteTrace: {
        if (traceBusy_.exchange(true)) {
            result.success = false;
            result.error = "trace busy";
            result.status = CommandResult::Failed;
            if (cmd.tracePromise) cmd.tracePromise->set_value(TraceExecutionResult{});
        } else {
            auto traceResult = executeTraceInternal(cmd.traceParams);
            result.success = true;
            traceBusy_.store(false);
            if (cmd.tracePromise) cmd.tracePromise->set_value(std::move(traceResult));
        }
        break;
    }
    case CommandType::Run:
        // Stage 5.3.3.3: Save pausePromise to pending list.
        // It will be fulfilled when the emulation actually transitions to Paused
        // (by run(), executeFramesTarget_(), or processCommand() post-check).
        if (cmd.pausePromise) {
            std::lock_guard<std::mutex> lk(pendingPauseMutex_);
            pendingPausePromises_.push_back(cmd.pausePromise);
        }
        // Stage 5.3.3.2: Use stateMutex_ for state_ protection.
        running_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_ = DebuggerState::Running;
            pauseRequestedAtomic_.store(false, std::memory_order_release);
        }
        skipBreakpoint_ = checkBreakpoint();
        break;
    case CommandType::Pause:
    case CommandType::Quit:
        // These are handled by the emulation loop directly, not through executeCommand.
        result.success = true;
        break;
    default:
        result.success = false;
        result.error = "unknown command type";
        result.status = CommandResult::Failed;
        break;
    }

    cmd.promise.set_value(result);
}

// ---------------------------------------------------------------------------
// Stage 5.3.2: Trace execution (through Command Queue)
// ---------------------------------------------------------------------------

std::future<DebugBackend::TraceExecutionResult>
DebugBackend::requestExecuteTrace(const TraceExecutionParams &params)
{
    // Stage 5.3.3.1: Always through Command Queue.
    // No direct-execution fallback in production.
    auto tracePromise = std::make_shared<std::promise<TraceExecutionResult>>();
    auto traceResultFuture = tracePromise->get_future();

    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::ExecuteTrace;
    cmd->traceParams = params;
    cmd->tracePromise = tracePromise;

    if (testSynchronous_ && !emulationLoopRunning_) {
        // Test-only: execute synchronously
        cmd->state.store(CommandState::Executing, std::memory_order_release);
        executeCommand(*cmd);
    } else {
        // Production: enqueue and wait for command completion
        auto cmdFuture = cmd->promise.get_future();
        commandQueue_.enqueue(std::move(cmd));
        cmdFuture.wait();  // Wait until trace result is delivered
    }

    return traceResultFuture;
}

// ---------------------------------------------------------------------------
// executeTraceInternal — runs exclusively on Emulation Thread
// ---------------------------------------------------------------------------

DebugBackend::TraceExecutionResult
DebugBackend::executeTraceInternal(const TraceExecutionParams &params)
{
    TraceExecutionResult result;
    result.startSequence = instructionSequence_;
    result.entrySp = target_->getCpuState().sp;
    result.minSp = result.entrySp;
    result.maxSp = result.entrySp;
    result.exitReason = ExitReason::Unknown;

    for (uint32_t i = 0; i < params.maxInstructions; ++i) {
        // Stage 5.3.3: Check for break/quit requests during trace
        if (breakRequested_.load(std::memory_order_acquire) ||
            quitRequested_.load(std::memory_order_acquire)) {
            if (result.exitReason == ExitReason::Unknown) {
                result.exitReason = ExitReason::Timeout;
                result.exitPc = target_->getCpuState().pc;
            }
            break;
        }

        uint16_t pc = target_->getCpuState().pc;
        uint8_t opcode = target_->peekMemory(pc);

        // Execute one instruction
        stepInstruction();

        // Track SP bounds
        uint16_t newSp = target_->getCpuState().sp;
        if (newSp < result.minSp) result.minSp = newSp;
        if (newSp > result.maxSp) result.maxSp = newSp;

        // Check exit conditions
        if (params.stopOnRet && opcode == 0xC9) {
            result.exitReason = ExitReason::Ret;
            result.exitPc = pc;
            break;
        }

        if (opcode == 0x76) {
            result.exitReason = ExitReason::Halt;
            result.exitPc = pc;
            break;
        }

        if (params.stopOnCallerReturn && params.callerReturnAddress != 0
            && target_->getCpuState().pc == params.callerReturnAddress) {
            result.exitReason = ExitReason::CallerReturn;
            result.exitPc = pc;
            break;
        }
    }

    // If loop finished without exit
    if (result.exitReason == ExitReason::Unknown) {
        result.exitReason = ExitReason::Timeout;
        result.exitPc = target_->getCpuState().pc;
    }

    result.endSequence = instructionSequence_;
    result.instructionsExecuted = static_cast<uint32_t>(result.endSequence - result.startSequence);
    result.exitSp = target_->getCpuState().sp;

    return result;
}

// ---------------------------------------------------------------------------
// Stage 4.2: Screen snapshot
// ---------------------------------------------------------------------------

DebugBackend::ScreenSnapshot DebugBackend::screenSnapshot() const
{
    ScreenSnapshot snap;

    std::lock_guard<std::mutex> lock(screenMutex_);

    ScreenData data = target_->screenSnapshot();
    snap.pixels = std::move(data.pixels);
    snap.width  = data.width;
    snap.height = data.height;

    return snap;
}

// ---------------------------------------------------------------------------
// Palette snapshot
// ---------------------------------------------------------------------------

PaletteSnapshot DebugBackend::paletteSnapshot() const
{
    return target_->paletteSnapshot();
}

// ---------------------------------------------------------------------------
// Sound snapshot
// ---------------------------------------------------------------------------

SoundSnapshot DebugBackend::soundSnapshot() const
{
    return target_->soundSnapshot();
}

void DebugBackend::setMuted(bool muted)
{
    target_->setMuted(muted);
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

    {
        std::lock_guard<std::mutex> lock(vramWriteMutex_);
        for (int i = 0; i < 256; ++i) {
            vramLastWrite_[i] = VramWriteInfo();
        }
    }

    {
        std::lock_guard<std::mutex> lock(ioRegMutex_);
        ioPA_ = 0xFF;
        ioPB_ = 0xFF;
    }
}

// ---------------------------------------------------------------------------
// Stage 5.2: Live Activity snapshot (for Memory Map)
// ---------------------------------------------------------------------------

DebugBackend::LiveActivitySnapshot DebugBackend::liveActivitySnapshot() const
{
    LiveActivitySnapshot snap;

    using TimePoint = std::chrono::steady_clock::time_point;
    TimePoint minTime = TimePoint::min();

    for (int i = 0; i < 256; ++i) {
        snap.blocks[i].lastReadTime  = minTime;
        snap.blocks[i].lastWriteTime = minTime;
    }

    for (int addr = 0; addr < 65536; ++addr) {
        int block = addr >> 8;  // addr / 256

        TimePoint rt = impl_->memStats[addr].lastReadTime;
        TimePoint wt = impl_->memStats[addr].lastWriteTime;

        if (rt > snap.blocks[block].lastReadTime)
            snap.blocks[block].lastReadTime = rt;
        if (wt > snap.blocks[block].lastWriteTime)
            snap.blocks[block].lastWriteTime = wt;
    }

    return snap;
}

// ---------------------------------------------------------------------------
// Enhanced Vector Screen: Video mode snapshot
// ---------------------------------------------------------------------------

DebugBackend::VideoModeSnapshot DebugBackend::videoModeSnapshot() const
{
    VideoModeSnapshot snap;

    // Framebuffer dimensions
    snap.screenWidth  = 576;   // DEFAULT_SCREEN_WIDTH
    snap.screenHeight = 288;   // DEFAULT_SCREEN_HEIGHT

    // Read video mode from tracked IO port values
    {
        std::lock_guard<std::mutex> lock(ioRegMutex_);
        snap.mode512     = (ioPB_ & 0x10) != 0;
        snap.scrollValue  = ioPA_;
    }

    snap.visibleWidth  = snap.mode512 ? 512 : 256;
    snap.visibleHeight = 256;
    snap.pixelsPerByte = snap.mode512 ? 4 : 8;

    snap.borderLeft = (snap.screenWidth - snap.visibleWidth) / 2;
    snap.borderTop  = (snap.screenHeight - snap.visibleHeight) / 2;

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
// Debug trace formatting
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
        snprintf(buf, bufsize, "PC=%04X  %02X %02X%02X",
                 pc, opcode, operandBytes[1], operandBytes[0]);
    }
}

// ---------------------------------------------------------------------------
// Thread-safe command API (Stage 3.1 — for GUI)
// ---------------------------------------------------------------------------

void DebugBackend::requestStep()
{
    // Stage 5.3.3: Always go through command queue / executeCommand.
    // No direct stepInstruction() from calling thread.
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::Step;
    submitAndWait(std::move(cmd));
}

void DebugBackend::requestRun()
{
    // Stage 5.3.3.2: Always through Command Queue.
    // skipBreakpoint_ is computed by emulation thread in executeFramesTarget_().
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::Run;
    submitAndWait(std::move(cmd));
}

// Stage 5.3.3.3: requestRun() variant that returns a future fulfilled
// when the Emulation Thread next transitions to Paused (breakpoint/pause hit).
// The pause promise belongs to this specific Run command — no global promise.
std::future<CommandResult> DebugBackend::requestRunFuture()
{
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::Run;

    // Stage 5.3.3.2: Test-only synchronous fallback
    if (testSynchronous_ && !emulationLoopRunning_) {
        // No emulation thread — return a ready future immediately.
        // No pausePromise needed (no emulation to wait for).
        auto cmdFuture = cmd->promise.get_future();
        cmd->state.store(CommandState::Executing, std::memory_order_release);
        executeCommand(*cmd);
        return cmdFuture;
    }

    // Stage 5.3.3.3: Per-command pause promise (production only)
    auto pausePromise = std::make_shared<std::promise<CommandResult>>();
    auto pauseFuture = pausePromise->get_future();
    cmd->pausePromise = pausePromise;

    // Production: enqueue and wait for command processing
    auto cmdFuture = cmd->promise.get_future();
    commandQueue_.enqueue(std::move(cmd));
    cmdFuture.wait();

    // Return future that completes when emulation pauses
    return pauseFuture;
}

void DebugBackend::requestPause()
{
    // Stage 5.3.3.2: Pure atomic signal — no state mutation from caller thread.
    // Emulation Thread performs the actual Running→Paused transition.
    pauseRequestedAtomic_.store(true, std::memory_order_release);
    breakRequested_.store(true, std::memory_order_release);
}

void DebugBackend::requestReset()
{
    // Stage 5.3.3.2: No running_ mutation — only enqueue Reset command.
    // Emulation Thread performs the actual state transition.
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::Reset;
    submitAndWait(std::move(cmd));
}

void DebugBackend::waitForCompletion()
{
    // Legacy method — kept for backward compatibility.
    // With Command Queue, callers use submitAndWait() futures instead.
}

void DebugBackend::requestQuit()
{
    // Stage 5.3.3.1: Always enqueue Quit command + set atomic signal.
    quitRequested_.store(true, std::memory_order_release);
    auto cmd = std::make_unique<Command>();
    cmd->type = CommandType::Quit;
    commandQueue_.enqueue(std::move(cmd));
}

bool DebugBackend::isQuitRequested() const
{
    return quitRequested_.load(std::memory_order_acquire);
}

// Stage 5.3.3.3: fulfillPausePromises_ — fulfill all pending Run pause promises.
// Called when the Emulation Thread transitions to Paused.
void DebugBackend::fulfillPausePromises_()
{
    std::vector<std::shared_ptr<std::promise<CommandResult>>> toFulfill;
    {
        std::lock_guard<std::mutex> lk(pendingPauseMutex_);
        toFulfill.swap(pendingPausePromises_);
    }
    CommandResult r;
    r.success = true;
    r.status = CommandResult::Completed;
    for (auto &p : toFulfill) {
        p->set_value(r);
    }
}

// Stage 5.3.3.3: Unified command processing — single CAS-based path.
// Called from both runUntilPause() (main loop) and executeFramesTarget_() (frame loop).
// Returns true if the frame loop should continue, false if it should break.
bool DebugBackend::processCommand(std::unique_ptr<Command> &cmd)
{
    // Stage 5.3.3.2: CAS Queued→Executing — mutually exclusive with Queued→Cancelled
    CommandState expected = CommandState::Queued;
    if (!cmd->state.compare_exchange_strong(expected, CommandState::Executing,
                                            std::memory_order_acq_rel)) {
        // CAS failed — command was cancelled (Queued→Cancelled)
        cmd->promise.set_value(CommandResult{false, "cancelled", CommandResult::Cancelled});
        return true;
    }

    // Every command must receive a result, even on quit.
    if (quitRequested_.load(std::memory_order_acquire) &&
        cmd->type != CommandType::Quit) {
        cmd->state.store(CommandState::Completed, std::memory_order_release);
        cmd->promise.set_value(CommandResult{false, "quit requested", CommandResult::Cancelled});
        return false;
    }

    // Check cancellation flag (redundant with CAS above, but kept for safety)
    if (cmd->cancelled.load(std::memory_order_acquire)) {
        cmd->state.store(CommandState::Completed, std::memory_order_release);
        cmd->promise.set_value(CommandResult{false, "cancelled", CommandResult::Cancelled});
        return true;
    }

    // Pause before trace execution — trace needs CPU in Paused state
    if (cmd->type == CommandType::ExecuteTrace) {
        running_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_ = DebuggerState::Paused;
            pauseRequestedAtomic_.store(false, std::memory_order_release);
        }
    }

    // Handle Quit specially: set flags and transition to Paused
    if (cmd->type == CommandType::Quit) {
        quitRequested_.store(true, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            state_ = DebuggerState::Paused;
        }
        cmd->state.store(CommandState::Completed, std::memory_order_release);
        cmd->promise.set_value(CommandResult{true, "", CommandResult::Completed});
        fulfillPausePromises_();
        return false;
    }

    // Handle Pause specially: transition to Paused
    if (cmd->type == CommandType::Pause) {
        running_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(stateMutex_);
            state_ = DebuggerState::Paused;
            stopReason_ = StopReason::UserPause;
            pauseRequestedAtomic_.store(false, std::memory_order_release);
        }
        cmd->state.store(CommandState::Completed, std::memory_order_release);
        cmd->promise.set_value(CommandResult{true, "", CommandResult::Completed});
        fulfillPausePromises_();
        return false;
    }

    executeCommand(*cmd);
    // Note: executeCommand() sets cmd->promise internally

    // Mark command as completed after execution
    cmd->state.store(CommandState::Completed, std::memory_order_release);

    // After any command, check if state is now Paused — fulfill pending promises
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        if (state_ == DebuggerState::Paused) {
            fulfillPausePromises_();
        }
    }

    return true;
}

// Note: processOneCommand() was replaced by processCommand() in Stage 5.3.3.3.
// The emulation main loop now uses processCommand() directly.

// ---------------------------------------------------------------------------
// executeFramesTarget_ — run frames until pause/breakpoint via IDebugTarget
// ---------------------------------------------------------------------------

void DebugBackend::executeFramesTarget_()
{
    syncBreakpointsToTarget();

    if (skipBreakpoint_ && checkBreakpoint()) {
        skipBreakpoint_ = false;
        stepInstruction();
    }

    // Reset frame pacing timer — avoids a huge delta after pause/resume
    bool paceFrames = target_->framePacingEnabled();
    if (paceFrames) {
        lastFrameTime_ = Clock::now();
    }

    while (running_.load(std::memory_order_acquire)) {
        if (quitRequested_.load(std::memory_order_acquire)) {
            target_->debuggerBreak();
            target_->debuggerDetached();
            return;
        }

        target_->debuggerContinue();
        uint16_t pcBeforeFrame = target_->getCpuState().pc;
        target_->executeFrame();
        uint16_t pcAfterFrame = target_->getCpuState().pc;

        // -- Frame pacing: sleep to maintain 50 Hz (20 ms per frame) --------
        if (paceFrames) {
            auto now = Clock::now();
            auto elapsed = now - lastFrameTime_;
            if (elapsed < kFrameDuration) {
                std::this_thread::sleep_for(kFrameDuration - elapsed);
            }
            lastFrameTime_ = Clock::now();
        }

        // Stage 5.3.3.3: Process commands through unified path
        while (auto cmd = commandQueue_.tryDequeue()) {
            if (!processCommand(cmd)) break;
        }

        // If PC didn't change, target stopped (breakpoint or debugger_interrupt)
        if (pcAfterFrame == pcBeforeFrame && !quitRequested_.load(std::memory_order_acquire)) {
            if (checkBreakpoint()) {
                stopReason_ = StopReason::Breakpoint;
                running_.store(false, std::memory_order_release);
                break;
            }
        }

        if (pauseRequestedAtomic_.load(std::memory_order_acquire) || !running_.load(std::memory_order_acquire)) {
            running_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(stateMutex_);
                stopReason_ = StopReason::UserPause;
                state_ = DebuggerState::Paused;
                pauseRequestedAtomic_.store(false, std::memory_order_release);
            }
            break;
        }

        if (checkBreakpoint()) {
            stopReason_ = StopReason::Breakpoint;
            running_.store(false, std::memory_order_release);
            break;
        }
    }

    // Stage 5.3.3.3: Frame loop ended — fulfill any pending Run pause promises
    {
        std::lock_guard<std::mutex> lk(stateMutex_);
        if (state_ != DebuggerState::Paused) {
            state_ = DebuggerState::Paused;
        }
    }
    fulfillPausePromises_();
}

// ---------------------------------------------------------------------------
// executeFramesNoTarget_ — fallback (should not normally be called)
// ---------------------------------------------------------------------------

void DebugBackend::executeFramesNoTarget_()
{
    while (running_.load(std::memory_order_acquire)) {
        if (quitRequested_.load(std::memory_order_acquire)) return;
        if (checkBreakpoint()) {
            if (skipBreakpoint_) {
                skipBreakpoint_ = false;
                stepInstruction();
                continue;
            }
            stopReason_ = StopReason::Breakpoint;
            running_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(stateMutex_);
                state_ = DebuggerState::Paused;
            }
            break;
        }
        if (pauseRequestedAtomic_.load(std::memory_order_acquire) || !running_.load(std::memory_order_acquire)) {
            running_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(stateMutex_);
                stopReason_ = StopReason::UserPause;
                state_ = DebuggerState::Paused;
                pauseRequestedAtomic_.store(false, std::memory_order_release);
            }
            break;
        }
        stepInstruction();
    }
}

void DebugBackend::runUntilPause()
{
    if (!target_) return;

    emulationLoopRunning_ = true;

    // Set up poll_debugger callback — checks flags set by other threads
    target_->setPollCallback([this]() {
        if (quitRequested_.load(std::memory_order_acquire)) {
            target_->debuggerBreak();
            return;
        }

        if (breakRequested_.load(std::memory_order_acquire)) {
            breakRequested_.store(false, std::memory_order_release);
            target_->debuggerBreak();
            return;
        }

        if (pauseRequestedAtomic_.load(std::memory_order_acquire) || !running_.load(std::memory_order_relaxed)) {
            pauseRequestedAtomic_.store(true, std::memory_order_release);
            target_->debuggerBreak();
            return;
        }
    });

    // Enable debugging mode
    target_->debuggerAttached();

    // Main emulation loop
    while (true) {
        // Stage 5.3.3.3: Check running_ BEFORE blocking on waitAndDequeue().
        // This handles test-mode where requestRun() executed synchronously
        // and already set running_=true before runUntilPause() was called.
        if (running_.load(std::memory_order_acquire)) {
            executeFramesTarget_();
            // executeFramesTarget_() already set Paused and
            // fulfilled pending pause promises before returning.
        }

        if (quitRequested_.load(std::memory_order_acquire)) {
            // Stage 5.3.3: Drain remaining commands — fulfill all promises
            while (auto pending = commandQueue_.tryDequeue()) {
                // Stage 5.3.3.2: CAS to Cancelled — only if still Queued
                CommandState expected = CommandState::Queued;
                if (pending->state.compare_exchange_strong(expected, CommandState::Cancelled,
                                                          std::memory_order_acq_rel)) {
                    // Stage 5.3.3.3: Also fulfill per-command pause promise if present
                    if (pending->pausePromise) {
                        CommandResult pr;
                        pr.success = false;
                        pr.error = "quit";
                        pr.status = CommandResult::Cancelled;
                        pending->pausePromise->set_value(pr);
                    }
                    pending->promise.set_value(
                        CommandResult{false, "quit", CommandResult::Cancelled});
                }
            }
            // Stage 5.3.3.3: processCommand(Quit) already set Paused and
            // fulfilled pending promises.  Ensure state is consistent.
            {
                std::lock_guard<std::mutex> lk(stateMutex_);
                state_ = DebuggerState::Paused;
            }
            fulfillPausePromises_();
            target_->debuggerDetached();
            emulationLoopRunning_ = false;
            return;
        }

        // Wait for a command from the queue (blocks until available)
        auto cmd = commandQueue_.waitAndDequeue();
        processCommand(cmd);
        // Loop back — if processCommand set running_ (Run command),
        // executeFramesTarget_() will be called at the top of the loop.
    }
}
