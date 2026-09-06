#include "agent_api.h"
#include "disassembler.h"
#include "opcode_info.h"
#include "rom_load_address.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <thread>

// ---------------------------------------------------------------------------
// AgentApi implementation — Stage 5.3.1 / Stage 6.1
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

bool AgentApi::isRunning() const
{
    return !backend_.isPaused();
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

AgentApiResult<void> AgentApi::clearAllBreakpoints()
{
    auto t0 = std::chrono::steady_clock::now();
    auto result = backend_.requestClearBreakpoints();

    log_.record("clearAllBreakpoints", "",
                result.success ? "cleared" : result.error,
                elapsedMs(t0), result.success, result.error);

    if (!result.success) {
        return AgentApiResult<void>::fail(
            ErrorCode::OperationFailed, result.error);
    }
    return AgentApiResult<void>::ok();
}

// ---------------------------------------------------------------------------
// Registers (Stage 6.1 §8)
// ---------------------------------------------------------------------------

AgentApiResult<void> AgentApi::setRegister(const std::string &name, uint16_t value)
{
    auto t0 = std::chrono::steady_clock::now();

    // Map register name → (RegisterId, high/low/whole)
    // Pairs AF, BC, DE, HL: high byte = first reg, low byte = second reg.
    struct RegMapping {
        IDebugBackend::RegisterId pair;
        enum { High, Low, Whole } half;
    };

    static const std::map<std::string, RegMapping> regMap = {
        {"A",  {IDebugBackend::RegisterId::AF, RegMapping::High}},
        {"F",  {IDebugBackend::RegisterId::AF, RegMapping::Low}},
        {"B",  {IDebugBackend::RegisterId::BC, RegMapping::High}},
        {"C",  {IDebugBackend::RegisterId::BC, RegMapping::Low}},
        {"D",  {IDebugBackend::RegisterId::DE, RegMapping::High}},
        {"E",  {IDebugBackend::RegisterId::DE, RegMapping::Low}},
        {"H",  {IDebugBackend::RegisterId::HL, RegMapping::High}},
        {"L",  {IDebugBackend::RegisterId::HL, RegMapping::Low}},
        {"PC", {IDebugBackend::RegisterId::PC, RegMapping::Whole}},
        {"SP", {IDebugBackend::RegisterId::SP, RegMapping::Whole}},
    };

    auto it = regMap.find(name);
    if (it == regMap.end()) {
        log_.record("setRegister", "name=" + name,
                     "unknown register", elapsedMs(t0), false, "unknown register");
        return AgentApiResult<void>::fail(
            ErrorCode::InvalidArgument,
            "Unknown register: " + name);
    }

    const auto &m = it->second;
    uint16_t writeValue = value;

    if (m.half != RegMapping::Whole) {
        // Read current pair, modify one half, write back
        CpuState cpu = backend_.getCpuState();
        uint16_t current = 0;
        switch (m.pair) {
            case IDebugBackend::RegisterId::AF: current = (static_cast<uint16_t>(cpu.a) << 8) | cpu.flags; break;
            case IDebugBackend::RegisterId::BC: current = (static_cast<uint16_t>(cpu.b) << 8) | cpu.c; break;
            case IDebugBackend::RegisterId::DE: current = (static_cast<uint16_t>(cpu.d) << 8) | cpu.e; break;
            case IDebugBackend::RegisterId::HL: current = (static_cast<uint16_t>(cpu.h) << 8) | cpu.l; break;
            default: break;
        }
        if (m.half == RegMapping::High) {
            writeValue = (value << 8) | (current & 0xFF);
        } else {
            writeValue = (current & 0xFF00) | (value & 0xFF);
        }
    }

    bool ok = backend_.writeRegister(m.pair, writeValue);

    std::ostringstream oss;
    oss << name << "=" << std::hex << value;
    log_.record("setRegister", oss.str(), ok ? "ok" : "failed",
                elapsedMs(t0), ok, ok ? "" : "write failed");

    if (!ok) {
        return AgentApiResult<void>::fail(
            ErrorCode::OperationFailed, "Register write failed");
    }
    return AgentApiResult<void>::ok();
}

// ---------------------------------------------------------------------------
// Disassembly (Stage 6.1 §10)
// ---------------------------------------------------------------------------

AgentApiResult<std::vector<DisassembledInstructionResult>>
AgentApi::disassemble(uint16_t address, size_t count)
{
    auto t0 = std::chrono::steady_clock::now();

    if (count == 0) {
        log_.record("disassemble", "count=0", "invalid count",
                    elapsedMs(t0), false, "count must be > 0");
        return AgentApiResult<std::vector<DisassembledInstructionResult>>::fail(
            ErrorCode::InvalidArgument, "count must be > 0");
    }

    auto readByte = [this](uint16_t addr) -> uint8_t {
        return backend_.readMemory(addr);
    };

    std::vector<DisassembledInstructionResult> result;
    result.reserve(count);

    uint16_t pc = address;
    for (size_t i = 0; i < count; ++i) {
        DisassembledInstruction di = ::disassemble(pc, readByte);

        DisassembledInstructionResult entry;
        entry.address = di.address;
        entry.next_address = pc + di.length;
        entry.bytes.assign(di.bytes.begin(), di.bytes.begin() + di.length);
        entry.mnemonic = di.mnemonic;
        entry.operands = di.operands;
        entry.text = di.text;
        result.push_back(std::move(entry));

        uint16_t nextPc = pc + di.length;
        if (nextPc <= pc) break;  // overflow guard
        pc = nextPc;
    }

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " count=" << std::dec << count;
    log_.record("disassemble", oss.str(),
                std::to_string(result.size()) + " instructions",
                elapsedMs(t0));

    return AgentApiResult<std::vector<DisassembledInstructionResult>>::ok(
        std::move(result));
}

// ---------------------------------------------------------------------------
// Instruction History (Stage 6.1 §11)
// ---------------------------------------------------------------------------

AgentApiResult<std::vector<InstructionHistoryEntry>>
AgentApi::getInstructionHistory(size_t count)
{
    auto t0 = std::chrono::steady_clock::now();

    if (count == 0) {
        log_.record("getInstructionHistory", "count=0", "invalid count",
                    elapsedMs(t0), false, "count must be > 0");
        return AgentApiResult<std::vector<InstructionHistoryEntry>>::fail(
            ErrorCode::InvalidArgument, "count must be > 0");
    }

    auto all = backend_.instructionHistorySnapshot();

    // Take last 'count' entries
    size_t start = 0;
    if (all.size() > count) {
        start = all.size() - count;
    }

    auto readByte = [this](uint16_t addr) -> uint8_t {
        return backend_.readMemory(addr);
    };

    std::vector<InstructionHistoryEntry> result;
    result.reserve(all.size() - start);

    for (size_t i = start; i < all.size(); ++i) {
        const auto &ev = all[i];

        InstructionHistoryEntry entry;
        entry.address = ev.pcBefore;
        entry.next_address = ev.pcAfter;

        // Build instruction bytes: opcode + operand bytes
        entry.bytes.push_back(ev.opcode);
        if (ev.length >= 2) entry.bytes.push_back(ev.operandBytes[0]);
        if (ev.length >= 3) entry.bytes.push_back(ev.operandBytes[1]);

        // Disassemble for text
        DisassembledInstruction di = ::disassemble(ev.pcBefore, readByte);
        entry.disassembly = di.text;

        result.push_back(std::move(entry));
    }

    std::ostringstream oss;
    oss << "count=" << count << " available=" << all.size();
    log_.record("getInstructionHistory", oss.str(),
                std::to_string(result.size()) + " entries",
                elapsedMs(t0));

    return AgentApiResult<std::vector<InstructionHistoryEntry>>::ok(
        std::move(result));
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

AgentApiResult<LoadRomResult> AgentApi::loadRomInfo(const std::string &path, uint32_t org)
{
    auto t0 = std::chrono::steady_clock::now();

    // Auto-detect origin from extension if not explicitly given
    uint32_t origin = org;
    if (origin == 0 && path.find('.') != std::string::npos) {
        origin = getRomLoadAddress(path);
    }

    bool ok = backend_.loadRom(path, org);
    if (!ok) {
        log_.record("loadRomInfo", path, "failed", elapsedMs(t0), false, "load failed");
        return AgentApiResult<LoadRomResult>::fail(
            ErrorCode::OperationFailed, "Failed to load ROM: " + path);
    }

    LoadRomResult result;
    result.path = path;
    result.origin = origin;
    result.pc = backend_.getCpuState().pc;

    std::ostringstream oss;
    oss << "path=" << path << " origin=" << std::hex << origin
        << " pc=" << result.pc;
    log_.record("loadRomInfo", "", oss.str(), elapsedMs(t0));

    return AgentApiResult<LoadRomResult>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Stack (Stage 6.1 §13)
// ---------------------------------------------------------------------------

AgentApiResult<std::vector<StackEntry>>
AgentApi::getStack(size_t limit)
{
    auto t0 = std::chrono::steady_clock::now();

    if (limit == 0) {
        log_.record("getStack", "limit=0", "invalid limit",
                    elapsedMs(t0), false, "limit must be > 0");
        return AgentApiResult<std::vector<StackEntry>>::fail(
            ErrorCode::InvalidArgument, "limit must be > 0");
    }

    uint16_t sp = backend_.getCpuState().sp;
    const auto &db = backend_.symbolDatabase();

    std::vector<StackEntry> result;
    result.reserve(limit);

    for (size_t i = 0; i < limit; ++i) {
        uint16_t addr = sp + static_cast<uint16_t>(i * 2);

        StackEntry entry;
        entry.address = addr;

        // Read 16-bit value (little-endian)
        uint8_t lo = backend_.readMemory(addr);
        uint8_t hi = backend_.readMemory(static_cast<uint16_t>(addr + 1));
        entry.value = static_cast<uint16_t>(lo | (hi << 8));

        // Look up symbol for this value
        const DebugSymbol *sym = db.findSymbol(entry.value);
        if (sym) {
            entry.symbol = sym->name;
        }

        result.push_back(entry);
    }

    std::ostringstream oss;
    oss << "sp=" << std::hex << sp << " limit=" << std::dec << limit;
    log_.record("getStack", oss.str(),
                std::to_string(result.size()) + " entries", elapsedMs(t0));

    return AgentApiResult<std::vector<StackEntry>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Memory Map (Stage 6.1 §18)
// ---------------------------------------------------------------------------

AgentApiResult<std::vector<MemoryMapBlock>>
AgentApi::getMemoryMap()
{
    auto t0 = std::chrono::steady_clock::now();

    auto activity = backend_.liveActivitySnapshot();
    const auto &db = backend_.symbolDatabase();
    auto regions = db.allRegions();

    std::vector<MemoryMapBlock> result;
    result.reserve(256);

    for (int i = 0; i < 256; ++i) {
        MemoryMapBlock block;
        block.start = static_cast<uint16_t>(i * 256);
        block.end = static_cast<uint16_t>(block.start + 255);

        // Classification from SymbolDatabase regions
        block.classification = MemoryMapBlock::Classification::Unknown;
        for (const auto &r : regions) {
            if (r.start <= block.start && r.end >= block.end) {
                switch (r.type) {
                    case MemoryRegionType::Code:
                        block.classification = MemoryMapBlock::Classification::Code;
                        break;
                    case MemoryRegionType::Data:
                        block.classification = MemoryMapBlock::Classification::Data;
                        break;
                    default:
                        break;
                }
                break;
            }
        }

        // Activity counters
        block.read_activity = activity.blocks[i].lastReadTime.time_since_epoch().count() > 0
            ? 1 : 0;  // simplified: 1 = accessed, 0 = not
        block.write_activity = activity.blocks[i].lastWriteTime.time_since_epoch().count() > 0
            ? 1 : 0;

        // Check if block has non-zero content
        auto data = backend_.readMemorySnapshot(block.start, 256);
        for (uint8_t b : data.data) {
            if (b != 0) {
                block.has_content = true;
                break;
            }
        }

        result.push_back(block);
    }

    log_.record("getMemoryMap", "", "256 blocks", elapsedMs(t0));

    return AgentApiResult<std::vector<MemoryMapBlock>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Screen Info (Stage 6.1 §20)
// ---------------------------------------------------------------------------

AgentApiResult<ScreenInfoResult> AgentApi::getScreenInfo()
{
    auto t0 = std::chrono::steady_clock::now();
    auto video = backend_.videoModeSnapshot();

    ScreenInfoResult result;
    result.width = video.screenWidth;
    result.height = video.screenHeight;
    result.visible_width = video.visibleWidth;
    result.visible_height = video.visibleHeight;
    result.mode512 = video.mode512;
    result.scroll_value = video.scrollValue;
    result.vram_base = video.vramBase;
    result.pixels_per_byte = video.pixelsPerByte;

    std::ostringstream oss;
    oss << result.width << "x" << result.height
        << (result.mode512 ? " 512-mode" : " 256-mode");
    log_.record("getScreenInfo", "", oss.str(), elapsedMs(t0));

    return AgentApiResult<ScreenInfoResult>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// VRAM Info (Stage 6.1 §19)
// ---------------------------------------------------------------------------

AgentApiResult<VramInfoResult> AgentApi::getVramInfo()
{
    auto t0 = std::chrono::steady_clock::now();
    auto video = backend_.videoModeSnapshot();

    VramInfoResult result;
    result.mode512 = video.mode512;
    result.vram_base = video.vramBase;
    result.scroll_value = video.scrollValue;

    // Vector-06C VRAM plane layout depends on video mode:
    //   256-mode: 1 screen plane at vramBase (typically 0xC000), 8 KB
    //   512-mode: 2 screen planes at 0xC000 (16 KB) and 0xE000 (16 KB)
    //
    // Derive from actual video mode parameters (visibleWidth, pixelsPerByte).
    int numCols = video.visibleWidth / video.pixelsPerByte;
    uint16_t planeSize = static_cast<uint16_t>(numCols * 256);

    if (!video.mode512) {
        // 256-mode: single screen plane
        VramPlaneInfo plane;
        plane.plane = 0;
        plane.address = video.vramBase;
        plane.size = planeSize;
        result.planes.push_back(plane);
    } else {
        // 512-mode: two screen planes
        // Plane 0: 0xC000 (16 KB)
        // Plane 1: 0xE000 (16 KB)
        VramPlaneInfo plane0;
        plane0.plane = 0;
        plane0.address = 0xC000;
        plane0.size = 16384;
        result.planes.push_back(plane0);

        VramPlaneInfo plane1;
        plane1.plane = 1;
        plane1.address = 0xE000;
        plane1.size = 16384;
        result.planes.push_back(plane1);
    }

    std::ostringstream oss;
    oss << "vram_base=" << std::hex << result.vram_base
        << (result.mode512 ? " 512-mode" : " 256-mode")
        << " " << std::dec << result.planes.size() << " planes";
    log_.record("getVramInfo", "", oss.str(), elapsedMs(t0));

    return AgentApiResult<VramInfoResult>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Symbols (Stage 6.1 §15)
// ---------------------------------------------------------------------------

AgentApiResult<std::vector<SymbolInfo>>
AgentApi::getSymbols(size_t limit)
{
    auto t0 = std::chrono::steady_clock::now();
    const auto &db = backend_.symbolDatabase();
    auto all = db.allSymbols();

    std::vector<SymbolInfo> result;
    size_t count = (limit > 0 && limit < all.size()) ? limit : all.size();
    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        SymbolInfo info;
        info.address = all[i].address;
        info.name = all[i].name;
        info.comment = all[i].comment;
        info.type = (all[i].type == SymbolType::Function)
            ? SymbolInfo::Type::Function
            : SymbolInfo::Type::Label;
        result.push_back(std::move(info));
    }

    log_.record("getSymbols", "",
                std::to_string(result.size()) + " / " + std::to_string(all.size()) + " symbols",
                elapsedMs(t0));

    return AgentApiResult<std::vector<SymbolInfo>>::ok(std::move(result));
}

AgentApiResult<SymbolInfo> AgentApi::getFunction(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    const auto &db = backend_.symbolDatabase();
    const DebugSymbol *sym = db.findSymbol(address);

    if (!sym) {
        log_.record("getFunction", "addr=" + std::to_string(address),
                    "not found", elapsedMs(t0), false, "symbol not found");
        return AgentApiResult<SymbolInfo>::fail(
            ErrorCode::InvalidAddress, "No symbol at address");
    }

    SymbolInfo info;
    info.address = sym->address;
    info.name = sym->name;
    info.comment = sym->comment;
    info.type = (sym->type == SymbolType::Function)
        ? SymbolInfo::Type::Function
        : SymbolInfo::Type::Label;

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " name=" << sym->name;
    log_.record("getFunction", oss.str(), "found", elapsedMs(t0));

    return AgentApiResult<SymbolInfo>::ok(std::move(info));
}

// ---------------------------------------------------------------------------
// Xrefs (Stage 6.1 §16)
// ---------------------------------------------------------------------------

AgentApiResult<std::vector<XrefResult>>
AgentApi::getXrefs(uint16_t address)
{
    auto t0 = std::chrono::steady_clock::now();
    const auto &db = backend_.symbolDatabase();

    auto toXrefs = db.xrefsTo(address);
    auto fromXrefs = db.xrefsFrom(address);

    std::vector<XrefResult> result;
    result.reserve(toXrefs.size() + fromXrefs.size());

    for (const auto &xr : toXrefs) {
        XrefResult r;
        r.from = xr.from;
        r.to = xr.to;
        result.push_back(r);
    }
    for (const auto &xr : fromXrefs) {
        XrefResult r;
        r.from = xr.from;
        r.to = xr.to;
        result.push_back(r);
    }

    std::ostringstream oss;
    oss << "addr=" << std::hex << address
        << " to=" << std::dec << toXrefs.size()
        << " from=" << fromXrefs.size();
    log_.record("getXrefs", oss.str(),
                std::to_string(result.size()) + " xrefs", elapsedMs(t0));

    return AgentApiResult<std::vector<XrefResult>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Call Graph (Stage 6.1 §17)
// ---------------------------------------------------------------------------

AgentApiResult<std::vector<CallGraphEdge>>
AgentApi::getCallGraph(uint16_t address, size_t limit)
{
    auto t0 = std::chrono::steady_clock::now();
    const auto &db = backend_.symbolDatabase();
    auto allEdges = db.callGraph();

    std::vector<CallGraphEdge> result;

    if (address != 0) {
        // Filter edges involving the given address (as caller or callee)
        for (const auto &e : allEdges) {
            if (e.from == address || e.to == address) {
                CallGraphEdge edge;
                edge.from = e.from;
                edge.to = e.to;
                result.push_back(edge);
            }
        }
    } else {
        // Return all edges
        for (const auto &e : allEdges) {
            CallGraphEdge edge;
            edge.from = e.from;
            edge.to = e.to;
            result.push_back(edge);
        }
    }

    // Apply limit
    if (limit > 0 && result.size() > limit) {
        result.resize(limit);
    }

    std::ostringstream oss;
    oss << "addr=" << std::hex << address << " limit=" << std::dec << limit;
    log_.record("getCallGraph", oss.str(),
                std::to_string(result.size()) + " edges", elapsedMs(t0));

    return AgentApiResult<std::vector<CallGraphEdge>>::ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Debug State (Stage 6.1 §21)
// ---------------------------------------------------------------------------

AgentApiResult<DebugStateResult> AgentApi::getDebugState()
{
    auto t0 = std::chrono::steady_clock::now();

    DebugStateResult result;
    result.running = !backend_.isPaused();
    result.cpu = backend_.getCpuState();
    result.breakpoints = backend_.getBreakpoints();

    // Disassemble current instruction
    auto readByte = [this](uint16_t addr) -> uint8_t {
        return backend_.readMemory(addr);
    };
    DisassembledInstruction di = ::disassemble(result.cpu.pc, readByte);
    result.current_instruction = di.text;

    // Look up function name at PC
    const auto &db = backend_.symbolDatabase();
    const DebugSymbol *sym = db.findSymbol(result.cpu.pc);
    if (sym) {
        result.current_function = sym->name;
    }

    std::ostringstream oss;
    oss << "pc=" << std::hex << result.cpu.pc
        << " running=" << result.running
        << " bps=" << std::dec << result.breakpoints.size();
    log_.record("getDebugState", "", oss.str(), elapsedMs(t0));

    return AgentApiResult<DebugStateResult>::ok(std::move(result));
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

        DisassembledInstruction di = ::disassemble(pc, readByte);

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
    // Stage 5.3.3.2: No polling — wait for Emulation Thread pause via future.
    auto pauseFuture = backend_.requestRunFuture();
    pauseFuture.wait();  // blocks until breakpoint hit or emulation pauses

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
