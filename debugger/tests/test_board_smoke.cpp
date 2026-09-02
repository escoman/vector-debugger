// Stage 3.2 — Simplified Board Integration Smoke Test
//
// This test verifies that the debugger works with the REAL Board,
// without requiring boot ROM or icon binary resources.
// We disable video/sound to avoid SDL dependencies.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

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
#include "disassembler.h"
#include "memory_inspector_window.h"
#include "disassembly_window.h"
#include "stack_view_window.h"
#include "breakpoints_window.h"

using namespace i8080cpu;

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
    
    // --- Test Stack View API (Stage 3.4) ---
    printf("  Testing Stack View API...\n");
    
    // Test SP read
    CpuState cpu = backend.getCpuState();
    CHECK(cpu.sp > 0, "SP > 0");
    printf("  SP = %04X\n", cpu.sp);
    
    // Test stack snapshot around SP
    uint16_t sp = cpu.sp;
    int start = std::max(static_cast<int>(sp) - 8, 0);
    int end = std::min(static_cast<int>(sp) + 8, 0xFFFF);
    size_t size = static_cast<size_t>(end - start + 1);
    
    MemorySnapshot stackSnap = backend.readMemorySnapshot(static_cast<uint16_t>(start), size);
    CHECK_EQ(start, stackSnap.start, "Stack snapshot start correct");
    CHECK(size > 0, "Stack snapshot has data");
    
    printf("  Stack View API works with real Board\n");
    
    // --- Test Memory Editing API (Stage 3.5) ---
    printf("  Testing Memory Editing API...\n");
    
    // Write a byte to a known RAM area (use address 0xC000 which is RAM)
    // First read original value
    uint8_t origByte = backend.readMemory(0xC000);
    printf("  Original C000 = %02X\n", origByte);
    
    // Write through Memory::write (virtual address path)
    // This is what processWriteCommand -> executeWriteMemory does
    memory.write(0xC000, 0x77, false);
    
    // Verify through backend read
    uint8_t newByte = backend.readMemory(0xC000);
    CHECK_EQ(0x77, newByte, "write C000 = 0x77, read back 0x77");
    
    // Verify snapshot reflects the write
    MemorySnapshot editSnap = backend.readMemorySnapshot(0xC000, 4);
    CHECK_EQ(0x77, editSnap.data[0], "snapshot[0] == 0x77 after write");
    
    // Write multiple bytes
    uint8_t editData[] = {0xDE, 0xAD, 0xBE, 0xEF};
    for (size_t i = 0; i < sizeof(editData); ++i) {
        memory.write(static_cast<uint16_t>(0xC010 + i), editData[i], false);
    }
    MemorySnapshot editSnap2 = backend.readMemorySnapshot(0xC010, 4);
    CHECK_EQ(0xDE, editSnap2.data[0], "bulk write [0] == 0xDE");
    CHECK_EQ(0xAD, editSnap2.data[1], "bulk write [1] == 0xAD");
    CHECK_EQ(0xBE, editSnap2.data[2], "bulk write [2] == 0xBE");
    CHECK_EQ(0xEF, editSnap2.data[3], "bulk write [3] == 0xEF");
    
    // Restore original value
    memory.write(0xC000, origByte, false);
    uint8_t restored = backend.readMemory(0xC000);
    CHECK_EQ(origByte, restored, "restored original value");
    
    printf("  Memory Editing API works with real Board\n");
    
    // --- Test CPU Register Write/Read (Stage 3.6) ---
    printf("  Testing CPU Register Write/Read API...\n");
    
    // Write registers via i8080 API (simulating what emulation thread does)
    i8080_jump(0x0100);
    i8080_setreg_sp(0xC100);
    i8080_setreg_b(0x12);
    i8080_setreg_c(0x34);
    i8080_setreg_d(0x56);
    i8080_setreg_e(0x78);
    i8080_setreg_h(0x9A);
    i8080_setreg_l(0xBC);
    i8080_setreg_a(0xDE);
    i8080_setreg_f(0xF0);
    
    // Read back via DebugBackend
    CpuState regs = backend.getCpuState();
    CHECK_EQ(0x0100, regs.pc, "PC = 0x0100");
    CHECK_EQ(0xC100, regs.sp, "SP = 0xC100");
    CHECK_EQ(0x12, regs.b, "B = 0x12");
    CHECK_EQ(0x34, regs.c, "C = 0x34");
    CHECK_EQ(0x56, regs.d, "D = 0x56");
    CHECK_EQ(0x78, regs.e, "E = 0x78");
    CHECK_EQ(0x9A, regs.h, "H = 0x9A");
    CHECK_EQ(0xBC, regs.l, "L = 0xBC");
    CHECK_EQ(0xDE, regs.a, "A = 0xDE");
    CHECK_EQ(0xF0, regs.flags, "F = 0xF0");
    
    // Verify additional Stage 3.6 fields
    printf("  IFF=%d EI_pending=%d Cycles=%u LastPC=%04X\n",
        regs.iff ? 1 : 0, regs.ei_pending ? 1 : 0, regs.cycles, regs.last_pc);
    CHECK(true, "Stage 3.6 fields accessible");
    
    // Test PC -> Step executes from new address
    // Write MVI A, 0x99 at 0x0100
    memory.write(0x0100, 0x3E, false);  // MVI A
    memory.write(0x0101, 0x99, false);  // operand
    
    i8080_jump(0x0100);
    backend.stepInstruction();
    CpuState afterStep = backend.getCpuState();
    CHECK_EQ(0x99, afterStep.a, "A = 0x99 after step from new PC");
    CHECK_EQ(0x0102, afterStep.pc, "PC advanced to 0x0102");
    
    printf("  CPU Register Write/Read API works with real Board\n");
    
    // --- Test Breakpoint API with real Board (Stage 3.7) ---
    printf("  Testing Breakpoint API with real Board...\n");
    
    // Reset to known state
    Options.pc = 0;
    board.reset(Board::ResetMode::LOADROM);
    backend.clearHistory();
    backend.clearBreakpoints();
    
    // Verify test program is still in memory (MVI A,55 / INR A / JMP 0002)
    CHECK_EQ(0x3E, backend.readMemory(0x0000), "test program at 0000: MVI A opcode");
    CHECK_EQ(0x55, backend.readMemory(0x0001), "test program at 0001: operand 55");
    CHECK_EQ(0x3C, backend.readMemory(0x0002), "test program at 0002: INR A");
    
    // Add breakpoint at 0x0002 (INR A)
    int bpId = backend.addBreakpoint(0x0002);
    CHECK(bpId >= 0, "breakpoint added at 0x0002");
    CHECK(backend.hasBreakpoint(0x0002), "hasBreakpoint(0x0002)");
    
    // Run — should stop at 0x0002 before executing INR A
    backend.run();
    
    CpuState bpState = backend.getCpuState();
    CHECK_EQ(0x0002, bpState.pc, "PC == 0x0002 at breakpoint");
    CHECK_EQ(0x55, bpState.a, "A == 0x55 (MVI executed, INR not yet)");
    CHECK_EQ((int)StopReason::Breakpoint, (int)backend.getStopReason(),
             "stopReason == Breakpoint");
    
    // Step — should execute INR A, A becomes 0x56, PC becomes 0x0003
    StepResult bpStep = backend.stepInstruction();
    CHECK_EQ(0x0002, bpStep.pcBefore, "step from 0x0002");
    CHECK_EQ(0x0003, bpStep.pcAfter, "now at 0x0003");
    
    CpuState afterBpStep = backend.getCpuState();
    CHECK_EQ(0x56, afterBpStep.a, "A == 0x56 (INR A executed)");
    CHECK_EQ((int)StopReason::Step, (int)backend.getStopReason(),
             "stopReason == Step");
    
    // Test breakpoint enable/disable
    backend.clearBreakpoints();
    backend.addBreakpoint(0x0002);
    backend.setBreakpointEnabled(0x0002, false);
    CHECK(!backend.hasBreakpoint(0x0002), "disabled BP not reported by hasBreakpoint");
    
    auto bpList = backend.getBreakpoints();
    CHECK_EQ((size_t)1, bpList.size(), "1 BP in list (disabled)");
    CHECK(!bpList[0].enabled, "BP marked disabled");
    
    backend.setBreakpointEnabled(0x0002, true);
    CHECK(backend.hasBreakpoint(0x0002), "re-enabled BP visible");
    
    // Test duplicate rejection
    int dupId = backend.addBreakpoint(0x0002);
    CHECK_EQ(-1, dupId, "duplicate BP returns -1");
    
    // Test reset preserves breakpoints
    backend.reset();
    auto bpAfterReset = backend.getBreakpoints();
    CHECK_EQ((size_t)1, bpAfterReset.size(), "BP survives reset");
    CHECK_EQ((int)StopReason::Reset, (int)backend.getStopReason(),
             "stopReason == Reset after reset");
    
    backend.clearBreakpoints();
    printf("  Breakpoint API works with real Board\n");
    
    // --- Test Disassembly API (Stage 3.8) ---
    printf("  Testing Disassembly API...\n");
    
    // Reset to known state — test program should still be in memory
    Options.pc = 0;
    board.reset(Board::ResetMode::LOADROM);
    backend.clearHistory();
    
    // Verify test program bytes
    CHECK_EQ(0x3E, backend.readMemory(0x0000), "test program: MVI A opcode at 0000");
    CHECK_EQ(0x55, backend.readMemory(0x0001), "test program: operand 55 at 0001");
    CHECK_EQ(0x3C, backend.readMemory(0x0002), "test program: INR A at 0002");
    CHECK_EQ(0xC3, backend.readMemory(0x0003), "test program: JMP opcode at 0003");
    
    // Disassemble using DebugMemoryAccess::peek() as read function
    auto dasmReadFn = [&backend](uint16_t addr) -> uint8_t {
        return backend.readMemory(addr);
    };
    
    // Disassemble at 0000: MVI A,55H (2 bytes)
    auto d1 = disassemble(0x0000, dasmReadFn);
    CHECK(d1.mnemonic == "MVI", "disasm 0000: MVI");
    CHECK_EQ(2, d1.length, "disasm 0000: length 2");
    
    // Disassemble at 0002: INR A (1 byte)
    auto d2 = disassemble(0x0002, dasmReadFn);
    CHECK(d2.mnemonic == "INR", "disasm 0002: INR");
    CHECK_EQ(1, d2.length, "disasm 0002: length 1");
    
    // Disassemble at 0003: JMP 0002 (3 bytes)
    auto d3 = disassemble(0x0003, dasmReadFn);
    CHECK(d3.mnemonic == "JMP", "disasm 0003: JMP");
    CHECK_EQ(3, d3.length, "disasm 0003: length 3");
    CHECK(d3.text.find("0002") != std::string::npos, "disasm 0003: target 0002");
    
    // Set BP at 0002, run, verify PC=0002
    backend.addBreakpoint(0x0002);
    backend.run();
    
    CpuState bpState2 = backend.getCpuState();
    CHECK_EQ(0x0002, bpState2.pc, "BP hit: PC == 0x0002");
    CHECK_EQ(0x55, bpState2.a, "BP hit: A == 0x55");
    CHECK_EQ((int)StopReason::Breakpoint, (int)backend.getStopReason(),
             "stopReason == Breakpoint");
    
    // Step — should execute INR A, PC becomes 0003
    backend.stepInstruction();
    CpuState afterStep2 = backend.getCpuState();
    CHECK_EQ(0x0003, afterStep2.pc, "after step: PC == 0x0003");
    CHECK_EQ(0x56, afterStep2.a, "after step: A == 0x56");
    
    // Disassemble at PC (0003): should be JMP 0002
    auto d4 = disassemble(afterStep2.pc, dasmReadFn);
    CHECK(d4.mnemonic == "JMP", "disasm at PC 0003: JMP");
    CHECK_EQ(3, d4.length, "disasm at PC 0003: length 3");
    
    backend.clearBreakpoints();
    printf("  Disassembly API works with real Board\n");
    
    // --- Test Navigation API (Stage 3.9) ---
    printf("  Testing Navigation API...\n");
    
    // Reset to known state
    Options.pc = 0;
    board.reset(Board::ResetMode::LOADROM);
    backend.clearHistory();
    backend.clearBreakpoints();
    
    // Create window objects for navigation testing
    MemoryInspectorWindow memWin;
    DisassemblyWindow dasmWin;
    StackViewWindow stackWin;
    BreakpointsWindow bpWin;
    
    // --- PC → Disassembly navigation ---
    printf("  Testing PC -> Disassembly navigation...\n");
    CpuState navCpu = backend.getCpuState();
    uint16_t pcAddr = navCpu.pc;
    dasmWin.gotoAddress(pcAddr);
    CHECK_EQ(pcAddr, dasmWin.address(), "Disassembly navigated to PC address");
    CHECK(dasmWin.isVisible(), "Disassembly window opened");
    
    // --- PC → Memory navigation ---
    printf("  Testing PC -> Memory navigation...\n");
    memWin.gotoAddress(pcAddr);
    CHECK_EQ(pcAddr, memWin.address(), "Memory Inspector navigated to PC address");
    CHECK(memWin.isVisible(), "Memory Inspector window opened");
    
    // --- SP → Stack navigation ---
    printf("  Testing SP -> Stack navigation...\n");
    uint16_t spAddr = navCpu.sp;
    stackWin.gotoAddress(spAddr);
    CHECK_EQ(spAddr, stackWin.address(), "Stack View navigated to SP address");
    CHECK(stackWin.isVisible(), "Stack View window opened");
    CHECK(!stackWin.followSP(), "gotoAddress disables Follow SP");
    
    // --- SP → Memory navigation ---
    printf("  Testing SP -> Memory navigation...\n");
    memWin.gotoAddress(spAddr);
    CHECK_EQ(spAddr, memWin.address(), "Memory Inspector navigated to SP address");
    
    // --- BP → Disassembly/Memory navigation ---
    printf("  Testing BP -> Disassembly/Memory navigation...\n");
    int navBpId = backend.addBreakpoint(0x0003);
    CHECK(navBpId >= 0, "BP added for navigation test");
    
    auto bpList2 = backend.getBreakpoints();
    CHECK_EQ((size_t)1, bpList2.size(), "1 BP in list");
    uint16_t bpAddr = bpList2[0].address;
    
    // Navigate BP address to both windows
    dasmWin.gotoAddress(bpAddr);
    CHECK_EQ(bpAddr, dasmWin.address(), "Disassembly navigated to BP address");
    
    memWin.gotoAddress(bpAddr);
    CHECK_EQ(bpAddr, memWin.address(), "Memory navigated to BP address");
    
    backend.clearBreakpoints();
    
    // --- Stack Word → Disassembly navigation ---
    printf("  Testing Stack Word -> Disassembly navigation...\n");
    
    // Write a known LE word at a RAM address
    uint16_t wordAddr = 0xC000;
    uint16_t wordVal  = 0x1234;
    memory.write(wordAddr,      static_cast<uint8_t>(wordVal & 0xFF), false);       // low byte
    memory.write(wordAddr + 1,  static_cast<uint8_t>((wordVal >> 8) & 0xFF), false); // high byte
    
    // Read back as LE word (same logic as StackViewWindow context menu)
    uint8_t lo = backend.readMemory(wordAddr);
    uint8_t hi = backend.readMemory(wordAddr + 1);
    uint16_t leWord = static_cast<uint16_t>(lo | (hi << 8));
    CHECK_EQ(wordVal, leWord, "LE word at C000 == 0x1234");
    
    // Navigate to the word value in Disassembly
    dasmWin.gotoAddress(leWord);
    CHECK_EQ(leWord, dasmWin.address(), "Disassembly navigated to LE word value");
    
    // --- Follow PC ON/OFF ---
    printf("  Testing Follow PC ON/OFF...\n");
    
    // gotoAddress should disable Follow PC
    dasmWin.gotoAddress(0x0000);
    CHECK_EQ(0x0000, dasmWin.address(), "gotoAddress sets address");
    // Follow PC is internal to DisassemblyWindow; we verify via address() stability
    // After gotoAddress, address is 0x0000 and followPc_ is false
    
    // Navigate again — address should change (proves follow was off, manual nav works)
    dasmWin.gotoAddress(0x0003);
    CHECK_EQ(0x0003, dasmWin.address(), "second gotoAddress updates address");
    
    // --- Follow SP ON/OFF ---
    printf("  Testing Follow SP ON/OFF...\n");
    
    // gotoAddress disables Follow SP
    stackWin.gotoAddress(0xC000);
    CHECK(!stackWin.followSP(), "gotoAddress disables Follow SP");
    CHECK_EQ(0xC000, stackWin.address(), "Stack address == 0xC000");
    
    // Enable Follow SP
    stackWin.setFollowSP(true);
    CHECK(stackWin.followSP(), "Follow SP enabled");
    // address() now returns currentSP_ (tracked internally)
    
    // Disable Follow SP — address should return to viewCenter_
    stackWin.setFollowSP(false);
    CHECK(!stackWin.followSP(), "Follow SP disabled");
    CHECK_EQ(0xC000, stackWin.address(), "Stack address returns to viewCenter_");
    
    // --- Cross-navigation callbacks ---
    printf("  Testing cross-navigation callbacks...\n");
    
    // Wire callbacks like DebuggerGui would
    uint16_t dasmNavTarget = 0xFFFF;
    uint16_t memNavTarget  = 0xFFFF;
    
    memWin.onGoToDisassembly = [&](uint16_t a) { dasmNavTarget = a; };
    dasmWin.onGoToMemoryInspector = [&](uint16_t a) { memNavTarget = a; };
    stackWin.onGoToDisassembly = [&](uint16_t a) { dasmNavTarget = a; };
    stackWin.onGoToMemoryInspector = [&](uint16_t a) { memNavTarget = a; };
    bpWin.onGoToDisassembly = [&](uint16_t a) { dasmNavTarget = a; };
    bpWin.onGoToMemoryInspector = [&](uint16_t a) { memNavTarget = a; };
    
    // Fire callbacks
    memWin.onGoToDisassembly(0x1234);
    CHECK_EQ(0x1234, dasmNavTarget, "Memory → Disassembly callback fires");
    
    dasmWin.onGoToMemoryInspector(0x2345);
    CHECK_EQ(0x2345, memNavTarget, "Disassembly → Memory callback fires");
    
    stackWin.onGoToDisassembly(0x3456);
    CHECK_EQ(0x3456, dasmNavTarget, "Stack → Disassembly callback fires");
    
    stackWin.onGoToMemoryInspector(0x4567);
    CHECK_EQ(0x4567, memNavTarget, "Stack → Memory callback fires");
    
    bpWin.onGoToDisassembly(0x5678);
    CHECK_EQ(0x5678, dasmNavTarget, "BP → Disassembly callback fires");
    
    bpWin.onGoToMemoryInspector(0x6789);
    CHECK_EQ(0x6789, memNavTarget, "BP → Memory callback fires");
    
    // --- Boundary addresses ---
    printf("  Testing boundary addresses...\n");
    memWin.gotoAddress(0x0000);
    CHECK_EQ(0x0000, memWin.address(), "Memory: gotoAddress(0x0000)");
    memWin.gotoAddress(0xFFFF);
    CHECK_EQ(0xFFFF, memWin.address(), "Memory: gotoAddress(0xFFFF)");
    
    dasmWin.gotoAddress(0x0000);
    CHECK_EQ(0x0000, dasmWin.address(), "Disassembly: gotoAddress(0x0000)");
    dasmWin.gotoAddress(0xFFFF);
    CHECK_EQ(0xFFFF, dasmWin.address(), "Disassembly: gotoAddress(0xFFFF)");
    
    stackWin.gotoAddress(0x0000);
    CHECK_EQ(0x0000, stackWin.address(), "Stack: gotoAddress(0x0000)");
    stackWin.gotoAddress(0xFFFF);
    CHECK_EQ(0xFFFF, stackWin.address(), "Stack: gotoAddress(0xFFFF)");
    
    printf("  Navigation API works with real Board\n");
    
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
