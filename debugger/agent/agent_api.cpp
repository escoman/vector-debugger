#include "agent_api.h"
#include "disassembler.h"
#include "opcode_info.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <thread>

// ---------------------------------------------------------------------------
// AgentApi implementation — Stage 5.3.1
//
// All operations delegate to IDebugBackend.  No direct access to Board,
// Memory, CPU, IO, TV, or any emulator internals.
//
// State-changing operations go through the Backend command protocol.
// ---------------------------------------------------------------------------

AgentApi::AgentApi(IDebugBackend &backend)
    : backend_(backend)
{
}

// ---------------------------------------------------------------------------
// Timer helper
// ---------------------------------------------------------------------------

double AgentApi::elapsedMs(std::chrono::steady_clock::time_point start)
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - start).count();
}

// ---------------------------------------------------------------------------
// Execution control
// ---------------------------------------------------------------------------

void AgentApi::run()
{
    auto t0 = std::chrono::steady_clock::now();
    backend_.requestRun();
    log_.record("run", "", backend_.isPaused() ? "paused" : "running",
                elapsedMs(t0));
}

void AgentApi::pause()
{
    auto t0 = std::chrono::steady_clock::now();
    backend_.requestPause();
    log_.record("pause", "",
                backend_.isPaused() ? "paused" : "still running",
                elapsedMs(t0));
}

void AgentApi::step()
{
    auto t0 = std::chrono::steady_clock::now();
    auto cpuBefore = backend_.getCpuState();
    backend_.stepInstruction();
    auto cpuAfter = backend_.getCpuState();

    std::ostringstream oss;
    oss << "PC: " << std::hex << cpuBefore.pc << " -> " << cpuAfter.pc;
    log_.record("step", "", oss.str(), elapsedMs(t0));
}

void AgentApi::reset()
{
    auto t0 = std::chrono::steady_clock::now();
    backend_.requestReset();
    log_.record("reset", "", "done", elapsedMs(t0));
}

// ---------------------------------------------------------------------------
// CPU state
// ---------------------------------------------------------------------------

CpuState AgentApi::getCpuState()
{
    auto t0 = std::chrono::steady_clock::now();
    CpuState cpu = backend_.getCpuState();

    std::ostringstream oss;
    oss << "PC=" << std::hex << cpu.pc << " SP=" << cpu.sp
        << " A=" << (int)cpu.a;
    log_.record("getCpuState", "", oss.str(), elapsedMs(t0));
    return cpu;
}

// ---------------------------------------------------------------------------
// Memory access
// ---------------------------------------------------------------------------

std::vector<uint8_t> AgentApi::readMemory(uint16_t address, size_t size)
{
    auto t0 = std::chrono::steady_clock::now();
    auto snap = backend_.readMemorySnapshot(address, size);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " size=" << std::dec << size;
    log_.record("readMemory", oss.str(),
                std::to_string(snap.data.size()) + " bytes",
                elapsedMs(t0));
    return snap.data;
}

bool AgentApi::writeMemory(uint16_t address, const std::vector<uint8_t> &data)
{
    auto t0 = std::chrono::steady_clock::now();
    bool ok = backend_.writeMemory(address, data.data(), data.size());

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " size=" << std::dec << data.size();
    log_.record("writeMemory", oss.str(), ok ? "ok" : "failed", elapsedMs(t0));
    return ok;
}

// ---------------------------------------------------------------------------
// Breakpoints (through command protocol)
// ---------------------------------------------------------------------------

CommandResult AgentApi::setBreakpoint(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = backend_.requestAddBreakpoint(address);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address;
    log_.record("setBreakpoint", oss.str(),
                result.success ? "ok" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

CommandResult AgentApi::clearBreakpoint(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = backend_.requestRemoveBreakpoint(address);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address;
    log_.record("clearBreakpoint", oss.str(),
                result.success ? "removed" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

CommandResult AgentApi::setBreakpointEnabled(uint16_t address, bool enabled)
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = backend_.requestSetBreakpointEnabled(address, enabled);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " enabled=" << enabled;
    log_.record("setBreakpointEnabled", oss.str(),
                result.success ? "ok" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

std::vector<DebuggerBreakpoint> AgentApi::listBreakpoints()
{
    auto t0 = std::chrono::steady_clock::now();
    auto bps = backend_.getBreakpoints();
    log_.record("listBreakpoints", "",
                std::to_string(bps.size()) + " breakpoints",
                elapsedMs(t0));
    return bps;
}

// ---------------------------------------------------------------------------
// Trace / I/O
// ---------------------------------------------------------------------------

std::vector<InstructionEvent> AgentApi::getExecutionTrace(size_t maxEntries)
{
    auto t0 = std::chrono::steady_clock::now();
    auto all = backend_.instructionHistorySnapshot();

    if (all.size() > maxEntries) {
        all.erase(all.begin(), all.end() - static_cast<ptrdiff_t>(maxEntries));
    }

    log_.record("getExecutionTrace",
                "max=" + std::to_string(maxEntries),
                std::to_string(all.size()) + " events",
                elapsedMs(t0));
    return all;
}

std::vector<IoAccessEvent> AgentApi::getIoTrace(size_t maxEntries)
{
    auto t0 = std::chrono::steady_clock::now();
    auto all = backend_.ioHistorySnapshot();

    if (all.size() > maxEntries) {
        all.erase(all.begin(), all.end() - static_cast<ptrdiff_t>(maxEntries));
    }

    log_.record("getIoTrace",
                "max=" + std::to_string(maxEntries),
                std::to_string(all.size()) + " events",
                elapsedMs(t0));
    return all;
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

IDebugBackend::ScreenSnapshot AgentApi::getScreen()
{
    auto t0 = std::chrono::steady_clock::now();
    auto snap = backend_.screenSnapshot();

    std::ostringstream oss;
    oss << snap.width << "x" << snap.height;
    log_.record("getScreen", "", oss.str(), elapsedMs(t0));
    return snap;
}

// ---------------------------------------------------------------------------
// Annotations — through Backend command protocol
// ---------------------------------------------------------------------------

CommandResult AgentApi::createFunction(uint16_t address, uint16_t /*size*/)
{
    auto t0 = std::chrono::steady_clock::now();
    std::string name = SymbolDatabase::autoName(address);
    auto result = backend_.requestCreateFunction(address, name);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " name=" << name;
    log_.record("createFunction", oss.str(),
                result.success ? "created" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

CommandResult AgentApi::renameFunction(uint16_t address, const std::string &name)
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = backend_.requestRenameSymbol(address, name);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " name=" << name;
    log_.record("renameFunction", oss.str(),
                result.success ? "renamed" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

CommandResult AgentApi::setFunctionComment(uint16_t address, const std::string &comment)
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = backend_.requestSetComment(address, comment);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address;
    log_.record("setFunctionComment", oss.str(),
                result.success ? "ok" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

CommandResult AgentApi::deleteFunction(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = backend_.requestRemoveSymbol(address);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address;
    log_.record("deleteFunction", oss.str(),
                result.success ? "deleted" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

CommandResult AgentApi::addLabel(uint16_t address, const std::string &name)
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = backend_.requestAddLabel(address, name);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " name=" << name;
    log_.record("addLabel", oss.str(),
                result.success ? "created" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

CommandResult AgentApi::setComment(uint16_t address, const std::string &comment)
{
    return setFunctionComment(address, comment);
}

CommandResult AgentApi::applyAnnotation(const Annotation &annotation)
{
    auto t0 = std::chrono::steady_clock::now();
    CommandResult result;

    switch (annotation.type) {
    case Annotation::Function:
        result = createFunction(annotation.address);
        if (result.success && !annotation.name.empty()) {
            result = renameFunction(annotation.address, annotation.name);
        }
        if (result.success && !annotation.comment.empty()) {
            result = setFunctionComment(annotation.address, annotation.comment);
        }
        break;

    case Annotation::Label:
        result = addLabel(annotation.address, annotation.name);
        break;

    case Annotation::Comment:
        result = setComment(annotation.address, annotation.comment);
        break;

    case Annotation::Rename:
        result = renameFunction(annotation.address, annotation.name);
        break;
    }

    std::ostringstream oss;
    oss << "type=" << static_cast<int>(annotation.type)
        << " addr=" << std::hex << annotation.address
        << " confidence=" << std::fixed << std::setprecision(2)
        << annotation.confidence;
    log_.record("applyAnnotation", oss.str(),
                result.success ? "applied" : result.error, elapsedMs(t0),
                result.success, result.error);
    return result;
}

// ---------------------------------------------------------------------------
// ROM
// ---------------------------------------------------------------------------

bool AgentApi::loadRom(const std::string &path, uint32_t org)
{
    auto t0 = std::chrono::steady_clock::now();
    bool ok = backend_.loadRom(path, org);
    log_.record("loadRom", path, ok ? "loaded" : "failed", elapsedMs(t0));
    return ok;
}

// ---------------------------------------------------------------------------
// Agent log
// ---------------------------------------------------------------------------

const AgentLog &AgentApi::log() const
{
    return log_;
}

void AgentApi::clearLog()
{
    log_.clear();
}

// ---------------------------------------------------------------------------
// Internal: disassembleFunction
//
// Linear disassembly starting at 'address' until:
//   - RET (0xC9) or HLT (0x76) instruction
//   - Unconditional JMP (0xC3) — code after is unreachable (Section 16)
//   - A known symbol is encountered (other than the start address)
//   - Maximum function size (4096 bytes) reached
// ---------------------------------------------------------------------------

std::vector<FunctionContext::Instruction>
AgentApi::disassembleFunction(uint16_t address)
{
    std::vector<FunctionContext::Instruction> result;

    auto readByte = [this](uint16_t addr) -> uint8_t {
        return backend_.readMemory(addr);
    };

    const auto &db = backend_.symbolDatabase();
    static const uint16_t MAX_FUNC_SIZE = 4096;

    uint16_t pc = address;
    for (uint16_t bytesDone = 0; bytesDone < MAX_FUNC_SIZE; ) {
        // Stop if we hit a known symbol (other than the entry point)
        if (pc != address && db.findSymbol(pc) != nullptr) {
            break;
        }

        DisassembledInstruction di = disassemble(pc, readByte);

        FunctionContext::Instruction fi;
        fi.address = di.address;
        fi.text = di.text;
        fi.length = di.length;
        for (int i = 0; i < 3; ++i) fi.bytes[i] = di.bytes[i];
        result.push_back(fi);

        bytesDone += di.length;

        // Stop after RET or HLT
        if (di.opcode == 0xC9 || di.opcode == 0x76) {
            break;
        }

        // Stop after unconditional JMP (0xC3) — code after is unreachable
        if (di.opcode == 0xC3) {
            break;
        }

        // Advance PC
        uint16_t nextPc = pc + di.length;
        if (nextPc <= pc) break;  // overflow guard
        pc = nextPc;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Internal: estimateFunctionSize
// ---------------------------------------------------------------------------

uint16_t AgentApi::estimateFunctionSize(uint16_t address)
{
    auto instrs = disassembleFunction(address);
    if (instrs.empty()) return 0;

    auto &last = instrs.back();
    return static_cast<uint16_t>(
        (last.address + last.length) - address);
}

// ---------------------------------------------------------------------------
// Internal: collectTraceEvents
//
// Collect memory/IO/VRAM events from history that fall within the
// instruction sequence range [startSeq, endSeq).
//
// Memory events have instructionSequence — the sequence number of the
// instruction that caused the access.  IO events similarly.
//
// For VRAM: writes to 0xC000-0xC0FF are memory writes, so they appear
// in memHistory.  We filter them out separately.
// ---------------------------------------------------------------------------

void AgentApi::collectTraceEvents(
    uint64_t startSeq, uint64_t endSeq,
    std::vector<TraceMemoryAccess> &memReads,
    std::vector<TraceMemoryAccess> &memWrites,
    std::vector<TraceIoAccess> &ioReads,
    std::vector<TraceIoAccess> &ioWrites,
    std::vector<TraceVramWrite> &vramWrites)
{
    // --- Memory events ---
    auto memHistory = backend_.memoryHistorySnapshot();
    for (const auto &ev : memHistory) {
        if (ev.instructionSequence < startSeq || ev.instructionSequence >= endSeq)
            continue;

        // We need the PC of the instruction that caused this access.
        // The instruction history has pcBefore for each sequence number.
        // For efficiency, we look it up from the instruction trace.
        // But we don't have a direct mapping here — we'll use the
        // instruction history to find the PC.
        //
        // Actually, MemoryAccessEvent doesn't have PC directly.
        // The PC is the instruction at instructionSequence.
        // We'll find it from instructionHistorySnapshot.
        // (This is done in traceFunction where we have the instr trace.)
        //
        // For now, record with address and type.  PC attribution
        // requires cross-referencing with instruction history.

        if (ev.type == MemoryAccessType::Write) {
            // Check if VRAM (0xC000 - 0xC0FF)
            if (ev.virt >= 0xC000 && ev.virt < 0xC100) {
                TraceVramWrite vw;
                vw.address = ev.virt;
                vw.value = ev.value;
                // PC will be filled by the caller
                vramWrites.push_back(vw);
            } else {
                TraceMemoryAccess ma;
                ma.address = ev.virt;
                ma.type = TraceMemoryAccess::Write;
                ma.value = ev.value;
                memWrites.push_back(ma);
            }
        } else if (ev.type == MemoryAccessType::Read) {
            TraceMemoryAccess ma;
            ma.address = ev.virt;
            ma.type = TraceMemoryAccess::Read;
            ma.value = ev.value;
            memReads.push_back(ma);
        }
        // Skip Fetch events (opcode fetches are not data accesses)
    }

    // --- IO events ---
    auto ioHistory = backend_.ioHistorySnapshot();
    for (const auto &ev : ioHistory) {
        if (ev.instructionSequence < startSeq || ev.instructionSequence >= endSeq)
            continue;

        TraceIoAccess io;
        io.port = ev.port;
        io.isOutput = (ev.type == IoAccessType::Out);
        io.value = ev.value;

        if (ev.type == IoAccessType::Out) {
            ioWrites.push_back(io);
        } else {
            ioReads.push_back(io);
        }
    }
}

// ---------------------------------------------------------------------------
// High-level: getFunctionContext
// ---------------------------------------------------------------------------

FunctionContext AgentApi::getFunctionContext(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    FunctionContext ctx;
    ctx.address = address;

    // 1. Name and comment from SymbolDatabase (read-only)
    const auto &db = backend_.symbolDatabase();
    const DebugSymbol *sym = db.findSymbol(address);
    if (sym) {
        ctx.name = sym->name;
        ctx.comment = sym->comment;
    } else {
        ctx.name = SymbolDatabase::autoName(address);
    }

    // 2. Disassemble function (static — from Disassembler)
    ctx.instructions = disassembleFunction(address);
    if (!ctx.instructions.empty()) {
        auto &last = ctx.instructions.back();
        ctx.size = static_cast<uint16_t>(
            (last.address + last.length) - address);
    }

    // 3. Callers — xrefs TO this address (filter for CALL instructions)
    auto xrefs = db.xrefsTo(address);
    for (const auto &xr : xrefs) {
        uint8_t opcode = backend_.readMemory(xr.from);
        bool isCall = (opcode == 0xCD ||  // CALL
                       opcode == 0xC4 || opcode == 0xCC ||
                       opcode == 0xD4 || opcode == 0xDC ||
                       opcode == 0xE4 || opcode == 0xEC ||
                       opcode == 0xF4 || opcode == 0xFC);
        if (isCall) {
            ctx.callers.push_back(xr.from);
        }
    }

    // 4. Callees — scan function instructions for CALL targets
    for (const auto &instr : ctx.instructions) {
        uint8_t opcode = instr.bytes[0];
        bool isCall = (opcode == 0xCD ||
                       opcode == 0xC4 || opcode == 0xCC ||
                       opcode == 0xD4 || opcode == 0xDC ||
                       opcode == 0xE4 || opcode == 0xEC ||
                       opcode == 0xF4 || opcode == 0xFC);
        if (isCall && instr.length == 3) {
            uint16_t target = static_cast<uint16_t>(
                instr.bytes[1] | (instr.bytes[2] << 8));
            ctx.callees.push_back(target);
        }
        // RST instructions
        bool isRst = (opcode == 0xC7 || opcode == 0xCF ||
                      opcode == 0xD7 || opcode == 0xDF ||
                      opcode == 0xE7 || opcode == 0xEF ||
                      opcode == 0xF7 || opcode == 0xFF);
        if (isRst) {
            uint16_t target = static_cast<uint16_t>(opcode & 0x38);
            ctx.callees.push_back(target);
        }
    }

    // 5. Dynamic data: no trace available by default
    //    Memory/IO/VRAM fields remain empty with DataSource::Unknown.
    //    If a trace was previously run, the caller can populate these
    //    from the TraceResult.
    ctx.memorySource = DataSource::Unknown;
    ctx.ioSource = DataSource::Unknown;
    ctx.vramSource = DataSource::Unknown;
    ctx.stackBehavior = FunctionContext::Unknown;

    std::ostringstream oss;
    oss << "addr=" << std::hex << address
        << " size=" << std::dec << ctx.size
        << " instrs=" << ctx.instructions.size()
        << " callers=" << ctx.callers.size()
        << " callees=" << ctx.callees.size();
    log_.record("getFunctionContext", oss.str(), ctx.name, elapsedMs(t0));

    return ctx;
}

// ---------------------------------------------------------------------------
// High-level: traceFunction — REAL execution experiment (Section 8)
//
// 1. Set temporary breakpoint at function entry
// 2. Run until breakpoint hit (function entry)
// 3. Execute trace (step through function, collect events)
// 4. Remove temporary breakpoint
// 5. Build TraceResult with attributed events
// ---------------------------------------------------------------------------

TraceResult AgentApi::traceFunction(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    TraceResult result;
    result.entryPc = address;

    // 1. Set temporary breakpoint at function entry
    auto bpResult = backend_.requestAddBreakpoint(address);
    if (!bpResult.success) {
        // Breakpoint already exists — that's OK for tracing
        // (the function address already has a breakpoint)
    }

    // 2. Run until breakpoint hit
    backend_.requestRun();

    // Stage 5.3.3.1: Wait for backend to pause (breakpoint hit).
    // No sleep_for / busy-wait — use yield() to cooperate with scheduler.
    // In test scenarios (MockBackend), requestRun() is synchronous and
    // the backend is already paused when we reach this point.
    int waitLimit = 10000;
    while (!backend_.isPaused() && waitLimit-- > 0) {
        std::this_thread::yield();
    }

    if (!backend_.isPaused()) {
        // Failed to reach function entry
        backend_.requestRemoveBreakpoint(address);
        result.exitReason = ExitReason::Timeout;
        log_.record("traceFunction", "addr=" + std::to_string(address),
                     "failed to reach function entry", elapsedMs(t0));
        return result;
    }

    // 3. Execute trace — step through function collecting events
    IDebugBackend::TraceExecutionParams params;
    params.startPc = address;
    params.maxInstructions = 10000;
    params.stopOnRet = true;
    params.stopOnCallerReturn = false;

    auto traceExec = backend_.requestExecuteTrace(params).get();

    // 4. Remove temporary breakpoint
    backend_.requestRemoveBreakpoint(address);

    // 5. Build TraceResult from execution data
    result.exitPc = traceExec.exitPc;
    result.entrySp = traceExec.entrySp;
    result.exitSp = traceExec.exitSp;
    result.minSp = traceExec.minSp;
    result.maxSp = traceExec.maxSp;
    result.instructionCount = traceExec.instructionsExecuted;
    result.executionCount = 1;  // single trace invocation
    result.exitReason = traceExec.exitReason;

    // 6. Collect events from history using sequence range
    auto instrTrace = backend_.instructionHistorySnapshot();
    auto memHistory = backend_.memoryHistorySnapshot();
    auto ioHistory = backend_.ioHistorySnapshot();

    // Build PC lookup: sequence → pcBefore
    std::map<uint64_t, uint16_t> seqToPc;
    for (const auto &ie : instrTrace) {
        if (ie.sequence >= traceExec.startSequence &&
            ie.sequence < traceExec.endSequence) {
            seqToPc[ie.sequence] = ie.pcBefore;
            result.executedPcs.push_back(ie.pcBefore);
        }
    }

    // Memory events with PC attribution
    for (const auto &ev : memHistory) {
        if (ev.instructionSequence < traceExec.startSequence ||
            ev.instructionSequence >= traceExec.endSequence)
            continue;

        // Stage 5.3.3: proper hasPc tracking — PC=0000 is not Unknown
        bool hasPc = false;
        uint16_t pc = 0;
        auto it = seqToPc.find(ev.instructionSequence);
        if (it != seqToPc.end()) { pc = it->second; hasPc = true; }

        if (ev.type == MemoryAccessType::Write) {
            if (ev.virt >= 0xC000 && ev.virt < 0xC100) {
                // VRAM write
                TraceVramWrite vw;
                vw.pc = pc;
                vw.hasPc = hasPc;
                vw.address = ev.virt;
                vw.value = ev.value;
                result.vramWrites.push_back(vw);
            } else {
                TraceMemoryAccess ma;
                ma.pc = pc;
                ma.hasPc = hasPc;
                ma.address = ev.virt;
                ma.type = TraceMemoryAccess::Write;
                ma.value = ev.value;
                result.memoryWrites.push_back(ma);
            }
        } else if (ev.type == MemoryAccessType::Read) {
            TraceMemoryAccess ma;
            ma.pc = pc;
            ma.hasPc = hasPc;
            ma.address = ev.virt;
            ma.type = TraceMemoryAccess::Read;
            ma.value = ev.value;
            result.memoryReads.push_back(ma);
        }
    }

    // IO events with PC attribution
    for (const auto &ev : ioHistory) {
        if (ev.instructionSequence < traceExec.startSequence ||
            ev.instructionSequence >= traceExec.endSequence)
            continue;

        // Stage 5.3.3: proper hasPc tracking
        bool hasPc = false;
        uint16_t pc = 0;
        auto it = seqToPc.find(ev.instructionSequence);
        if (it != seqToPc.end()) { pc = it->second; hasPc = true; }

        TraceIoAccess io;
        io.pc = pc;
        io.hasPc = hasPc;
        io.port = ev.port;
        io.isOutput = (ev.type == IoAccessType::Out);
        io.value = ev.value;

        if (ev.type == IoAccessType::Out) {
            result.ioWrites.push_back(io);
        } else {
            result.ioReads.push_back(io);
        }
    }

    // Extract called functions from instruction trace
    std::set<uint16_t> calledSet;
    for (const auto &ie : instrTrace) {
        if (ie.sequence < traceExec.startSequence ||
            ie.sequence >= traceExec.endSequence)
            continue;

        uint8_t opcode = ie.opcode;
        bool isCall = (opcode == 0xCD ||
                       opcode == 0xC4 || opcode == 0xCC ||
                       opcode == 0xD4 || opcode == 0xDC ||
                       opcode == 0xE4 || opcode == 0xEC ||
                       opcode == 0xF4 || opcode == 0xFC);
        if (isCall && ie.length == 3) {
            uint16_t target = static_cast<uint16_t>(
                ie.operandBytes[0] | (ie.operandBytes[1] << 8));
            calledSet.insert(target);
        }

        // RST instructions
        bool isRst = (opcode == 0xC7 || opcode == 0xCF ||
                      opcode == 0xD7 || opcode == 0xDF ||
                      opcode == 0xE7 || opcode == 0xEF ||
                      opcode == 0xF7 || opcode == 0xFF);
        if (isRst) {
            uint16_t target = static_cast<uint16_t>(opcode & 0x38);
            calledSet.insert(target);
        }
    }
    result.calledFunctions.assign(calledSet.begin(), calledSet.end());

    // Stack depth tracking
    result.callDepth = 0;
    int depth = 0;
    for (const auto &ie : instrTrace) {
        if (ie.sequence < traceExec.startSequence ||
            ie.sequence >= traceExec.endSequence)
            continue;

        uint8_t opcode = ie.opcode;
        bool isCall = (opcode == 0xCD ||
                       opcode == 0xC4 || opcode == 0xCC ||
                       opcode == 0xD4 || opcode == 0xDC ||
                       opcode == 0xE4 || opcode == 0xEC ||
                       opcode == 0xF4 || opcode == 0xFC);
        bool isRst = (opcode == 0xC7 || opcode == 0xCF ||
                      opcode == 0xD7 || opcode == 0xDF ||
                      opcode == 0xE7 || opcode == 0xEF ||
                      opcode == 0xF7 || opcode == 0xFF);
        if (isCall || isRst) depth++;
        if (opcode == 0xC9) depth--;  // RET
    }
    result.callDepth = depth;

    std::ostringstream oss;
    oss << "addr=" << std::hex << address
        << " instrs=" << std::dec << result.instructionCount
        << " exit=" << static_cast<int>(result.exitReason)
        << " callees=" << result.calledFunctions.size();
    log_.record("traceFunction", oss.str(), "done", elapsedMs(t0));

    return result;
}
