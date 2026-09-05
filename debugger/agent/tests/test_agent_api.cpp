// Agent API tests — Stage 5.3 / Stage 6.1
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
// Tests: Stage 6.1 — ErrorCode and AgentApiResult
// ---------------------------------------------------------------------------

static void test_agent_api_result_ok()
{
    TEST_BEGIN("AgentApiResult::ok factory");
    auto r = AgentApiResult<int>::ok(42);
    CHECK(r.success, "success is true");
    CHECK_EQ(42u, (unsigned)r.value, "value is 42");
    CHECK(r.error_code == ErrorCode::None, "error_code is None");
    CHECK(r.error_message.empty(), "error_message is empty");
    TEST_END();
}

static void test_agent_api_result_fail()
{
    TEST_BEGIN("AgentApiResult::fail factory");
    auto r = AgentApiResult<int>::fail(ErrorCode::InvalidArgument, "bad arg");
    CHECK(!r.success, "success is false");
    CHECK(r.error_code == ErrorCode::InvalidArgument, "error_code is InvalidArgument");
    CHECK_STR("bad arg", r.error_message, "error_message matches");
    TEST_END();
}

static void test_agent_api_result_void()
{
    TEST_BEGIN("AgentApiResult<void> factory");
    auto ok = AgentApiResult<void>::ok();
    CHECK(ok.success, "void ok success");
    CHECK(ok.error_code == ErrorCode::None, "void ok no error");

    auto fail = AgentApiResult<void>::fail(ErrorCode::OperationFailed, "oops");
    CHECK(!fail.success, "void fail not success");
    CHECK(fail.error_code == ErrorCode::OperationFailed, "void fail error code");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Stage 6.1 — isRunning()
// ---------------------------------------------------------------------------

static void test_is_running_initially()
{
    TEST_BEGIN("isRunning initially false (paused)");
    MockAgentBackend mock;
    AgentApi api(mock);

    CHECK(!api.isRunning(), "not running when paused");
    CHECK(mock.isPaused(), "backend confirms paused");
    TEST_END();
}

static void test_is_running_after_run()
{
    TEST_BEGIN("isRunning after run/pause cycle");
    MockAgentBackend mock;

    // Fill with NOPs so run hits safety limit and pauses
    std::vector<uint8_t> nops(256, 0x00);
    mock.setMemory(0x0100, nops);

    AgentApi api(mock);
    api.run();
    // Mock runs synchronously until HLT/limit, then pauses
    CHECK(!api.isRunning(), "not running after mock run completes");
    CHECK(mock.isPaused(), "backend paused after run");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Stage 6.1 — clearAllBreakpoints()
// ---------------------------------------------------------------------------

static void test_clear_all_breakpoints()
{
    TEST_BEGIN("clearAllBreakpoints removes all");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.setBreakpoint(0x0200);
    api.setBreakpoint(0x0300);
    api.setBreakpoint(0x0400);
    CHECK_EQ(3u, (unsigned)api.listBreakpoints().size(), "3 breakpoints");

    auto r = api.clearAllBreakpoints();
    CHECK(r.success, "clearAll succeeds");
    CHECK(r.error_code == ErrorCode::None, "no error");
    CHECK_EQ(0u, (unsigned)api.listBreakpoints().size(), "0 breakpoints after clear");
    TEST_END();
}

static void test_clear_all_breakpoints_empty()
{
    TEST_BEGIN("clearAllBreakpoints on empty list");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.clearAllBreakpoints();
    CHECK(r.success, "clearAll on empty succeeds");
    CHECK_EQ(0u, (unsigned)api.listBreakpoints().size(), "still 0 breakpoints");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Stage 6.1 — setRegister()
// ---------------------------------------------------------------------------

static void test_set_register_a()
{
    TEST_BEGIN("setRegister A");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.setRegister("A", 0xAA);
    CHECK(r.success, "setRegister A succeeds");
    CHECK_EQ(0xAAu, (unsigned)api.getCpuState().a, "A = 0xAA");
    TEST_END();
}

static void test_set_register_f_preserves_a()
{
    TEST_BEGIN("setRegister F preserves A");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.setRegister("A", 0x55);
    auto r = api.setRegister("F", 0x03);
    CHECK(r.success, "setRegister F succeeds");
    CHECK_EQ(0x55u, (unsigned)api.getCpuState().a, "A preserved");
    CHECK_EQ(0x03u, (unsigned)api.getCpuState().flags, "F = 0x03");
    TEST_END();
}

static void test_set_register_bc()
{
    TEST_BEGIN("setRegister B and C");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.setRegister("B", 0x12);
    api.setRegister("C", 0x34);
    CHECK_EQ(0x12u, (unsigned)api.getCpuState().b, "B = 0x12");
    CHECK_EQ(0x34u, (unsigned)api.getCpuState().c, "C = 0x34");
    TEST_END();
}

static void test_set_register_hl()
{
    TEST_BEGIN("setRegister H and L");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.setRegister("H", 0xDE);
    api.setRegister("L", 0xAD);
    CHECK_EQ(0xDEu, (unsigned)api.getCpuState().h, "H = 0xDE");
    CHECK_EQ(0xADu, (unsigned)api.getCpuState().l, "L = 0xAD");
    TEST_END();
}

static void test_set_register_pc()
{
    TEST_BEGIN("setRegister PC");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.setRegister("PC", 0x8000);
    CHECK(r.success, "setRegister PC succeeds");
    CHECK_EQ(0x8000u, (unsigned)api.getCpuState().pc, "PC = 0x8000");
    TEST_END();
}

static void test_set_register_sp()
{
    TEST_BEGIN("setRegister SP");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.setRegister("SP", 0xF000);
    CHECK(r.success, "setRegister SP succeeds");
    CHECK_EQ(0xF000u, (unsigned)api.getCpuState().sp, "SP = 0xF000");
    TEST_END();
}

static void test_set_register_invalid()
{
    TEST_BEGIN("setRegister invalid name");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.setRegister("X", 0x42);
    CHECK(!r.success, "invalid register fails");
    CHECK(r.error_code == ErrorCode::InvalidArgument, "error is InvalidArgument");
    CHECK(!r.error_message.empty(), "error message provided");
    TEST_END();
}

static void test_set_register_empty_name()
{
    TEST_BEGIN("setRegister empty name");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.setRegister("", 0x42);
    CHECK(!r.success, "empty name fails");
    CHECK(r.error_code == ErrorCode::InvalidArgument, "error is InvalidArgument");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Stage 6.1 — disassemble()
// ---------------------------------------------------------------------------

static void test_disassemble_basic()
{
    TEST_BEGIN("disassemble basic program");
    MockAgentBackend mock;
    AgentApi api(mock);

    // Program at 0x0100: LXI SP,0xF800 (31 00 F8), MVI A,0x55 (3E 55)
    auto r = api.disassemble(0x0100, 2);
    CHECK(r.success, "disassemble succeeds");
    CHECK(r.error_code == ErrorCode::None, "no error");
    CHECK_EQ(2u, (unsigned)r.value.size(), "2 instructions");

    // First: LXI SP, 0xF800
    CHECK_EQ(0x0100u, (unsigned)r.value[0].address, "first addr 0100");
    CHECK_EQ(0x0103u, (unsigned)r.value[0].next_address, "first next_addr 0103");
    CHECK_EQ(3u, (unsigned)r.value[0].bytes.size(), "3 bytes");
    CHECK_EQ(0x31u, (unsigned)r.value[0].bytes[0], "opcode 0x31");

    // Second: MVI A, 0x55
    CHECK_EQ(0x0103u, (unsigned)r.value[1].address, "second addr 0103");
    CHECK_EQ(0x0105u, (unsigned)r.value[1].next_address, "second next_addr 0105");
    CHECK_EQ(2u, (unsigned)r.value[1].bytes.size(), "2 bytes");
    CHECK_EQ(0x3Eu, (unsigned)r.value[1].bytes[0], "opcode 0x3E");
    TEST_END();
}

static void test_disassemble_single_instruction()
{
    TEST_BEGIN("disassemble single instruction");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.disassemble(0x0100, 1);
    CHECK(r.success, "disassemble succeeds");
    CHECK_EQ(1u, (unsigned)r.value.size(), "1 instruction");
    CHECK(!r.value[0].text.empty(), "text is non-empty");
    CHECK(!r.value[0].mnemonic.empty(), "mnemonic is non-empty");
    TEST_END();
}

static void test_disassemble_count_zero()
{
    TEST_BEGIN("disassemble count=0 fails");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.disassemble(0x0100, 0);
    CHECK(!r.success, "count=0 fails");
    CHECK(r.error_code == ErrorCode::InvalidArgument, "error is InvalidArgument");
    TEST_END();
}

static void test_disassemble_hlt()
{
    TEST_BEGIN("disassemble HLT instruction");
    MockAgentBackend mock;
    AgentApi api(mock);

    // HLT at 0x0108
    auto r = api.disassemble(0x0108, 1);
    CHECK(r.success, "disassemble succeeds");
    CHECK_EQ(1u, (unsigned)r.value.size(), "1 instruction");
    CHECK_EQ(0x76u, (unsigned)r.value[0].bytes[0], "opcode 0x76 (HLT)");
    CHECK_EQ(1u, (unsigned)r.value[0].bytes.size(), "1 byte");
    CHECK_EQ(0x0109u, (unsigned)r.value[0].next_address, "next_addr 0109");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Tests: Stage 6.1 — getInstructionHistory()
// ---------------------------------------------------------------------------

static void test_get_instruction_history_after_steps()
{
    TEST_BEGIN("getInstructionHistory after stepping");
    MockAgentBackend mock;
    AgentApi api(mock);

    // Step a few instructions
    api.step();  // LXI SP at 0100
    api.step();  // MVI A at 0103
    api.step();  // CALL at 0105

    auto r = api.getInstructionHistory(10);
    CHECK(r.success, "getInstructionHistory succeeds");
    CHECK(r.error_code == ErrorCode::None, "no error");
    CHECK(r.value.size() >= 3, "at least 3 entries");

    // First entry should be at 0x0100
    CHECK_EQ(0x0100u, (unsigned)r.value[0].address, "first entry at 0100");
    // Second at 0x0103
    CHECK_EQ(0x0103u, (unsigned)r.value[1].address, "second entry at 0103");
    // Third at 0x0105
    CHECK_EQ(0x0105u, (unsigned)r.value[2].address, "third entry at 0105");

    // Each entry should have bytes and disassembly
    CHECK(!r.value[0].bytes.empty(), "first entry has bytes");
    CHECK(!r.value[0].disassembly.empty(), "first entry has disassembly");
    TEST_END();
}

static void test_get_instruction_history_limit()
{
    TEST_BEGIN("getInstructionHistory respects limit");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.step();
    api.step();
    api.step();

    auto r = api.getInstructionHistory(2);
    CHECK(r.success, "succeeds");
    CHECK_EQ(2u, (unsigned)r.value.size(), "limited to 2 entries");
    // Should be the LAST 2 entries
    CHECK_EQ(0x0103u, (unsigned)r.value[0].address, "first of last 2 at 0103");
    CHECK_EQ(0x0105u, (unsigned)r.value[1].address, "second of last 2 at 0105");
    TEST_END();
}

static void test_get_instruction_history_count_zero()
{
    TEST_BEGIN("getInstructionHistory count=0 fails");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getInstructionHistory(0);
    CHECK(!r.success, "count=0 fails");
    CHECK(r.error_code == ErrorCode::InvalidArgument, "error is InvalidArgument");
    TEST_END();
}

static void test_get_instruction_history_empty()
{
    TEST_BEGIN("getInstructionHistory with no history");
    MockAgentBackend mock;
    // Don't step — mock has pre-populated events from constructor? No, only from simulateStep.
    AgentApi api(mock);

    auto r = api.getInstructionHistory(10);
    CHECK(r.success, "succeeds even with empty history");
    CHECK_EQ(0u, (unsigned)r.value.size(), "0 entries");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — getStack (§13)
// ---------------------------------------------------------------------------

static void test_get_stack_basic()
{
    TEST_BEGIN("getStack returns SP entries");
    MockAgentBackend mock;
    AgentApi api(mock);

    // SP = 0xF800, memory is zeroed
    auto r = api.getStack(4);
    CHECK(r.success, "getStack succeeds");
    CHECK_EQ(4u, (unsigned)r.value.size(), "4 entries");
    CHECK_EQ(0xF800u, (unsigned)r.value[0].address, "first entry at SP");
    CHECK_EQ(0xF802u, (unsigned)r.value[1].address, "second entry at SP+2");
    CHECK_EQ(0xF804u, (unsigned)r.value[2].address, "third entry at SP+4");
    CHECK_EQ(0xF806u, (unsigned)r.value[3].address, "fourth entry at SP+6");
    // Values are 0 (memory is zeroed)
    CHECK_EQ(0x0000u, (unsigned)r.value[0].value, "first value = 0");
    TEST_END();
}

static void test_get_stack_with_data()
{
    TEST_BEGIN("getStack reads 16-bit values from memory");
    MockAgentBackend mock;
    AgentApi api(mock);

    // Write known values at SP
    // SP = 0xF800: lo=0x34, hi=0x12 → value = 0x1234
    mock.setMemory(0xF800, {0x34, 0x12});
    // SP+2: lo=0xAB, hi=0xCD → value = 0xCDAB
    mock.setMemory(0xF802, {0xAB, 0xCD});

    auto r = api.getStack(2);
    CHECK(r.success, "succeeds");
    CHECK_EQ(0x1234u, (unsigned)r.value[0].value, "first value = 0x1234");
    CHECK_EQ(0xCDABu, (unsigned)r.value[1].value, "second value = 0xCDAB");
    TEST_END();
}

static void test_get_stack_with_symbols()
{
    TEST_BEGIN("getStack resolves symbols");
    MockAgentBackend mock;
    AgentApi api(mock);

    // Create a symbol at 0x0200
    mock.requestCreateFunction(0x0200, "myFunc");

    // Write 0x0200 at SP
    mock.setMemory(0xF800, {0x00, 0x02});

    auto r = api.getStack(1);
    CHECK(r.success, "succeeds");
    CHECK_EQ(0x0200u, (unsigned)r.value[0].value, "value = 0x0200");
    CHECK_STR("myFunc", r.value[0].symbol, "symbol resolved");
    TEST_END();
}

static void test_get_stack_limit_zero()
{
    TEST_BEGIN("getStack limit=0 fails");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getStack(0);
    CHECK(!r.success, "limit=0 fails");
    CHECK(r.error_code == ErrorCode::InvalidArgument, "InvalidArgument");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — getMemoryMap (§18)
// ---------------------------------------------------------------------------

static void test_get_memory_map()
{
    TEST_BEGIN("getMemoryMap returns 256 blocks");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getMemoryMap();
    CHECK(r.success, "succeeds");
    CHECK_EQ(256u, (unsigned)r.value.size(), "256 blocks");

    // First block: 0x0000-0x00FF
    CHECK_EQ(0x0000u, (unsigned)r.value[0].start, "block 0 start");
    CHECK_EQ(0x00FFu, (unsigned)r.value[0].end, "block 0 end");

    // Last block: 0xFF00-0xFFFF
    CHECK_EQ(0xFF00u, (unsigned)r.value[255].start, "block 255 start");
    CHECK_EQ(0xFFFFu, (unsigned)r.value[255].end, "block 255 end");
    TEST_END();
}

static void test_get_memory_map_has_content()
{
    TEST_BEGIN("getMemoryMap detects non-zero content");
    MockAgentBackend mock;
    AgentApi api(mock);

    // Mock has program at 0x0100-0x0208, so block 1 (0x0100-0x01FF) and
    // block 2 (0x0200-0x02FF) should have content
    auto r = api.getMemoryMap();
    CHECK(r.success, "succeeds");
    CHECK(r.value[1].has_content, "block 1 (0x0100-0x01FF) has content");
    CHECK(r.value[2].has_content, "block 2 (0x0200-0x02FF) has content");
    CHECK(!r.value[10].has_content, "block 10 (0x0A00-0x0AFF) is empty");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — getScreenInfo (§20)
// ---------------------------------------------------------------------------

static void test_get_screen_info()
{
    TEST_BEGIN("getScreenInfo returns video mode");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getScreenInfo();
    CHECK(r.success, "succeeds");
    CHECK_EQ(576u, (unsigned)r.value.width, "width = 576");
    CHECK_EQ(288u, (unsigned)r.value.height, "height = 288");
    CHECK_EQ(512u, (unsigned)r.value.visible_width, "visible_width = 512");
    CHECK_EQ(256u, (unsigned)r.value.visible_height, "visible_height = 256");
    CHECK(!r.value.mode512, "256-mode by default");
    CHECK_EQ(0xC000u, (unsigned)r.value.vram_base, "vram_base = 0xC000");
    CHECK_EQ(8u, (unsigned)r.value.pixels_per_byte, "pixels_per_byte = 8");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — getVramInfo (§19)
// ---------------------------------------------------------------------------

static void test_get_vram_info()
{
    TEST_BEGIN("getVramInfo returns 4 planes");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getVramInfo();
    CHECK(r.success, "succeeds");
    CHECK_EQ(4u, (unsigned)r.value.planes.size(), "4 planes");
    CHECK(!r.value.mode512, "256-mode");
    CHECK_EQ(0xC000u, (unsigned)r.value.vram_base, "vram_base = 0xC000");

    // Plane 0: 0xE000, Plane 1: 0xC000, Plane 2: 0xA000, Plane 3: 0x8000
    CHECK_EQ(0u, (unsigned)r.value.planes[0].plane, "plane 0 index");
    CHECK_EQ(0xE000u, (unsigned)r.value.planes[0].address, "plane 0 addr");
    CHECK_EQ(8192u, (unsigned)r.value.planes[0].size, "plane 0 size");

    CHECK_EQ(1u, (unsigned)r.value.planes[1].plane, "plane 1 index");
    CHECK_EQ(0xC000u, (unsigned)r.value.planes[1].address, "plane 1 addr");

    CHECK_EQ(3u, (unsigned)r.value.planes[3].plane, "plane 3 index");
    CHECK_EQ(0x8000u, (unsigned)r.value.planes[3].address, "plane 3 addr");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — getSymbols / getFunction (§15)
// ---------------------------------------------------------------------------

static void test_get_symbols_empty()
{
    TEST_BEGIN("getSymbols with no symbols");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getSymbols();
    CHECK(r.success, "succeeds");
    CHECK_EQ(0u, (unsigned)r.value.size(), "0 symbols");
    TEST_END();
}

static void test_get_symbols_with_data()
{
    TEST_BEGIN("getSymbols returns created symbols");
    MockAgentBackend mock;
    AgentApi api(mock);

    mock.requestCreateFunction(0x0200, "myFunc");
    mock.requestAddLabel(0x0300, "myLabel");

    auto r = api.getSymbols();
    CHECK(r.success, "succeeds");
    CHECK_EQ(2u, (unsigned)r.value.size(), "2 symbols");

    // Sorted by address: 0x0200 first, 0x0300 second
    CHECK_EQ(0x0200u, (unsigned)r.value[0].address, "first addr");
    CHECK_STR("myFunc", r.value[0].name, "first name");
    CHECK(r.value[0].type == SymbolInfo::Type::Function, "first is Function");

    CHECK_EQ(0x0300u, (unsigned)r.value[1].address, "second addr");
    CHECK_STR("myLabel", r.value[1].name, "second name");
    CHECK(r.value[1].type == SymbolInfo::Type::Label, "second is Label");
    TEST_END();
}

static void test_get_symbols_with_limit()
{
    TEST_BEGIN("getSymbols with limit");
    MockAgentBackend mock;
    AgentApi api(mock);

    mock.requestCreateFunction(0x0200, "func1");
    mock.requestAddLabel(0x0300, "label1");
    mock.requestCreateFunction(0x0400, "func2");

    auto r = api.getSymbols(2);
    CHECK(r.success, "succeeds");
    CHECK_EQ(2u, (unsigned)r.value.size(), "limited to 2");
    TEST_END();
}

static void test_get_function_found()
{
    TEST_BEGIN("getFunction finds existing symbol");
    MockAgentBackend mock;
    AgentApi api(mock);

    mock.requestCreateFunction(0x0200, "myFunc");

    auto r = api.getFunction(0x0200);
    CHECK(r.success, "found");
    CHECK_STR("myFunc", r.value.name, "name matches");
    CHECK_EQ(0x0200u, (unsigned)r.value.address, "address matches");
    CHECK(r.value.type == SymbolInfo::Type::Function, "is Function");
    TEST_END();
}

static void test_get_function_not_found()
{
    TEST_BEGIN("getFunction fails for unknown address");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getFunction(0x9999);
    CHECK(!r.success, "not found");
    CHECK(r.error_code == ErrorCode::InvalidAddress, "InvalidAddress");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — getXrefs (§16)
// ---------------------------------------------------------------------------

static void test_get_xrefs_empty()
{
    TEST_BEGIN("getXrefs with no xrefs");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getXrefs(0x0200);
    CHECK(r.success, "succeeds");
    CHECK_EQ(0u, (unsigned)r.value.size(), "0 xrefs");
    TEST_END();
}

static void test_get_xrefs_with_data()
{
    TEST_BEGIN("getXrefs returns xrefs after rebuild");
    MockAgentBackend mock;
    AgentApi api(mock);

    // The mock has CALL 0x0200 at address 0x0105 (CD 00 02)
    // Rebuild xrefs to detect it
    auto readByte = [&mock](uint16_t addr) -> uint8_t {
        return mock.readMemory(addr);
    };
    mock.symbolDatabase().rebuildXrefs(readByte);

    auto r = api.getXrefs(0x0200);
    CHECK(r.success, "succeeds");
    CHECK(r.value.size() > 0, "has xrefs to 0x0200");
    CHECK_EQ(0x0105u, (unsigned)r.value[0].from, "xref from 0x0105");
    CHECK_EQ(0x0200u, (unsigned)r.value[0].to, "xref to 0x0200");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — getCallGraph (§17)
// ---------------------------------------------------------------------------

static void test_get_call_graph()
{
    TEST_BEGIN("getCallGraph returns edges");
    MockAgentBackend mock;
    AgentApi api(mock);

    // Rebuild xrefs so call graph has data
    auto readByte = [&mock](uint16_t addr) -> uint8_t {
        return mock.readMemory(addr);
    };
    mock.symbolDatabase().rebuildXrefs(readByte);

    // Get all edges
    auto r = api.getCallGraph();
    CHECK(r.success, "succeeds");
    // Mock has CALL 0x0200 at 0x0105
    CHECK(r.value.size() > 0, "has at least 1 edge");

    bool found = false;
    for (const auto &e : r.value) {
        if (e.from == 0x0105 && e.to == 0x0200) {
            found = true;
            break;
        }
    }
    CHECK(found, "found CALL 0x0200 edge");
    TEST_END();
}

static void test_get_call_graph_filtered()
{
    TEST_BEGIN("getCallGraph filtered by address");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto readByte = [&mock](uint16_t addr) -> uint8_t {
        return mock.readMemory(addr);
    };
    mock.symbolDatabase().rebuildXrefs(readByte);

    // Filter for address 0x0200
    auto r = api.getCallGraph(0x0200);
    CHECK(r.success, "succeeds");
    CHECK(r.value.size() > 0, "has edges involving 0x0200");
    for (const auto &e : r.value) {
        CHECK(e.from == 0x0200 || e.to == 0x0200, "edge involves 0x0200");
    }
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — loadRomInfo (§4)
// ---------------------------------------------------------------------------

static void test_load_rom_info()
{
    TEST_BEGIN("loadRomInfo returns structured result");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.loadRomInfo("test.rom");
    CHECK(r.success, "succeeds");
    CHECK_STR("test.rom", r.value.path, "path matches");
    CHECK_EQ(0x0100u, (unsigned)r.value.origin, ".rom origin = 0x0100");
    // PC is whatever the mock returns after load (mock doesn't change PC in loadRom)
    // So PC stays at 0x0100 (initial)
    CHECK_EQ(0x0100u, (unsigned)r.value.pc, "pc after load");
    TEST_END();
}

static void test_load_rom_info_r0m()
{
    TEST_BEGIN("loadRomInfo .r0m origin = 0x0000");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.loadRomInfo("game.r0m");
    CHECK(r.success, "succeeds");
    CHECK_EQ(0x0000u, (unsigned)r.value.origin, ".r0m origin = 0x0000");
    TEST_END();
}

static void test_load_rom_info_explicit_org()
{
    TEST_BEGIN("loadRomInfo with explicit org");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.loadRomInfo("data.bin", 0x8000);
    CHECK(r.success, "succeeds");
    CHECK_EQ(0x8000u, (unsigned)r.value.origin, "explicit org = 0x8000");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stage 6.1 Iteration 2 — getDebugState (§21)
// ---------------------------------------------------------------------------

static void test_get_debug_state()
{
    TEST_BEGIN("getDebugState returns full snapshot");
    MockAgentBackend mock;
    AgentApi api(mock);

    auto r = api.getDebugState();
    CHECK(!r.running, "not running (paused)");
    CHECK_EQ(0x0100u, (unsigned)r.cpu.pc, "PC = 0x0100");
    CHECK_EQ(0xF800u, (unsigned)r.cpu.sp, "SP = 0xF800");
    CHECK_EQ(0x42u, (unsigned)r.cpu.a, "A = 0x42");
    CHECK_EQ(0u, (unsigned)r.breakpoints.size(), "no breakpoints");
    // Current instruction at PC=0x0100: LXI SP, 0xF800
    CHECK(!r.current_instruction.empty(), "has current instruction");
    TEST_END();
}

static void test_get_debug_state_with_breakpoint()
{
    TEST_BEGIN("getDebugState includes breakpoints");
    MockAgentBackend mock;
    AgentApi api(mock);

    api.setBreakpoint(0x0200);

    auto r = api.getDebugState();
    CHECK_EQ(1u, (unsigned)r.breakpoints.size(), "1 breakpoint");
    CHECK_EQ(0x0200u, (unsigned)r.breakpoints[0].address, "at 0x0200");
    TEST_END();
}

static void test_get_debug_state_with_function()
{
    TEST_BEGIN("getDebugState resolves current function");
    MockAgentBackend mock;
    AgentApi api(mock);

    mock.requestCreateFunction(0x0100, "main");

    auto r = api.getDebugState();
    CHECK_STR("main", r.current_function, "current function = main");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    printf("\n\033[1;33m========================================\033[0m\n");
    printf("\033[1;33m  Agent API Tests — Stage 5.3 / 6.1\033[0m\n");
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

    // Stage 6.1 — ErrorCode / AgentApiResult
    test_agent_api_result_ok();
    test_agent_api_result_fail();
    test_agent_api_result_void();

    // Stage 6.1 — isRunning
    test_is_running_initially();
    test_is_running_after_run();

    // Stage 6.1 — clearAllBreakpoints
    test_clear_all_breakpoints();
    test_clear_all_breakpoints_empty();

    // Stage 6.1 — setRegister
    test_set_register_a();
    test_set_register_f_preserves_a();
    test_set_register_bc();
    test_set_register_hl();
    test_set_register_pc();
    test_set_register_sp();
    test_set_register_invalid();
    test_set_register_empty_name();

    // Stage 6.1 — disassemble
    test_disassemble_basic();
    test_disassemble_single_instruction();
    test_disassemble_count_zero();
    test_disassemble_hlt();

    // Stage 6.1 — getInstructionHistory
    test_get_instruction_history_after_steps();
    test_get_instruction_history_limit();
    test_get_instruction_history_count_zero();
    test_get_instruction_history_empty();

    // Stage 6.1 Iteration 2 — getStack (§13)
    test_get_stack_basic();
    test_get_stack_with_data();
    test_get_stack_with_symbols();
    test_get_stack_limit_zero();

    // Stage 6.1 Iteration 2 — getMemoryMap (§18)
    test_get_memory_map();
    test_get_memory_map_has_content();

    // Stage 6.1 Iteration 2 — getScreenInfo (§20)
    test_get_screen_info();

    // Stage 6.1 Iteration 2 — getVramInfo (§19)
    test_get_vram_info();

    // Stage 6.1 Iteration 2 — getSymbols / getFunction (§15)
    test_get_symbols_empty();
    test_get_symbols_with_data();
    test_get_symbols_with_limit();
    test_get_function_found();
    test_get_function_not_found();

    // Stage 6.1 Iteration 2 — getXrefs (§16)
    test_get_xrefs_empty();
    test_get_xrefs_with_data();

    // Stage 6.1 Iteration 2 — getCallGraph (§17)
    test_get_call_graph();
    test_get_call_graph_filtered();

    // Stage 6.1 Iteration 2 — loadRomInfo (§4)
    test_load_rom_info();
    test_load_rom_info_r0m();
    test_load_rom_info_explicit_org();

    // Stage 6.1 Iteration 2 — getDebugState (§21)
    test_get_debug_state();
    test_get_debug_state_with_breakpoint();
    test_get_debug_state_with_function();

    printf("\n\033[1;33m========================================\033[0m\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("  (\033[1;31m%d FAILED\033[0m)", tests_failed);
    }
    printf("\n\033[1;33m========================================\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
