#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

// ---------------------------------------------------------------------------
// Shared data types for Debugger Core
//
// These types flow across the IDebugTarget / IDebugBackend interfaces.
// Kept in a separate header to avoid circular includes between
// debug_target.h, idebug_backend.h, and backend.h.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// CPU state snapshot
// ---------------------------------------------------------------------------

struct CpuState
{
    uint16_t pc;
    uint16_t sp;

    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;

    uint8_t flags;   // S Z AC P CY packed as in F register

    bool iff;        // interrupt flip-flop

    // Stage 3.6 — additional CPU state
    uint32_t cycles;       // cycles of last instruction
    bool     ei_pending;   // EI pending flag
    uint16_t last_pc;      // PC before last step
};

// ---------------------------------------------------------------------------
// Memory snapshot for Inspector (Stage 3.3)
// ---------------------------------------------------------------------------

struct MemorySnapshot
{
    uint16_t start;
    std::vector<uint8_t> data;
};

// ---------------------------------------------------------------------------
// Screen data (IDebugTarget → DebugBackend)
// ---------------------------------------------------------------------------

struct ScreenData
{
    std::vector<uint32_t> pixels;  // ARGB8888
    int width  = 0;
    int height = 0;
};

// ---------------------------------------------------------------------------
// Palette snapshot (16 Vector-06C colors)
// ---------------------------------------------------------------------------

struct PaletteSnapshot
{
    struct Color {
        uint8_t r, g, b;      // decoded 8-bit RGB
        uint8_t rawByte;       // original Vector-06C palette byte (B:7-6 G:5-3 R:2-0)
    };
    Color entries[16];
    int count = 0;  // number of valid entries (0 = not available)
};

// ---------------------------------------------------------------------------
// Debugger state machine
// ---------------------------------------------------------------------------

enum class DebuggerState
{
    Running,
    Paused,
    Stopped
};

// ---------------------------------------------------------------------------
// Stop reason (Stage 3.7)
// ---------------------------------------------------------------------------

enum class StopReason
{
    None,
    Breakpoint,
    UserPause,
    Step,
    Reset
};

// ---------------------------------------------------------------------------
// Breakpoint model (Stage 3.7)
// ---------------------------------------------------------------------------

struct DebuggerBreakpoint
{
    uint16_t address;
    bool     enabled;
};
