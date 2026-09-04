// Agent Mock Scenario Test — Stage 5.3
//
// Full OBSERVE → HYPOTHESIS → EXPERIMENT → ANNOTATE cycle using
// real Board + DebugAdapter + DebugBackend + AgentApi.
// No LLM — exercises the API pipeline end-to-end.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#include "agent_api.h"
#include "backend.h"
#include "debug_adapter.h"
#include "debug_target.h"
#include "events.h"
#include "options.h"
#include "disassembler.h"
#include "i8080.h"

// ---------------------------------------------------------------------------
// The debug_adapter.cpp references `g_adapter_backend` for I/O tracking.
// We provide it here (set to nullptr — no I/O tracking needed for this test).
// ---------------------------------------------------------------------------

DebugBackend *g_adapter_backend = nullptr;

// ---------------------------------------------------------------------------
// Test framework
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

#define CHECK_STR(exp, act, msg) \
        do { \
            std::string _e(exp); \
            std::string _a(act); \
            if (_e != _a) { \
                printf("  \033[41;97m FAIL \033[0m %s: expected \"%s\", got \"%s\" (line %d)\n", \
                       msg, _e.c_str(), _a.c_str(), __LINE__); \
                _test_ok = false; \
            } else { \
                printf("  \033[46;30m ok \033[0m %s = \"%s\"\n", msg, _a.c_str()); \
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
// Test program — placed at address 0x0000 (CPU start after reset)
// ---------------------------------------------------------------------------
//
// Main at 0x0000:
// 0000: LXI SP, 0xC100   ; 31 00 C1        — init stack
// 0003: MVI A, 0x05      ; 3E 05            — counter = 5
// 0005: CALL 0200h        ; CD 00 02         — call DrawPixel
// 0008: DCR A             ; 3D               — counter--
// 0009: JNZ 0005h         ; C2 05 00         — if != 0, loop
// 000C: HLT               ; 76               — done
//
// Subroutine at 0x0200:
// 0200: PUSH H            ; E5               — save HL
// 0201: MVI H, 0xC0       ; 26 C0            — HL = C000 (VRAM)
// 0203: MVI L, 0x00       ; 2E 00
// 0205: MOV M, A          ; 77               — *HL = A
// 0206: POP H             ; E1               — restore HL
// 0207: RET               ; C9               — return
//

static const uint8_t main_program[] = {
    // 0x0000: LXI SP, 0xC100
    0x31, 0x00, 0xC1,
    // 0x0003: MVI A, 0x05
    0x3E, 0x05,
    // 0x0005: CALL 0x0200
    0xCD, 0x00, 0x02,
    // 0x0008: DCR A
    0x3D,
    // 0x0009: JNZ 0x0005
    0xC2, 0x05, 0x00,
    // 0x000C: HLT
    0x76,
};

static const uint8_t subroutine[] = {
    // 0x0200: PUSH H
    0xE5,
    // 0x0201: MVI H, 0xC0
    0x26, 0xC0,
    // 0x0203: MVI L, 0x00
    0x2E, 0x00,
    // 0x0205: MOV M, A
    0x77,
    // 0x0206: POP H
    0xE1,
    // 0x0207: RET
    0xC9,
};

// Addresses for the test
static const uint16_t MAIN_ADDR  = 0x0000;
static const uint16_t SUB_ADDR   = 0x0200;
static const uint16_t CALL_ADDR  = 0x0005;  // CALL instruction in main
static const uint16_t DCR_ADDR   = 0x0008;  // After CALL returns
static const uint16_t HLT_ADDR   = 0x000C;

// ---------------------------------------------------------------------------
// Helper: write program to memory via adapter
// ---------------------------------------------------------------------------
static void writeProgram(DebugAdapter &adapter)
{
    for (size_t i = 0; i < sizeof(main_program); ++i) {
        adapter.writeMemory(static_cast<uint16_t>(MAIN_ADDR + i), main_program[i]);
    }
    for (size_t i = 0; i < sizeof(subroutine); ++i) {
        adapter.writeMemory(static_cast<uint16_t>(SUB_ADDR + i), subroutine[i]);
    }
}

// ---------------------------------------------------------------------------
// Scenario: Full OBSERVE → HYPOTHESIS → EXPERIMENT → ANNOTATE cycle
// ---------------------------------------------------------------------------

static void test_full_scenario()
{
    TEST_BEGIN("Full Agent Scenario (OBSERVE → ANNOTATE)");

    // --- Setup: disable video/sound for headless test ---
    printf("  Setting up headless Board...\n");
    Options.novideo = true;
    Options.nosound = true;
    Options.pc = 0;

    // --- Create real emulator via DebugAdapter ---
    DebugAdapter adapter;
    adapter.init();
    adapter.bindHal();

    // Create DebugBackend wrapping the adapter
    DebugBackend backend(adapter);

    // Create AgentApi wrapping the backend
    AgentApi api(backend);

    printf("  DebugAdapter + DebugBackend + AgentApi created\n");

    // --- Reset in LOADROM mode (detaches boot ROM, PC=0) ---
    backend.reset();
    backend.clearHistory();
    backend.clearActivityCounters();

    // --- Write test program to memory (boot ROM detached, RAM visible at 0) ---
    printf("  Writing test program to memory...\n");
    writeProgram(adapter);

    // Verify program is in memory
    CHECK_EQ(0x31u, (unsigned)backend.readMemory(0x0000), "LXI SP at 0000");
    CHECK_EQ(0xCDu, (unsigned)backend.readMemory(0x0005), "CALL at 0005");
    CHECK_EQ(0xE5u, (unsigned)backend.readMemory(0x0200), "PUSH H at 0200");

    // ===================================================================
    // PHASE 1: OBSERVE — gather initial state
    // ===================================================================
    printf("\n  --- Phase 1: OBSERVE ---\n");

    CpuState initial = api.getCpuState();
    CHECK_EQ(0x0000u, (unsigned)initial.pc, "PC at 0x0000");

    auto romData = api.readMemory(MAIN_ADDR, sizeof(main_program));
    CHECK_EQ(0x31u, (unsigned)romData[0], "LXI SP opcode");
    CHECK_EQ(0xCDu, (unsigned)romData[5], "CALL opcode");
    CHECK_EQ(0x76u, (unsigned)romData[12], "HLT opcode");

    auto subData = api.readMemory(SUB_ADDR, sizeof(subroutine));
    CHECK_EQ(0xE5u, (unsigned)subData[0], "PUSH H opcode");
    CHECK_EQ(0xC9u, (unsigned)subData[7], "RET opcode");

    // ===================================================================
    // PHASE 2: EXPERIMENT — step through instructions
    // ===================================================================
    printf("\n  --- Phase 2: EXPERIMENT (stepping) ---\n");

    // Step 1: LXI SP, 0xC100
    api.step();
    CpuState after1 = api.getCpuState();
    CHECK_EQ(0x0003u, (unsigned)after1.pc, "after LXI SP: PC=0003");

    // Step 2: MVI A, 0x05
    api.step();
    CpuState after2 = api.getCpuState();
    CHECK_EQ(0x0005u, (unsigned)after2.pc, "after MVI A: PC=0005");
    CHECK_EQ(0x05u, (unsigned)after2.a, "after MVI A: A=0x05");

    // Step 3: CALL 0x0200 — should jump to subroutine
    api.step();
    CpuState after3 = api.getCpuState();
    CHECK_EQ(0x0200u, (unsigned)after3.pc, "after CALL: PC=0200");

    // Step through subroutine: PUSH H, MVI H, MVI L, MOV M, POP H, RET
    api.step(); // PUSH H    at 0200
    api.step(); // MVI H,C0  at 0201
    api.step(); // MVI L,00  at 0203
    api.step(); // MOV M,A   at 0205 — writes to VRAM at C000
    api.step(); // POP H     at 0206
    api.step(); // RET       at 0207 — should return to 0x0008

    CpuState afterRet = api.getCpuState();
    CHECK_EQ(DCR_ADDR, (unsigned)afterRet.pc, "after RET: PC=0008");

    // ===================================================================
    // PHASE 3: OBSERVE — check trace and activity
    // ===================================================================
    printf("\n  --- Phase 3: OBSERVE (trace analysis) ---\n");

    auto trace = api.getExecutionTrace(1000);
    CHECK(trace.size() >= 9, "trace has at least 9 instructions");
    CHECK_EQ(0x0000u, (unsigned)trace[0].pcBefore, "trace[0] = 0000 (LXI SP)");
    CHECK_EQ(0x0005u, (unsigned)trace[2].pcBefore, "trace[2] = 0005 (CALL instr)");
    CHECK_EQ(SUB_ADDR, (unsigned)trace[3].pcBefore, "trace[3] = 0200 (CALL target)");

    // Check VRAM write — MOV M,A wrote A=0x05 to C000
    // Verify by reading memory directly (activity counters cleared)
    uint8_t vramByte = backend.readMemory(0xC000);
    CHECK_EQ(0x05u, (unsigned)vramByte, "VRAM write at C000 detected");

    printf("  Trace entries: %zu\n", trace.size());

    // ===================================================================
    // PHASE 4: HYPOTHESIS — analyze function context
    // ===================================================================
    printf("\n  --- Phase 4: HYPOTHESIS (function analysis) ---\n");

    // Analyze the subroutine at 0x0200
    FunctionContext subCtx = api.getFunctionContext(SUB_ADDR);
    CHECK_EQ(SUB_ADDR, (unsigned)subCtx.address, "function at 0200");
    CHECK(subCtx.instructions.size() >= 6, "subroutine has >= 6 instructions");
    CHECK(subCtx.size > 0, "function size > 0");
    CHECK_STR("sub_0200", subCtx.name, "auto-name");

    // Check that VRAM source is unknown without trace
    CHECK(subCtx.vramSource == DataSource::Unknown, "VRAM source unknown without trace");

    // Analyze the main program at 0x0000
    FunctionContext mainCtx = api.getFunctionContext(MAIN_ADDR);
    CHECK_EQ(MAIN_ADDR, (unsigned)mainCtx.address, "main at 0000");

    // Main should have CALL 0x0200 as a callee
    bool foundCallee = false;
    for (uint16_t addr : mainCtx.callees) {
        if (addr == SUB_ADDR) foundCallee = true;
    }
    CHECK(foundCallee, "main calls 0x0200 (callee detected)");

    // ===================================================================
    // PHASE 5: ANNOTATE — apply discovered knowledge
    // ===================================================================
    printf("\n  --- Phase 5: ANNOTATE ---\n");

    // Create function at 0x0200
    auto created = api.createFunction(SUB_ADDR);
    CHECK(created.success, "function created at 0200");

    // Rename it based on our hypothesis (it writes to VRAM)
    auto renamed = api.renameFunction(SUB_ADDR, "DrawPixel");
    CHECK(renamed.success, "renamed to DrawPixel");

    // Add a comment
    auto commented = api.setFunctionComment(SUB_ADDR, "Writes A to VRAM C000");
    CHECK(commented.success, "comment set");

    // Create function at 0x0000 (main loop)
    api.createFunction(MAIN_ADDR);
    api.renameFunction(MAIN_ADDR, "MainLoop");
    api.setFunctionComment(MAIN_ADDR, "Main loop: calls DrawPixel 5 times");

    // Add a label for VRAM base
    auto labelR = api.addLabel(0xC000, "VRAM_BASE");
    CHECK(labelR.success, "VRAM_BASE label created");

    // Verify annotations via getFunctionContext
    FunctionContext annotated = api.getFunctionContext(SUB_ADDR);
    CHECK_STR("DrawPixel", annotated.name, "name persisted");
    CHECK_STR("Writes A to VRAM C000", annotated.comment, "comment persisted");

    // ===================================================================
    // PHASE 6: Verify log
    // ===================================================================
    printf("\n  --- Phase 6: Verify operation log ---\n");

    auto logEntries = api.log().entries();
    CHECK(logEntries.size() > 10, "log has many entries");

    // Check that key operations were logged
    bool foundStep = false, foundCtx = false, foundAnnot = false;
    for (const auto &e : logEntries) {
        if (e.tool == "step") foundStep = true;
        if (e.tool == "getFunctionContext") foundCtx = true;
        if (e.tool == "applyAnnotation" || e.tool == "renameFunction") foundAnnot = true;
    }
    CHECK(foundStep, "step was logged");
    CHECK(foundCtx, "getFunctionContext was logged");
    CHECK(foundAnnot, "annotation was logged");

    printf("  Total log entries: %zu\n", logEntries.size());

    // ===================================================================
    // PHASE 7: Test breakpoint + run cycle via AgentApi
    // ===================================================================
    printf("\n  --- Phase 7: Breakpoint + run cycle ---\n");

    // Reset to start
    backend.reset();
    writeProgram(adapter);
    backend.clearHistory();
    backend.clearActivityCounters();

    // Set breakpoint at subroutine
    auto bpR = api.setBreakpoint(SUB_ADDR);
    CHECK(bpR.success, "breakpoint set at 0200");

    // List breakpoints
    auto bps = api.listBreakpoints();
    CHECK_EQ(1u, (unsigned)bps.size(), "1 breakpoint listed");

    // Run — should stop at breakpoint (executes LXI SP, MVI A, then hits CALL 0200)
    backend.run();

    CpuState atBp = api.getCpuState();
    printf("  Stopped at PC=%04X\n", atBp.pc);
    CHECK_EQ(SUB_ADDR, (unsigned)atBp.pc, "stopped at 0200 (breakpoint)");
    CHECK_EQ((int)StopReason::Breakpoint, (int)backend.getStopReason(),
             "stopReason == Breakpoint");

    // Clear breakpoint
    api.clearBreakpoint(SUB_ADDR);
    auto bpsAfter = api.listBreakpoints();
    CHECK_EQ(0u, (unsigned)bpsAfter.size(), "breakpoints cleared");

    // ===================================================================
    // PHASE 8: Test executeTrace (real execution experiment)
    // ===================================================================
    printf("\n  --- Phase 8: executeTrace analysis ---\n");

    // Step to function entry (PC should be at 0x0200 after Phase 7 breakpoint)
    // Reset and step through to reach the subroutine
    backend.reset();
    writeProgram(adapter);
    backend.clearHistory();

    // Step to CALL 0x0200
    api.step(); // LXI SP    at 0000
    api.step(); // MVI A     at 0003
    api.step(); // CALL 0200 at 0005 → now at 0200

    // Execute trace directly through backend (no emulation thread needed)
    IDebugBackend::TraceExecutionParams params;
    params.startPc = SUB_ADDR;
    params.maxInstructions = 10000;
    params.stopOnRet = true;
    params.stopOnCallerReturn = false;

    auto traceExec = backend.executeTrace(params);
    CHECK(traceExec.instructionsExecuted > 0, "subroutine executed");
    CHECK(traceExec.exitReason == ExitReason::Ret, "exit reason Ret");
    CHECK_EQ(0x0207u, (unsigned)traceExec.exitPc, "exit at RET (0207)");

    printf("  Instructions executed: %u\n", traceExec.instructionsExecuted);
    printf("  SP entry: %04X, SP exit: %04X\n", traceExec.entrySp, traceExec.exitSp);

    // ===================================================================
    // Cleanup
    // ===================================================================
    printf("\n  --- Cleanup ---\n");
    g_adapter_backend = nullptr;

    TEST_END();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    printf("\n\033[1;33m========================================\033[0m\n");
    printf("\033[1;33m  Agent Mock Scenario Test — Stage 5.3\033[0m\n");
    printf("\033[1;33m========================================\033[0m\n");

    test_full_scenario();

    printf("\n\033[1;33m========================================\033[0m\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("  (\033[1;31m%d FAILED\033[0m)", tests_failed);
    }
    printf("\n\033[1;33m========================================\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
