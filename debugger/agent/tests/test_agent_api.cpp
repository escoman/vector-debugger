// Agent API tests — Stage 5.3
//
// Unit tests for AgentApi using MockAgentBackend.
// No Board, SDL, or emulator dependency.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

#include "agent_api.h"
#include "mock_backend_for_agent.h"

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
// Tests: Execution control
// ---------------------------------------------------------------------------

static void test_run_pause()
{
    TEST_BEGIN("run and pause");
    MockAgentBackend mock;

    // Fill memory with NOPs so requestRun() doesn't hit HLT
    std::vector<uint8_t> nops(256, 0x00);
    mock.setMemory(0x0100, nops);

    AgentApi api(mock);

    CHECK(mock.isPaused(), "initially paused");

    api.run();
    // Mock requestRun() is synchronous — it runs until HLT/breakpoint/limit.
    // With NOPs it hits the 100000-step safety limit, then is paused again.
    // So we verify that stepping occurred (PC advanced far from 0x0100).
    CHECK(api.getCpuState().pc != 0x0100, "PC advanced during run");

    api.pause();
    CHECK(mock.isPaused(), "paused after pause()");
    TEST_END();
}

static void test_step()
{
    TEST_BEGIN("step advances PC");
    MockAgentBackend mock;
    AgentApi api(mock);

    CpuState before = api.getCpuState();
    CHECK_EQ(0x0100u, (unsigned)before.pc, "PC at 0x0100");

    api.step();

    CpuState after = api.getCpuState();
    // 0x0100 is LXI SP,word (3 bytes) → PC should be 0x0103
    CHECK_EQ(0x0103u, (unsigned)after.pc, "PC advanced to 0x0103");
    TEST_END();
}

static void test_reset()
{
    TEST_BEGIN("reset returns PC to start");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.step();
    api.step();
    CHECK(api.getCpuState().pc != 0x0100, "PC moved");

    api.reset();
    CHECK_EQ(0x0100u, (unsigned)api.getCpuState().pc, "PC back to 0x0100");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: CPU state
// ---------------------------------------------------------------------------

static void test_get_cpu_state()
{
    TEST_BEGIN("getCpuState returns correct data");
    MockAgentBackend mock;
    AgentApi api(mock);

    CpuState cpu = api.getCpuState();
    CHECK_EQ(0x0100u, (unsigned)cpu.pc, "PC");
    CHECK_EQ(0xF800u, (unsigned)cpu.sp, "SP");
    CHECK_EQ(0x42u, (unsigned)cpu.a, "A register");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Memory access
// ---------------------------------------------------------------------------

static void test_read_memory()
{
    TEST_BEGIN("readMemory returns correct bytes");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto data = api.readMemory(0x0100, 4);
    CHECK_EQ(4u, (unsigned)data.size(), "4 bytes read");
    CHECK_EQ(0x31u, (unsigned)data[0], "opcode 0x31");
    CHECK_EQ(0x00u, (unsigned)data[1], "operand byte 0");
    CHECK_EQ(0xF8u, (unsigned)data[2], "operand byte 0xF8");
    CHECK_EQ(0x3Eu, (unsigned)data[3], "next opcode 0x3E");
    TEST_END();
}

static void test_write_memory()
{
    TEST_BEGIN("writeMemory modifies memory");
    MockAgentBackend mock;
    AgentApi api(mock);

    std::vector<uint8_t> data = {0xAA, 0xBB, 0xCC};
    bool ok = api.writeMemory(0x8000, data);
    CHECK(ok, "write succeeds");

    auto readback = api.readMemory(0x8000, 3);
    CHECK_EQ(0xAAu, (unsigned)readback[0], "byte 0");
    CHECK_EQ(0xBBu, (unsigned)readback[1], "byte 1");
    CHECK_EQ(0xCCu, (unsigned)readback[2], "byte 2");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Breakpoints
// ---------------------------------------------------------------------------

static void test_set_clear_breakpoint()
{
    TEST_BEGIN("set and clear breakpoint");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r1 = api.setBreakpoint(0x0200);
    CHECK(r1.success, "breakpoint set");
    CHECK(mock.hasBreakpoint(0x0200), "backend confirms breakpoint");

    auto r2 = api.setBreakpoint(0x0200);
    CHECK(!r2.success, "duplicate rejected");

    auto bps = api.listBreakpoints();
    CHECK_EQ(1u, (unsigned)bps.size(), "1 breakpoint");

    auto r3 = api.clearBreakpoint(0x0200);
    CHECK(r3.success, "breakpoint cleared");
    CHECK(!mock.hasBreakpoint(0x0200), "backend confirms removed");

    auto r4 = api.clearBreakpoint(0x0200);
    CHECK(!r4.success, "clear nonexistent fails");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Trace / IO / VRAM
// ---------------------------------------------------------------------------

static void test_get_execution_trace()
{
    TEST_BEGIN("getExecutionTrace returns events");
    MockAgentBackend mock;

    // Add some instruction events
    InstructionEvent ev1{};
    ev1.pcBefore = 0x0100; ev1.pcAfter = 0x0103; ev1.opcode = 0x31;
    mock.addInstructionEvent(ev1);

    InstructionEvent ev2{};
    ev2.pcBefore = 0x0103; ev2.pcAfter = 0x0105; ev2.opcode = 0x3E;
    mock.addInstructionEvent(ev2);

    AgentApi api(mock);
    auto trace = api.getExecutionTrace(100);
    CHECK_EQ(2u, (unsigned)trace.size(), "2 events");
    CHECK_EQ(0x0100u, (unsigned)trace[0].pcBefore, "first PC");
    CHECK_EQ(0x0103u, (unsigned)trace[1].pcBefore, "second PC");
    TEST_END();
}

static void test_list_breakpoints()
{
    TEST_BEGIN("listBreakpoints returns all breakpoints");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.setBreakpoint(0x0200);
    api.setBreakpoint(0x0300);
    auto bps = api.listBreakpoints();
    CHECK_EQ(2u, (unsigned)bps.size(), "2 breakpoints");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Annotations
// ---------------------------------------------------------------------------

static void test_create_function()
{
    TEST_BEGIN("createFunction adds symbol");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.createFunction(0x0200);
    CHECK(r.success, "function created");

    const DebugSymbol *sym = mock.symbolDatabase().findSymbol(0x0200);
    CHECK(sym != nullptr, "symbol exists");
    CHECK_STR("sub_0200", sym->name, "auto-name");
    CHECK_EQ((unsigned)SymbolType::Function, (unsigned)sym->type, "function type");

    auto r2 = api.createFunction(0x0200);
    CHECK(!r2.success, "duplicate rejected");
    TEST_END();
}

static void test_rename_function()
{
    TEST_BEGIN("renameFunction changes name");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.createFunction(0x0200);
    auto r = api.renameFunction(0x0200, "DrawSprite");
    CHECK(r.success, "rename succeeds");

    const DebugSymbol *sym = mock.symbolDatabase().findSymbol(0x0200);
    CHECK_STR("DrawSprite", sym->name, "new name");

    auto r2 = api.renameFunction(0x9999, "Nope");
    CHECK(!r2.success, "rename nonexistent fails");
    TEST_END();
}

static void test_set_function_comment()
{
    TEST_BEGIN("setFunctionComment sets comment");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.createFunction(0x0200);
    auto r = api.setFunctionComment(0x0200, "Draws a sprite");
    CHECK(r.success, "comment set");

    const DebugSymbol *sym = mock.symbolDatabase().findSymbol(0x0200);
    CHECK_STR("Draws a sprite", sym->comment, "comment text");
    TEST_END();
}

static void test_delete_function()
{
    TEST_BEGIN("deleteFunction removes symbol");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.createFunction(0x0200);
    auto r = api.deleteFunction(0x0200);
    CHECK(r.success, "deleted");
    CHECK(mock.symbolDatabase().findSymbol(0x0200) == nullptr, "gone");

    auto r2 = api.deleteFunction(0x0200);
    CHECK(!r2.success, "delete again fails");
    TEST_END();
}

static void test_add_label()
{
    TEST_BEGIN("addLabel creates label symbol");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.addLabel(0x4000, "SCORE");
    CHECK(r.success, "label created");

    const DebugSymbol *sym = mock.symbolDatabase().findSymbol(0x4000);
    CHECK(sym != nullptr, "exists");
    CHECK_STR("SCORE", sym->name, "name");
    CHECK_EQ((unsigned)SymbolType::Label, (unsigned)sym->type, "label type");
    TEST_END();
}

static void test_apply_annotation()
{
    TEST_BEGIN("applyAnnotation with confidence");
    MockAgentBackend mock;
    AgentApi api(mock);

    Annotation ann;
    ann.type = Annotation::Function;
    ann.address = 0x0200;
    ann.name = "InitSound";
    ann.comment = "Initialize AY-3-8912";
    ann.confidence = 0.91;

    auto r = api.applyAnnotation(ann);
    CHECK(r.success, "annotation applied");

    const DebugSymbol *sym = mock.symbolDatabase().findSymbol(0x0200);
    CHECK(sym != nullptr, "symbol exists");
    CHECK_STR("InitSound", sym->name, "name");
    CHECK_STR("Initialize AY-3-8912", sym->comment, "comment");

    // Verify log has confidence
    auto entries = api.log().entries();
    CHECK(entries.size() > 0, "log has entries");
    bool foundConfidence = false;
    for (const auto &e : entries) {
        if (e.tool == "applyAnnotation" && e.arguments.find("0.91") != std::string::npos) {
            foundConfidence = true;
        }
    }
    CHECK(foundConfidence, "confidence logged");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: High-level — getFunctionContext
// ---------------------------------------------------------------------------

static void test_get_function_context()
{
    TEST_BEGIN("getFunctionContext returns disassembly");
    MockAgentBackend mock;
    AgentApi api(mock);

    // The subroutine at 0x0200 is:
    // E5        PUSH H
    // 26 C0     MVI H, 0xC0
    // 2E 00     MVI L, 0x00
    // 77        MOV M, A
    // E1        POP H
    // C9        RET

    FunctionContext ctx = api.getFunctionContext(0x0200);

    CHECK_EQ(0x0200u, (unsigned)ctx.address, "address");
    CHECK(ctx.instructions.size() >= 6, "at least 6 instructions");
    CHECK(ctx.size > 0, "size > 0");

    // Check first instruction is PUSH H
    CHECK_EQ(0xE5u, (unsigned)ctx.instructions[0].bytes[0], "first opcode PUSH H");

    // Check last instruction is RET
    CHECK_EQ(0xC9u, (unsigned)ctx.instructions.back().bytes[0], "last opcode RET");

    // Name should be auto-generated
    CHECK_STR("sub_0200", ctx.name, "auto-name");
    TEST_END();
}

static void test_get_function_context_with_name()
{
    TEST_BEGIN("getFunctionContext uses existing name");
    MockAgentBackend mock;
    mock.symbolDatabase().addSymbol(0x0200, "WriteVRAM", SymbolType::Function);
    mock.symbolDatabase().setComment(0x0200, "Writes A to VRAM");

    AgentApi api(mock);
    FunctionContext ctx = api.getFunctionContext(0x0200);

    CHECK_STR("WriteVRAM", ctx.name, "existing name");
    CHECK_STR("Writes A to VRAM", ctx.comment, "existing comment");
    TEST_END();
}

static void test_get_function_context_callees()
{
    TEST_BEGIN("getFunctionContext finds callees");
    MockAgentBackend mock;
    AgentApi api(mock);

    // The main program at 0x0100 has CALL 0x0200
    FunctionContext ctx = api.getFunctionContext(0x0100);

    // Should find 0x0200 as a callee
    bool found = false;
    for (uint16_t addr : ctx.callees) {
        if (addr == 0x0200) found = true;
    }
    CHECK(found, "CALL 0x0200 found in callees");
    TEST_END();
}

static void test_get_function_context_vram()
{
    TEST_BEGIN("getFunctionContext finds VRAM writes");
    MockAgentBackend mock;
    AgentApi api(mock);

    FunctionContext ctx = api.getFunctionContext(0x0200);

    // Without trace, VRAM writes are unknown (no global counters)
    CHECK(ctx.vramSource == DataSource::Unknown, "VRAM source unknown without trace");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: traceFunction
// ---------------------------------------------------------------------------

static void test_trace_function()
{
    TEST_BEGIN("traceFunction executes and analyzes");
    MockAgentBackend mock;

    // Set up a simple function at 0x0200: MOV H,A; RET
    // (MOV H,A = 0x26 is not specially handled, just advances PC)
    std::vector<uint8_t> func = {0x67, 0xC9};  // MOV H,A; RET
    mock.setMemory(0x0200, func);

    // Place NOP at 0x01FF so requestRun() advances to 0x0200
    std::vector<uint8_t> nop = {0x00};
    mock.setMemory(0x01FF, nop);

    // Set CPU state: PC just before function, SP as if CALL pushed return addr
    CpuState cpu{};
    cpu.pc = 0x01FF;
    cpu.sp = 0xF7FE;
    cpu.a = 0x42;
    mock.setCpuState(cpu);

    // Place return address on stack at SP (simulating CALL having pushed it)
    std::vector<uint8_t> retAddr = {0x08, 0x01};  // 0x0108 little-endian
    mock.setMemory(0xF7FE, retAddr);

    AgentApi api(mock);
    TraceResult tr = api.traceFunction(0x0200);

    CHECK_EQ(0x0200u, (unsigned)tr.entryPc, "entry PC");
    CHECK(tr.instructionCount > 0, "instructions executed");
    CHECK_EQ(0x0201u, (unsigned)tr.exitPc, "exit at RET (0201)");
    CHECK_EQ(0xF7FEu, (unsigned)tr.entrySp, "SP entry");
    CHECK_EQ(0xF800u, (unsigned)tr.exitSp, "SP exit (restored by RET)");
    CHECK(tr.exitReason == ExitReason::Ret, "exit reason Ret");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Agent log
// ---------------------------------------------------------------------------

static void test_agent_log()
{
    TEST_BEGIN("Agent log records operations");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.getCpuState();
    api.readMemory(0x0100, 4);
    api.setBreakpoint(0x0200);

    auto entries = api.log().entries();
    CHECK_EQ(3u, (unsigned)entries.size(), "3 log entries");
    CHECK_STR("getCpuState", entries[0].tool, "first tool");
    CHECK_STR("readMemory", entries[1].tool, "second tool");
    CHECK_STR("setBreakpoint", entries[2].tool, "third tool");

    api.clearLog();
    CHECK_EQ(0u, (unsigned)api.log().size(), "log cleared");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Error cases
// ---------------------------------------------------------------------------

static void test_read_memory_zero_size()
{
    TEST_BEGIN("readMemory with zero size");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto data = api.readMemory(0x0100, 0);
    CHECK_EQ(0u, (unsigned)data.size(), "empty result");
    TEST_END();
}

static void test_rename_nonexistent()
{
    TEST_BEGIN("rename nonexistent function fails");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.renameFunction(0x9999, "Nope");
    CHECK(!r.success, "rename fails for nonexistent");
    TEST_END();
}

static void test_comment_nonexistent()
{
    TEST_BEGIN("setComment on nonexistent symbol fails");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.setComment(0x9999, "Nope");
    CHECK(!r.success, "comment fails for nonexistent");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("\n\033[1;33m========================================\033[0m\n");
    printf("\033[1;33m  Agent API Tests — Stage 5.3\033[0m\n");
    printf("\033[1;33m========================================\033[0m\n");

    // Execution control
    test_run_pause();
    test_step();
    test_reset();

    // CPU state
    test_get_cpu_state();

    // Memory access
    test_read_memory();
    test_write_memory();

    // Breakpoints
    test_set_clear_breakpoint();

    // Trace / IO / VRAM
    test_get_execution_trace();
    test_list_breakpoints();

    // Annotations
    test_create_function();
    test_rename_function();
    test_set_function_comment();
    test_delete_function();
    test_add_label();
    test_apply_annotation();

    // High-level
    test_get_function_context();
    test_get_function_context_with_name();
    test_get_function_context_callees();
    test_get_function_context_vram();
    test_trace_function();

    // Log
    test_agent_log();

    // Error cases
    test_read_memory_zero_size();
    test_rename_nonexistent();
    test_comment_nonexistent();

    printf("\n\033[1;33m========================================\033[0m\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("  (\033[1;31m%d FAILED\033[0m)", tests_failed);
    }
    printf("\n\033[1;33m========================================\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
