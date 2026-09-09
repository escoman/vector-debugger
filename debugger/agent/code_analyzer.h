#pragma once

// ---------------------------------------------------------------------------
// code_analyzer.h — Stage 6.16
//
// Control-flow analysis of 8080 machine code.
// Uses the existing disassembler (disassemble()) — does NOT create a
// second opcode decoder.
//
// The analyzer performs BFS from an entry point, following JMP/CALL/Jcc/
// RST targets and fall-through paths.  It detects instruction-boundary
// conflicts (two paths interpreting the same byte as different instruction
// starts) and forms contiguous code ranges.
//
// The analyzer does NOT modify RDB or SymbolDatabase.
// ---------------------------------------------------------------------------

#include "disassembler.h"

#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <queue>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Result types
// ---------------------------------------------------------------------------

struct AnalyzedInstruction {
    uint16_t    address  = 0;
    uint8_t     opcode   = 0;
    uint8_t     size     = 0;
    std::string mnemonic;
    std::string operands;
    std::string text;
    bool        hasTarget = false;
    uint16_t    target    = 0;
};

struct ControlFlowRef {
    uint16_t    from = 0;
    uint16_t    to   = 0;
    std::string type;   // "JMP", "JCC", "CALL", "CALLCC", "RST"
};

struct CodeRange {
    uint16_t start = 0;
    uint16_t end   = 0;   // inclusive
};

struct AnalysisConflict {
    uint16_t    address = 0;
    std::string description;
};

struct CodeAnalysisResult {
    uint16_t                       entryPoint = 0;
    std::vector<AnalyzedInstruction> instructions;
    std::vector<ControlFlowRef>     references;
    std::vector<CodeRange>          ranges;
    std::vector<AnalysisConflict>   conflicts;
    size_t   instructionCount = 0;
    bool     truncated        = false;
};

// ---------------------------------------------------------------------------
// classifyControlFlow — determine 8080 control-flow type from opcode
// ---------------------------------------------------------------------------

enum class ControlFlowType {
    Sequential,       // normal instruction — continue to next
    UnconditionalJmp, // JMP (C3)
    ConditionalJmp,   // Jcc (C2,CA,D2,DA,E2,EA,F2,FA)
    UnconditionalCall,// CALL (CD)
    ConditionalCall,  // Ccc (C4,CC,D4,DC,E4,EC,F4,FC)
    UnconditionalRet, // RET (C9)
    ConditionalRet,   // Rcc (C0,C8,D0,D8,E0,E8,F0,F8)
    Restart,          // RST n (C7,CF,D7,DF,E7,EF,F7,FF)
    Halt,             // HLT (76)
    IndirectJump,     // PCHL (E9) — target unknown statically
};

inline ControlFlowType classifyControlFlow(uint8_t opcode)
{
    switch (opcode) {
    case 0xC3: return ControlFlowType::UnconditionalJmp;
    case 0xCD: return ControlFlowType::UnconditionalCall;
    case 0xC9: return ControlFlowType::UnconditionalRet;
    case 0xE9: return ControlFlowType::IndirectJump;
    case 0x76: return ControlFlowType::Halt;
    default: break;
    }

    uint8_t lo = opcode & 0x0F;

    // Jcc: C2,JNZ  CA,JZ  D2,JNC  DA,JC  E2,JPO  EA,JPE  F2,JP  FA,JM
    if ((opcode & 0xC7) == 0xC0 && lo != 0x09 && lo != 0x01 &&
        lo != 0x0B && lo != 0x03 && opcode != 0xC9) {
        // Distinguish JMP-family from RET/CALL-family
        if (lo == 0x02 || lo == 0x0A)
            return ControlFlowType::ConditionalJmp;
    }

    // Ccc: C4,CNZ  CC,CZ  D4,CNC  DC,CC  E4,CPO  EC,CPE  F4,CP  FC,CM
    if ((opcode & 0xC7) == 0xC0 &&
        (lo == 0x04 || lo == 0x0C)) {
        return ControlFlowType::ConditionalCall;
    }

    // Rcc: C0,RNZ  C8,RZ  D0,RNC  D8,RC  E0,RPO  E8,RPE  F0,RP  F8,RM
    if ((opcode & 0xC7) == 0xC0 &&
        (lo == 0x00 || lo == 0x08)) {
        return ControlFlowType::ConditionalRet;
    }

    // RST n: C7,CF,D7,DF,E7,EF,F7,FF
    if ((opcode & 0xC7) == 0xC0 && (lo == 0x07 || lo == 0x0F)) {
        return ControlFlowType::Restart;
    }

    return ControlFlowType::Sequential;
}

// ---------------------------------------------------------------------------
// analyzeCode — BFS control-flow analysis
// ---------------------------------------------------------------------------

inline CodeAnalysisResult analyzeCode(
    uint16_t startAddress,
    DisasmReadFn readByte,
    size_t maxInstructions)
{
    CodeAnalysisResult result;
    result.entryPoint = startAddress;

    std::set<uint16_t> visited;
    std::unordered_map<uint16_t, uint8_t> instrEndMap; // address → last byte offset
    std::queue<uint16_t> workQueue;

    workQueue.push(startAddress);

    while (!workQueue.empty() && result.instructions.size() < maxInstructions) {
        uint16_t addr = workQueue.front();
        workQueue.pop();

        // Already fully processed
        if (visited.count(addr)) continue;

        // Conflict: this address falls inside an already-decoded instruction
        for (const auto &kv : instrEndMap) {
            uint16_t instrStart = kv.first;
            uint16_t instrLast  = instrStart + kv.second; // inclusive last byte
            if (addr > instrStart && addr <= instrLast) {
                result.conflicts.push_back({addr,
                    "instruction boundary conflict: address falls inside "
                    "existing instruction at " + std::to_string(instrStart)});
                goto skip_decode;
            }
        }

        {
            DisassembledInstruction di = disassemble(addr, readByte);

            AnalyzedInstruction ai;
            ai.address   = di.address;
            ai.opcode    = di.opcode;
            ai.size      = di.length;
            ai.mnemonic  = di.mnemonic;
            ai.operands  = di.operands;
            ai.text      = di.text;
            ai.hasTarget = di.hasTarget;
            ai.target    = di.target;
            result.instructions.push_back(ai);

            visited.insert(addr);
            instrEndMap[addr] = static_cast<uint8_t>(di.length - 1);

            uint16_t nextAddr = addr + di.length;
            ControlFlowType cft = classifyControlFlow(di.opcode);

            // Record control-flow references
            switch (cft) {
            case ControlFlowType::UnconditionalJmp:
                result.references.push_back({addr, di.target, "JMP"});
                break;
            case ControlFlowType::ConditionalJmp:
                result.references.push_back({addr, di.target, "JCC"});
                break;
            case ControlFlowType::UnconditionalCall:
                result.references.push_back({addr, di.target, "CALL"});
                break;
            case ControlFlowType::ConditionalCall:
                result.references.push_back({addr, di.target, "CALLCC"});
                break;
            case ControlFlowType::Restart:
                result.references.push_back({addr, di.target, "RST"});
                break;
            default:
                break;
            }

            // Determine successors for BFS
            switch (cft) {
            case ControlFlowType::UnconditionalJmp:
                // Only follow target; fall-through is NOT reachable
                if (!visited.count(di.target))
                    workQueue.push(di.target);
                break;

            case ControlFlowType::ConditionalJmp:
                // Both target and fall-through are reachable
                if (!visited.count(di.target))
                    workQueue.push(di.target);
                if (!visited.count(nextAddr))
                    workQueue.push(nextAddr);
                break;

            case ControlFlowType::UnconditionalCall:
            case ControlFlowType::ConditionalCall:
                // Follow call target AND continue after call
                if (!visited.count(di.target))
                    workQueue.push(di.target);
                if (!visited.count(nextAddr))
                    workQueue.push(nextAddr);
                break;

            case ControlFlowType::UnconditionalRet:
            case ControlFlowType::Halt:
            case ControlFlowType::IndirectJump:
                // Path terminates
                break;

            case ControlFlowType::ConditionalRet:
                // Both return (terminates) and fall-through are reachable
                if (!visited.count(nextAddr))
                    workQueue.push(nextAddr);
                break;

            case ControlFlowType::Restart:
                // RST is like CALL: follow target + continue after
                if (!visited.count(di.target))
                    workQueue.push(di.target);
                if (!visited.count(nextAddr))
                    workQueue.push(nextAddr);
                break;

            case ControlFlowType::Sequential:
                if (!visited.count(nextAddr))
                    workQueue.push(nextAddr);
                break;
            }
        }
        skip_decode:;
    }

    result.truncated = !workQueue.empty() &&
                       result.instructions.size() >= maxInstructions;
    result.instructionCount = result.instructions.size();

    // Form contiguous code ranges (inclusive end)
    if (!result.instructions.empty()) {
        std::vector<AnalyzedInstruction> sorted = result.instructions;
        std::sort(sorted.begin(), sorted.end(),
                  [](const AnalyzedInstruction &a, const AnalyzedInstruction &b) {
                      return a.address < b.address;
                  });

        uint16_t rangeStart = sorted[0].address;
        uint16_t rangeEnd   = sorted[0].address + sorted[0].size - 1;

        for (size_t i = 1; i < sorted.size(); ++i) {
            uint16_t curStart = sorted[i].address;
            if (curStart <= rangeEnd + 1) {
                // Contiguous or overlapping — extend range
                uint16_t curEnd = curStart + sorted[i].size - 1;
                if (curEnd > rangeEnd) rangeEnd = curEnd;
            } else {
                result.ranges.push_back({rangeStart, rangeEnd});
                rangeStart = curStart;
                rangeEnd   = curStart + sorted[i].size - 1;
            }
        }
        result.ranges.push_back({rangeStart, rangeEnd});
    }

    return result;
}
