// Stage 3.2 — Simplified Board Integration Smoke Test
//
// This test verifies that the debugger works with the REAL Board,
// without requiring boot ROM or icon binary resources.
// We disable video/sound to avoid SDL dependencies.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "memory.h"
#include "i8080.h"
#include "i8080_hal.h"
#include "backend.h"
#include "events.h"
#include "options.h"
#include "board.h"
#include "vio.h"
#include "tv.h"
#include "sound.h"
#include "filler.h"
#include "keyboard.h"
#include "8253.h"
#include "ay.h"
#include "wav.h"
#include "fd1793.h"
#include "debug_memory.h"

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_BEGIN(name) \
    do { \
        tests_run++; \
        printf("\n\033[0;35m=== TEST: %s ===\033[0m\n", name); \
        const char *_test_name = name; \
        bool _test_ok = true; \
        (void)_test_name;

#define CHECK(cond, msg) \
        do { \
            if (!(cond)) { \
                printf("  \033[41;97m FAIL \033[0m %s (line %d)\n", msg, __LINE__); \
                _test_ok = false; \
            } else { \
                printf("  \033[46;30m ok \033[0m %s\n", msg); \
            } \
        } while(0)

#define CHECK_EQ(exp, act, msg) \
        do { \
            unsigned _e = (unsigned)(exp); \
            unsigned _a = (unsigned)(act); \
            if (_e != _a) { \
                printf("  \033[41;97m FAIL \033[0m %s: expected 0x%X, got 0x%X (line %d)\n", \
                       msg, _e, _a, __LINE__); \
                _test_ok = false; \
            } else { \
                printf("  \033[46;30m ok \033[0m %s = 0x%X\n", msg, _a); \
            } \
        } while(0)

#define TEST_END() \
        if (_test_ok) { \
            tests_passed++; \
            printf("\033[46;30m PASS \033[0m %s\n", _test_name); \
        } else { \
            tests_failed++; \
            printf("\033[41;97m FAIL \033[0m %s\n", _test_name); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// Minimal test program (written directly to Memory, no ROM file)
// ---------------------------------------------------------------------------
//
// 0000: MVI A, 55h    ; 3E 55
// 0002: INR A         ; 3C
// 0003: JMP 0002h     ; C3 02 00
//

static const uint8_t test_program[] = {
    0x3E, 0x55,        // 0000: MVI A, 55h
    0x3C,              // 0002: INR A
    0xC3, 0x02, 0x00,  // 0003: JMP 0002h
};

// ---------------------------------------------------------------------------
// HAL — we use the real hal.cpp from the main project
// ---------------------------------------------------------------------------

static DebugBackend *test_backend = nullptr;

// Forward declarations from hal.cpp
extern void i8080_hal_bind(Memory & _mem, IO & _io, Board & _board);

// ---------------------------------------------------------------------------
// Test: Simplified Board integration smoke test
// ---------------------------------------------------------------------------

static void test_board_smoke()
{
    TEST_BEGIN("Board Integration Smoke Test (simplified)");

    // --- Disable video/sound to avoid SDL dependencies ---
    printf("  Disabling video and sound for headless test...\n");
    Options.novideo = true;
    Options.nosound = true;
    
    // --- Create real components in correct order ---
    printf("  Creating real Board components...\n");
    
    Memory memory;
    FD1793 fdc;
    Wav wav;
    WavPlayer tape_player(wav);
    Keyboard keyboard;
    I8253 timer;
    TimerWrapper tw(timer);
    AY ay;
    AYWrapper aw(ay);
    Soundnik soundnik(tw, aw);
    IO io(memory, keyboard, timer, fdc, ay, tape_player);
    TV tv;
    PixelFiller filler(memory, io, tv);
    
    // Initialize components (headless mode)
    filler.init();
    soundnik.init(nullptr);  // No WavRecorder
    tv.init();  // Will skip video init due to Options.novideo
    
    // Create real Board
    Board board(memory, io, filler, soundnik, tv, tape_player);
    
    printf("  Real Board created\n");
    
    // --- Initialize Board ---
    printf("  Initializing Board...\n");
    board.init();  // This calls i8080_hal_bind()
    
    // --- Create DebugBackend and attach real Board ---
    DebugBackend backend(memory);
    backend.attachBoard(&board);
    test_backend = &backend;
    
    printf("  Real Board attached to DebugBackend\n");
    
    // --- Write test program directly to Memory ---
    printf("  Writing test program to Memory...\n");
    for (size_t i = 0; i < sizeof(test_program); ++i) {
        memory.write((uint16_t)i, test_program[i], false);
    }
    
    // --- Reset Board in LOADROM mode ---
    printf("  Resetting Board in LOADROM mode...\n");
    Options.pc = 0;
    board.reset(Board::ResetMode::LOADROM);
    backend.clearHistory();
    
    // --- Check initial CPU state ---
    printf("  Checking initial CPU state...\n");
    CpuState initial = backend.getCpuState();
    CHECK_EQ(0x0000, initial.pc, "Initial PC == 0x0000");
    CHECK_EQ(0x0000, initial.a,  "Initial A == 0x00");
    
    // --- Step 1: Execute MVI A, 55h ---
    printf("  Stepping: MVI A, 55h...\n");
    StepResult step1 = backend.stepInstruction();
    CHECK_EQ(0x0000, step1.pcBefore, "Step 1: PC before == 0x0000");
    CHECK_EQ(0x0002, step1.pcAfter,  "Step 1: PC after == 0x0002");
    CHECK_EQ(0x3E,   step1.opcode,   "Step 1: opcode == 0x3E (MVI A)");
    
    CpuState after_step1 = backend.getCpuState();
    CHECK_EQ(0x55, after_step1.a, "After step 1: A == 0x55");
    
    // --- Step 2: Execute INR A ---
    printf("  Stepping: INR A...\n");
    StepResult step2 = backend.stepInstruction();
    CHECK_EQ(0x0002, step2.pcBefore, "Step 2: PC before == 0x0002");
    CHECK_EQ(0x0003, step2.pcAfter,  "Step 2: PC after == 0x0003");
    CHECK_EQ(0x3C,   step2.opcode,   "Step 2: opcode == 0x3C (INR A)");
    
    CpuState after_step2 = backend.getCpuState();
    CHECK_EQ(0x56, after_step2.a, "After step 2: A == 0x56 (incremented)");
    
    // --- Test breakpoint ---
    printf("  Testing breakpoint at 0x0002 (INR A)...\n");
    int bp_id = backend.addBreakpoint(0x0002);
    CHECK(bp_id >= 0, "Breakpoint added");
    
    // Reset to 0x0000
    Options.pc = 0;
    board.reset(Board::ResetMode::LOADROM);
    backend.clearHistory();
    
    // Execute MVI A, 55h
    backend.stepInstruction();
    CpuState before_bp = backend.getCpuState();
    CHECK_EQ(0x0002, before_bp.pc, "PC == 0x0002 (at breakpoint)");
    
    // Step should execute the instruction at breakpoint
    backend.stepInstruction();
    CpuState after_bp_step = backend.getCpuState();
    CHECK_EQ(0x56, after_bp_step.a, "After breakpoint step: A == 0x56 (incremented from 0x55)");
    CHECK_EQ(0x0003, after_bp_step.pc, "After breakpoint step: PC == 0x0003");
    
    backend.removeBreakpoint(bp_id);
    
    // --- Test instrumentation ---
    printf("  Checking instrumentation events...\n");
    auto instr_history = backend.instructionHistorySnapshot();
    auto mem_history = backend.memoryHistorySnapshot();
    
    CHECK(instr_history.size() > 0, "Instruction history has events");
    CHECK(mem_history.size() > 0, "Memory history has events");
    
    printf("  Instruction events: %zu\n", instr_history.size());
    printf("  Memory events: %zu\n", mem_history.size());
    
    // Check that we got Fetch events
    int fetch_count = 0;
    for (const auto &ev : mem_history) {
        if (ev.type == MemoryAccessType::Fetch) {
            fetch_count++;
        }
    }
    CHECK(fetch_count > 0, "Got Fetch events from real Board");
    printf("  Fetch events: %d\n", fetch_count);
    
    // --- Test Memory::read/write ---
    printf("  Testing real Memory::read/write...\n");
    memory.write(0x8000, 0xAB, false);
    uint8_t val = DebugMemoryAccess::peek(memory, 0x8000);
    CHECK_EQ(0xAB, val, "Memory::read/write works");
    
    // --- Test Memory Inspector API (Stage 3.3) ---
    printf("  Testing Memory Inspector API...\n");
    
    // Test readMemory()
    uint8_t byte_at_0 = backend.readMemory(0x0000);
    CHECK_EQ(0x3E, byte_at_0, "readMemory(0000) == 0x3E (MVI A opcode)");
    
    // Test readMemorySnapshot()
    MemorySnapshot snap = backend.readMemorySnapshot(0x0000, 6);
    CHECK_EQ(0x0000, snap.start, "Snapshot start == 0x0000");
    CHECK_EQ(6, snap.data.size(), "Snapshot size == 6");
    CHECK_EQ(0x3E, snap.data[0], "Snapshot[0] == 0x3E (MVI A)");
    CHECK_EQ(0x55, snap.data[1], "Snapshot[1] == 0x55 (operand)");
    CHECK_EQ(0x3C, snap.data[2], "Snapshot[2] == 0x3C (INR A)");
    CHECK_EQ(0xC3, snap.data[3], "Snapshot[3] == 0xC3 (JMP)");
    
    printf("  Memory Inspector API works with real Board\n");
    
    // --- Cleanup ---
    test_backend = nullptr;
    
    printf("  Board and components destroyed\n");
    
    TEST_END();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    printf("\033[0;36m=== Board Integration Smoke Test (Stage 3.2, simplified) ===\033[0m\n");
    
    test_board_smoke();
    
    printf("\n\033[0;36m=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", \033[41;97m %d FAILED \033[0m\033[0;36m", tests_failed);
    }
    printf(" ===\033[0m\n\n");
    
    return tests_failed > 0 ? 1 : 0;
}
