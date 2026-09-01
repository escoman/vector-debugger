#pragma once

#include <cstdint>
#include "backend.h"  // CpuState

// ---------------------------------------------------------------------------
// Instruction event — recorded after every executed 8080 instruction
// ---------------------------------------------------------------------------

struct InstructionEvent
{
    uint64_t sequence;

    uint16_t pcBefore;
    uint16_t pcAfter;

    uint8_t opcode;
    uint8_t length;

    int cycles;

    CpuState before;
    CpuState after;
};

// ---------------------------------------------------------------------------
// Memory access event — recorded via Memory::onread / onwrite callbacks
// ---------------------------------------------------------------------------

enum class MemoryAccessType
{
    Fetch,
    Read,
    Write
};

struct MemoryAccessEvent
{
    uint64_t instructionSequence;

    MemoryAccessType type;

    uint16_t virt;
    uint32_t phys;

    uint8_t value;

    bool stack;
};

// ---------------------------------------------------------------------------
// I/O access event — recorded via IO::onread / onwrite callbacks
// ---------------------------------------------------------------------------

enum class IoAccessType
{
    In,
    Out
};

struct IoAccessEvent
{
    uint64_t instructionSequence;

    IoAccessType type;

    uint8_t port;
    uint8_t value;
};

// ---------------------------------------------------------------------------
// Per-address cumulative memory statistics (for future Memory Map)
// ---------------------------------------------------------------------------

struct MemoryStats
{
    uint64_t reads;
    uint64_t writes;

    uint64_t lastReadSequence;
    uint64_t lastWriteSequence;
};
