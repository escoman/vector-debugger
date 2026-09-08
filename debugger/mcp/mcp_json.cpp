// ---------------------------------------------------------------------------
// mcp_json.cpp — Stage 6.4
//
// JSON serialization implementation for Agent API types.
// ---------------------------------------------------------------------------

#include "mcp_json.h"

namespace mcp_json {

// -- CpuState ----------------------------------------------------------------

json cpuStateToJson(const CpuState &cpu) {
    return {
        {"pc",     hex16(cpu.pc)},
        {"sp",     hex16(cpu.sp)},
        {"a",      hex8(cpu.a)},
        {"b",      hex8(cpu.b)},
        {"c",      hex8(cpu.c)},
        {"d",      hex8(cpu.d)},
        {"e",      hex8(cpu.e)},
        {"h",      hex8(cpu.h)},
        {"l",      hex8(cpu.l)},
        {"flags",  hex8(cpu.flags)},
        {"iff",    cpu.iff},
        {"cycles", cpu.cycles}
    };
}

// -- DebuggerBreakpoint ------------------------------------------------------

json breakpointToJson(const DebuggerBreakpoint &bp) {
    return {
        {"address", hex16(bp.address)},
        {"enabled", bp.enabled}
    };
}

json breakpointsToJson(const std::vector<DebuggerBreakpoint> &bps) {
    json arr = json::array();
    for (auto &bp : bps) arr.push_back(breakpointToJson(bp));
    return arr;
}

// -- DisassembledInstructionResult -------------------------------------------

json disassembledInstructionToJson(const DisassembledInstructionResult &inst) {
    json bytesArr = json::array();
    for (auto b : inst.bytes) bytesArr.push_back(b);

    return {
        {"address",      hex16(inst.address)},
        {"next_address", hex16(inst.next_address)},
        {"bytes",        bytesArr},
        {"mnemonic",     inst.mnemonic},
        {"operands",     inst.operands},
        {"text",         inst.text}
    };
}

json disassembledInstructionsToJson(const std::vector<DisassembledInstructionResult> &insts) {
    json arr = json::array();
    for (auto &inst : insts) arr.push_back(disassembledInstructionToJson(inst));
    return arr;
}

// -- InstructionHistoryEntry -------------------------------------------------

json historyEntryToJson(const InstructionHistoryEntry &entry) {
    json bytesArr = json::array();
    for (auto b : entry.bytes) bytesArr.push_back(b);

    return {
        {"address",      hex16(entry.address)},
        {"next_address", hex16(entry.next_address)},
        {"bytes",        bytesArr},
        {"disassembly",  entry.disassembly}
    };
}

json historyEntriesToJson(const std::vector<InstructionHistoryEntry> &entries) {
    json arr = json::array();
    for (auto &e : entries) arr.push_back(historyEntryToJson(e));
    return arr;
}

// -- InstructionEvent (execution trace) --------------------------------------

json instructionEventToJson(const InstructionEvent &ev) {
    return {
        {"sequence",  ev.sequence},
        {"pc_before", hex16(ev.pcBefore)},
        {"pc_after",  hex16(ev.pcAfter)},
        {"opcode",    ev.opcode},
        {"length",    ev.length},
        {"cycles",    ev.cycles}
    };
}

json instructionEventsToJson(const std::vector<InstructionEvent> &events) {
    json arr = json::array();
    for (auto &ev : events) arr.push_back(instructionEventToJson(ev));
    return arr;
}

// -- IoAccessEvent -----------------------------------------------------------

json ioAccessEventToJson(const IoAccessEvent &ev) {
    return {
        {"sequence", ev.instructionSequence},
        {"type",     ev.type == IoAccessType::In ? "in" : "out"},
        {"port",     hex8(ev.port)},
        {"value",    hex8(ev.value)}
    };
}

json ioAccessEventsToJson(const std::vector<IoAccessEvent> &events) {
    json arr = json::array();
    for (auto &ev : events) arr.push_back(ioAccessEventToJson(ev));
    return arr;
}

// -- StackEntry --------------------------------------------------------------

json stackEntryToJson(const StackEntry &entry) {
    return {
        {"address", hex16(entry.address)},
        {"value",   hex16(entry.value)},
        {"symbol",  entry.symbol}
    };
}

json stackEntriesToJson(const std::vector<StackEntry> &entries) {
    json arr = json::array();
    for (auto &e : entries) arr.push_back(stackEntryToJson(e));
    return arr;
}

// -- MemoryMapBlock ----------------------------------------------------------

json memoryMapBlockToJson(const MemoryMapBlock &block) {
    std::string classification;
    switch (block.classification) {
        case MemoryMapBlock::Classification::Code: classification = "code"; break;
        case MemoryMapBlock::Classification::Data: classification = "data"; break;
        default: classification = "unknown"; break;
    }

    return {
        {"start",           hex16(block.start)},
        {"end",             hex16(block.end)},
        {"classification",  classification},
        {"read_activity",   block.read_activity},
        {"write_activity",  block.write_activity},
        {"execute_activity", block.execute_activity},
        {"has_content",     block.has_content}
    };
}

json memoryMapBlocksToJson(const std::vector<MemoryMapBlock> &blocks) {
    json arr = json::array();
    for (auto &b : blocks) arr.push_back(memoryMapBlockToJson(b));
    return arr;
}

// -- ScreenInfoResult --------------------------------------------------------

json screenInfoToJson(const ScreenInfoResult &info) {
    return {
        {"width",          info.width},
        {"height",         info.height},
        {"visible_width",  info.visible_width},
        {"visible_height", info.visible_height},
        {"mode512",        info.mode512},
        {"scroll_value",   info.scroll_value},
        {"vram_base",      hex16(info.vram_base)},
        {"pixels_per_byte", info.pixels_per_byte}
    };
}

// -- VramInfoResult ----------------------------------------------------------

json vramInfoToJson(const VramInfoResult &info) {
    json planesArr = json::array();
    for (auto &p : info.planes) {
        planesArr.push_back({
            {"plane",   p.plane},
            {"address", hex16(p.address)},
            {"size",    p.size}
        });
    }

    return {
        {"mode512",     info.mode512},
        {"vram_base",   hex16(info.vram_base)},
        {"scroll_value", info.scroll_value},
        {"planes",      planesArr}
    };
}

// -- SymbolInfo --------------------------------------------------------------

json symbolInfoToJson(const SymbolInfo &sym) {
    std::string typeStr;
    switch (sym.type) {
        case SymbolInfo::Type::Function: typeStr = "function"; break;
        case SymbolInfo::Type::Label:    typeStr = "label"; break;
    }

    return {
        {"address", hex16(sym.address)},
        {"name",    sym.name},
        {"type",    typeStr},
        {"comment", sym.comment}
    };
}

json symbolInfosToJson(const std::vector<SymbolInfo> &syms) {
    json arr = json::array();
    for (auto &s : syms) arr.push_back(symbolInfoToJson(s));
    return arr;
}

// -- XrefResult --------------------------------------------------------------

json xrefResultToJson(const XrefResult &xref) {
    return {
        {"from", hex16(xref.from)},
        {"to",   hex16(xref.to)}
    };
}

json xrefResultsToJson(const std::vector<XrefResult> &xrefs) {
    json arr = json::array();
    for (auto &x : xrefs) arr.push_back(xrefResultToJson(x));
    return arr;
}

// -- CallGraphEdge -----------------------------------------------------------

json callGraphEdgeToJson(const CallGraphEdge &edge) {
    return {
        {"from", hex16(edge.from)},
        {"to",   hex16(edge.to)}
    };
}

json callGraphEdgesToJson(const std::vector<CallGraphEdge> &edges) {
    json arr = json::array();
    for (auto &e : edges) arr.push_back(callGraphEdgeToJson(e));
    return arr;
}

// -- LoadRomResult -----------------------------------------------------------

json loadRomResultToJson(const LoadRomResult &result) {
    return {
        {"path",   result.path},
        {"origin", hex16(static_cast<uint16_t>(result.origin))},
        {"pc",     hex16(result.pc)}
    };
}

// -- DebugStateResult --------------------------------------------------------

json debugStateToJson(const DebugStateResult &state) {
    return {
        {"running",            state.running},
        {"cpu",                cpuStateToJson(state.cpu)},
        {"breakpoints",        breakpointsToJson(state.breakpoints)},
        {"current_instruction", state.current_instruction},
        {"current_function",   state.current_function}
    };
}

// -- AgentScreenSnapshot -----------------------------------------------------

json screenSnapshotToJson(const AgentScreenSnapshot &snap) {
    return {
        {"width",  snap.width},
        {"height", snap.height},
        {"pixel_count", static_cast<int>(snap.pixels.size())}
    };
}

// -- FunctionContext ---------------------------------------------------------

json functionContextToJson(const FunctionContext &ctx) {
    // Instructions
    json instructions = json::array();
    for (auto &inst : ctx.instructions) {
        json bytesArr = json::array();
        for (int i = 0; i < inst.length; ++i) bytesArr.push_back(inst.bytes[i]);

        instructions.push_back({
            {"address", hex16(inst.address)},
            {"text",    inst.text},
            {"bytes",   bytesArr},
            {"length",  inst.length}
        });
    }

    // Callers / callees
    json callers = json::array();
    for (auto a : ctx.callers) callers.push_back(hex16(a));
    json callees = json::array();
    for (auto a : ctx.callees) callees.push_back(hex16(a));

    // Stack behavior
    std::string stackBehavior;
    switch (ctx.stackBehavior) {
        case FunctionContext::Balanced:   stackBehavior = "balanced"; break;
        case FunctionContext::Unbalanced: stackBehavior = "unbalanced"; break;
        default: stackBehavior = "unknown"; break;
    }

    return {
        {"address",        hex16(ctx.address)},
        {"size",           ctx.size},
        {"name",           ctx.name},
        {"comment",        ctx.comment},
        {"instructions",   instructions},
        {"is_heuristic",   ctx.isHeuristic},
        {"callers",        callers},
        {"callees",        callees},
        {"memory_reads",   static_cast<int>(ctx.memoryReads.size())},
        {"memory_writes",  static_cast<int>(ctx.memoryWrites.size())},
        {"io_accesses",    static_cast<int>(ctx.ioAccesses.size())},
        {"vram_writes",    static_cast<int>(ctx.vramWrites.size())},
        {"stack_behavior", stackBehavior},
        {"entry_sp",       hex16(ctx.entrySp)},
        {"exit_sp",        hex16(ctx.exitSp)},
        {"min_sp",         hex16(ctx.minSp)},
        {"max_sp",         hex16(ctx.maxSp)},
        {"call_depth",     ctx.callDepth}
    };
}

// -- TraceResult -------------------------------------------------------------

json traceResultToJson(const TraceResult &result) {
    json executedPcs = json::array();
    for (auto pc : result.executedPcs) executedPcs.push_back(hex16(pc));

    json calledFunctions = json::array();
    for (auto a : result.calledFunctions) calledFunctions.push_back(hex16(a));

    return {
        {"entry_pc",          hex16(result.entryPc)},
        {"exit_pc",           hex16(result.exitPc)},
        {"instruction_count", result.instructionCount},
        {"execution_count",   result.executionCount},
        {"executed_pcs",      executedPcs},
        {"memory_reads",      static_cast<int>(result.memoryReads.size())},
        {"memory_writes",     static_cast<int>(result.memoryWrites.size())},
        {"io_reads",          static_cast<int>(result.ioReads.size())},
        {"io_writes",         static_cast<int>(result.ioWrites.size())},
        {"vram_writes",       static_cast<int>(result.vramWrites.size())},
        {"called_functions",  calledFunctions},
        {"entry_sp",          hex16(result.entrySp)},
        {"exit_sp",           hex16(result.exitSp)},
        {"min_sp",            hex16(result.minSp)},
        {"max_sp",            hex16(result.maxSp)},
        {"call_depth",        result.callDepth}
    };
}

// -- Error / Success helpers -------------------------------------------------

json errorResult(ErrorCode code, const std::string &message) {
    return {
        {"success",    false},
        {"error_code", static_cast<int>(code)},
        {"error",      message}
    };
}

json successResult(const json &value) {
    return {
        {"success", true},
        {"data",    value}
    };
}

json successVoidResult() {
    return {
        {"success", true}
    };
}

// -- RDB types (Stage 6.11) --------------------------------------------------

json rdbInfoToJson(const RdbInfoResult &info) {
    return {
        {"path",         info.path},
        {"platform",     info.platform},
        {"version",      info.version},
        {"loaded",       info.loaded},
        {"dirty",        info.dirty},
        {"exists_on_disk", info.existsOnDisk},
        {"object_count", static_cast<int>(info.objectCount)},
        {"rom_file",     info.romFile},
        {"rom_size",     info.romSize},
        {"rom_sha256",   info.romSha256}
    };
}

json rdbObjectToJson(const RdbObjectResult &obj) {
    json linksArr = json::array();
    for (uint16_t l : obj.links) {
        linksArr.push_back(hex16(l));
    }
    json propsObj = json::object();
    for (const auto &kv : obj.properties) {
        propsObj[kv.first] = kv.second;
    }
    return {
        {"address",  hex16(obj.address)},
        {"type",     obj.type},
        {"name",     obj.name},
        {"size",     obj.hasSize ? static_cast<int>(obj.size) : -1},
        {"comment",  obj.comment},
        {"links",    linksArr},
        {"properties", propsObj}
    };
}

json rdbObjectsToJson(const std::vector<RdbObjectResult> &objects) {
    json arr = json::array();
    for (const auto &obj : objects) {
        arr.push_back(rdbObjectToJson(obj));
    }
    return arr;
}

} // namespace mcp_json
