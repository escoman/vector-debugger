#include "agent_api.h"
#include "disassembler.h"
#include "opcode_info.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------------------------
// AgentApi implementation — Stage 5.3
//
// All operations delegate to IDebugBackend.  No direct access to Board,
// Memory, CPU, IO, TV, or any emulator internals.
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
// Breakpoints
// ---------------------------------------------------------------------------

int AgentApi::setBreakpoint(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    int id = backend_.addBreakpoint(address);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address;
    log_.record("setBreakpoint", oss.str(),
                id >= 0 ? ("id=" + std::to_string(id)) : "duplicate",
                elapsedMs(t0));
    return id;
}

bool AgentApi::clearBreakpoint(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    bool ok = backend_.removeBreakpoint(address);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address;
    log_.record("clearBreakpoint", oss.str(), ok ? "removed" : "not found",
                elapsedMs(t0));
    return ok;
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
// Trace / I/O / VRAM
// ---------------------------------------------------------------------------

std::vector<InstructionEvent> AgentApi::getExecutionTrace(size_t maxEntries)
{
    auto t0 = std::chrono::steady_clock::now();
    auto all = backend_.instructionHistorySnapshot();

    // Return the last maxEntries entries
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

IDebugBackend::ActivitySnapshot AgentApi::getVramActivity()
{
    auto t0 = std::chrono::steady_clock::now();
    auto snap = backend_.activitySnapshot();
    log_.record("getVramActivity", "", "snapshot", elapsedMs(t0));
    return snap;
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
// Annotations — delegate to SymbolDatabase
// ---------------------------------------------------------------------------

bool AgentApi::createFunction(uint16_t address, uint16_t /*size*/)
{
    auto t0 = std::chrono::steady_clock::now();
    auto &db = backend_.symbolDatabase();
    std::string name = SymbolDatabase::autoName(address);
    bool ok = db.addSymbol(address, name, SymbolType::Function);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " name=" << name;
    log_.record("createFunction", oss.str(), ok ? "created" : "exists",
                elapsedMs(t0));
    return ok;
}

bool AgentApi::renameFunction(uint16_t address, const std::string &name)
{
    auto t0 = std::chrono::steady_clock::now();
    bool ok = backend_.symbolDatabase().renameSymbol(address, name);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " name=" << name;
    log_.record("renameFunction", oss.str(), ok ? "renamed" : "not found",
                elapsedMs(t0));
    return ok;
}

bool AgentApi::setFunctionComment(uint16_t address, const std::string &comment)
{
    auto t0 = std::chrono::steady_clock::now();
    bool ok = backend_.symbolDatabase().setComment(address, comment);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address;
    log_.record("setFunctionComment", oss.str(),
                ok ? "ok" : "symbol not found", elapsedMs(t0));
    return ok;
}

bool AgentApi::deleteFunction(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    bool ok = backend_.symbolDatabase().removeSymbol(address);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address;
    log_.record("deleteFunction", oss.str(), ok ? "deleted" : "not found",
                elapsedMs(t0));
    return ok;
}

bool AgentApi::addLabel(uint16_t address, const std::string &name)
{
    auto t0 = std::chrono::steady_clock::now();
    bool ok = backend_.symbolDatabase().addSymbol(address, name, SymbolType::Label);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " name=" << name;
    log_.record("addLabel", oss.str(), ok ? "created" : "exists",
                elapsedMs(t0));
    return ok;
}

bool AgentApi::setComment(uint16_t address, const std::string &comment)
{
    // setComment works like setFunctionComment — delegates to SymbolDatabase
    return setFunctionComment(address, comment);
}

bool AgentApi::applyAnnotation(const Annotation &annotation)
{
    auto t0 = std::chrono::steady_clock::now();
    bool ok = false;

    switch (annotation.type) {
    case Annotation::Function:
        ok = createFunction(annotation.address);
        if (!annotation.name.empty()) {
            renameFunction(annotation.address, annotation.name);
        }
        if (!annotation.comment.empty()) {
            setFunctionComment(annotation.address, annotation.comment);
        }
        ok = true;
        break;

    case Annotation::Label:
        ok = addLabel(annotation.address, annotation.name);
        break;

    case Annotation::Comment:
        ok = setComment(annotation.address, annotation.comment);
        break;

    case Annotation::Rename:
        ok = renameFunction(annotation.address, annotation.name);
        break;
    }

    std::ostringstream oss;
    oss << "type=" << static_cast<int>(annotation.type)
        << " addr=" << std::hex << annotation.address
        << " confidence=" << std::fixed << std::setprecision(2)
        << annotation.confidence;
    log_.record("applyAnnotation", oss.str(), ok ? "applied" : "failed",
                elapsedMs(t0));
    return ok;
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
// Internal: analyzeStackBalance
//
// Scan instruction history for entries within [funcStart, funcEnd).
// Compare SP at function entry vs SP at RET/exit.
// ---------------------------------------------------------------------------

FunctionContext::StackBehavior
AgentApi::analyzeStackBalance(uint16_t funcStart, uint16_t funcEnd)
{
    auto trace = backend_.instructionHistorySnapshot();

    uint16_t spEntry = 0;
    bool foundEntry = false;
    bool foundExit = false;
    bool balanced = true;

    for (const auto &ev : trace) {
        // Look for instructions within the function range
        if (ev.pcBefore < funcStart || ev.pcBefore >= funcEnd) continue;

        // Check if this is a CALL to the function (entry point)
        if (ev.pcBefore == funcStart && !foundEntry) {
            spEntry = ev.before.sp;
            foundEntry = true;
        }

        // Check for RET instruction within the function
        if (ev.opcode == 0xC9) {  // RET
            foundExit = true;
            if (foundEntry && ev.after.sp != spEntry) {
                balanced = false;
            }
        }
    }

    if (!foundEntry || !foundExit) {
        return FunctionContext::Unknown;
    }
    return balanced ? FunctionContext::Balanced : FunctionContext::Unbalanced;
}

// ---------------------------------------------------------------------------
// Internal: collectMemoryAccess
// ---------------------------------------------------------------------------

void AgentApi::collectMemoryAccess(
    uint16_t start, uint16_t end,
    const IDebugBackend::ActivitySnapshot &activity,
    std::vector<FunctionContext::MemoryAccess> &reads,
    std::vector<FunctionContext::MemoryAccess> &writes)
{
    // activity vectors are 65536 entries (per-address)
    for (uint32_t addr = start; addr <= end && addr < 0x10000; ++addr) {
        if (addr < activity.readCount.size() && activity.readCount[addr] > 0) {
            reads.push_back({static_cast<uint16_t>(addr), activity.readCount[addr]});
        }
        if (addr < activity.writeCount.size() && activity.writeCount[addr] > 0) {
            writes.push_back({static_cast<uint16_t>(addr), activity.writeCount[addr]});
        }
    }
}

// ---------------------------------------------------------------------------
// Internal: collectIoAccess
// ---------------------------------------------------------------------------

void AgentApi::collectIoAccess(
    uint16_t funcStart, uint16_t funcEnd,
    const std::vector<IoAccessEvent> &ioHistory,
    std::vector<FunctionContext::IoAccess> &ioAccesses)
{
    // Count I/O accesses that occurred while executing within the function.
    // We approximate by checking if the instruction sequence falls within
    // the range of sequences where PC was in the function.
    //
    // Simpler approach: collect all unique ports accessed, with counts.
    // (A more precise version would cross-reference instruction sequences.)

    std::map<uint16_t, FunctionContext::IoAccess> portMap;

    for (const auto &ev : ioHistory) {
        uint16_t key = (static_cast<uint16_t>(ev.port) << 8) |
                       (ev.type == IoAccessType::Out ? 1 : 0);
        auto &entry = portMap[key];
        entry.port = ev.port;
        entry.isOutput = (ev.type == IoAccessType::Out);
        entry.count++;
    }

    for (auto &kv : portMap) {
        ioAccesses.push_back(kv.second);
    }
}

// ---------------------------------------------------------------------------
// Internal: collectVramWrites
// ---------------------------------------------------------------------------

void AgentApi::collectVramWrites(
    const IDebugBackend::ActivitySnapshot &activity,
    std::vector<FunctionContext::VramWrite> &vramWrites)
{
    // VRAM is at 0xC000 - 0xC0FF (256 bytes)
    for (uint32_t addr = 0xC000; addr <= 0xC0FF; ++addr) {
        if (addr < activity.writeCount.size() && activity.writeCount[addr] > 0) {
            vramWrites.push_back({
                static_cast<uint16_t>(addr),
                activity.writeCount[addr]
            });
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

    // 1. Name and comment from SymbolDatabase
    const auto &db = backend_.symbolDatabase();
    const DebugSymbol *sym = db.findSymbol(address);
    if (sym) {
        ctx.name = sym->name;
        ctx.comment = sym->comment;
    } else {
        ctx.name = SymbolDatabase::autoName(address);
    }

    // 2. Disassemble function
    ctx.instructions = disassembleFunction(address);
    if (!ctx.instructions.empty()) {
        auto &last = ctx.instructions.back();
        ctx.size = static_cast<uint16_t>(
            (last.address + last.length) - address);
    }

    // 3. Callers — xrefs TO this address (filter for CALL instructions)
    auto xrefs = db.xrefsTo(address);
    for (const auto &xr : xrefs) {
        // Check if the xref source is a CALL instruction
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

    // 5. Memory access from activity snapshot
    uint16_t funcEnd = address + ctx.size;
    auto activity = backend_.activitySnapshot();
    collectMemoryAccess(address, funcEnd, activity,
                        ctx.memoryReads, ctx.memoryWrites);

    // 6. I/O accesses
    auto ioHistory = backend_.ioHistorySnapshot();
    collectIoAccess(address, funcEnd, ioHistory, ctx.ioAccesses);

    // 7. VRAM writes
    collectVramWrites(activity, ctx.vramWrites);

    // 8. Stack behavior
    ctx.stackBehavior = analyzeStackBalance(address, funcEnd);

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
// High-level: traceFunction (simplified — analyzes existing trace history)
// ---------------------------------------------------------------------------

TraceResult AgentApi::traceFunction(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    TraceResult result;
    result.entryPc = address;

    // Determine function range
    uint16_t funcSize = estimateFunctionSize(address);
    uint16_t funcEnd = address + funcSize;

    // Analyze existing instruction history
    auto trace = backend_.instructionHistorySnapshot();

    std::set<uint16_t> calledSet;
    std::map<uint16_t, uint64_t> readMap;
    std::map<uint16_t, uint64_t> writeMap;
    uint64_t execCount = 0;

    for (const auto &ev : trace) {
        // Count executions at function entry
        if (ev.pcBefore == address) {
            execCount++;
            if (result.spEntry == 0) {
                result.spEntry = ev.before.sp;
            }
        }

        // Instructions within the function range
        if (ev.pcBefore >= address && ev.pcBefore < funcEnd) {
            // Track SP at exit (RET)
            if (ev.opcode == 0xC9) {  // RET
                result.exitPc = ev.pcBefore;
                result.spExit = ev.after.sp;
            }

            // Track CALL targets
            uint8_t opcode = ev.opcode;
            bool isCall = (opcode == 0xCD ||
                           opcode == 0xC4 || opcode == 0xCC ||
                           opcode == 0xD4 || opcode == 0xDC ||
                           opcode == 0xE4 || opcode == 0xEC ||
                           opcode == 0xF4 || opcode == 0xFC);
            if (isCall && ev.length == 3) {
                uint16_t target = static_cast<uint16_t>(
                    ev.operandBytes[0] | (ev.operandBytes[1] << 8));
                calledSet.insert(target);
            }
        }
    }

    result.executionCount = execCount;
    result.calledFunctions.assign(calledSet.begin(), calledSet.end());

    // Memory access from activity snapshot
    auto activity = backend_.activitySnapshot();
    collectMemoryAccess(address, funcEnd, activity,
                        result.memoryReads, result.memoryWrites);

    // I/O accesses
    auto ioHistory = backend_.ioHistorySnapshot();
    collectIoAccess(address, funcEnd, ioHistory, result.ioAccesses);

    // VRAM writes
    collectVramWrites(activity, result.vramWrites);

    std::ostringstream oss;
    oss << "addr=" << std::hex << address
        << " execCount=" << std::dec << execCount
        << " callees=" << result.calledFunctions.size();
    log_.record("traceFunction", oss.str(), "done", elapsedMs(t0));

    return result;
}
