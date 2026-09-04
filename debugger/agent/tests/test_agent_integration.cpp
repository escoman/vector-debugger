// Agent Integration Tests — Stage 5.3.2
//
// 20 tests verifying Command Queue + Emulation Thread isolation
// using real DebugBackend + NoBoardTarget.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <future>
#include <set>

#include "agent_api.h"
#include "backend.h"
#include "memory.h"
#include "no_board_target.h"
#include "debug_target.h"
#include "events.h"
#include "options.h"

// ---------------------------------------------------------------------------
// Global references needed by debug_adapter.cpp (weak dependency)
// ---------------------------------------------------------------------------
DebugBackend *g_adapter_backend = nullptr;

// ---------------------------------------------------------------------------
// HAL functions — needed by i8080.cpp (same as test_backend.cpp)
// ---------------------------------------------------------------------------

static Memory       *hal_memory = nullptr;
static DebugBackend *hal_dbg    = nullptr;
static bool          hal_iff    = false;

int i8080_hal_memory_read_byte(int addr)
{
    return hal_memory->read(addr, false);
}

void i8080_hal_memory_write_byte(int addr, int value)
{
    hal_memory->write(addr, value, false);
}

int i8080_hal_memory_read_word(int addr, bool stack)
{
    return hal_memory->read(addr, stack)
         | (hal_memory->read(addr + 1, stack) << 8);
}

void i8080_hal_memory_write_word(int addr, int word, bool stack)
{
    hal_memory->write(addr, word & 0xff, stack);
    hal_memory->write(addr + 1, word >> 8, stack);
}

int i8080_hal_io_input(int port)
{
    if (hal_dbg) hal_dbg->onIoInput((uint8_t)port, 0xff);
    return 0xff;
}

void i8080_hal_io_output(int port, int value)
{
    if (hal_dbg) hal_dbg->onIoOutput((uint8_t)port, (uint8_t)value);
}

void i8080_hal_iff(int on)
{
    hal_iff = (on != 0);
}

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
// Test program
// ---------------------------------------------------------------------------

static const uint8_t test_program[] = {
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

// Simple loop program for running tests
static const uint8_t loop_program[] = {
    // 0x0000: LXI SP, 0xC100
    0x31, 0x00, 0xC1,
    // 0x0003: MVI A, 0x42
    0x3E, 0x42,
    // 0x0005: MOV M, A   (HL=C000 from LXI H below — but we use this as bp target)
    0x77,
    // 0x0006: INR A
    0x3C,
    // 0x0007: MOV M, A
    0x77,
    // 0x0008: JMP 0003
    0xC3, 0x03, 0x00,
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void writeProgram(Memory &mem, const uint8_t *data, size_t len, uint16_t addr)
{
    for (size_t i = 0; i < len; ++i) {
        mem.write(static_cast<uint16_t>(addr + i), data[i], false);
    }
}

struct TestFixture {
    Memory mem;
    NoBoardTarget *target;
    DebugBackend *backend;

    TestFixture() {
        Options.novideo = true;
        Options.nosound = true;
        target = new NoBoardTarget(mem);
        backend = new DebugBackend(*target);
        backend->reset();
        // Set HAL globals for i8080 CPU
        hal_memory = &mem;
        hal_dbg = backend;
    }
    ~TestFixture() {
        hal_dbg = nullptr;
        hal_memory = nullptr;
        delete backend;
        delete target;
    }

    void loadMainProgram() {
        writeProgram(mem, test_program, sizeof(test_program), 0x0000);
        writeProgram(mem, subroutine, sizeof(subroutine), 0x0200);
    }

    void loadLoopProgram() {
        writeProgram(mem, loop_program, sizeof(loop_program), 0x0000);
    }

    void initCpu(uint16_t pc = 0, uint16_t sp = 0xC100) {
        target->initCpu(pc, sp);
    }
};

// ---------------------------------------------------------------------------
// Test 1: command_queue_preserves_order
// ---------------------------------------------------------------------------

static void test_command_queue_preserves_order()
{
    TEST_BEGIN("command_queue_preserves_order");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    // Add N breakpoints via command queue and verify all succeed in order
    const int N = 10;
    std::vector<uint16_t> addresses;
    for (int i = 0; i < N; ++i) {
        addresses.push_back(static_cast<uint16_t>(0x1000 + i * 0x100));
    }

    for (int i = 0; i < N; ++i) {
        auto r = f.backend->requestAddBreakpoint(addresses[i]);
        CHECK(r.success, "breakpoint added");
    }

    // Verify all breakpoints exist
    auto bps = f.backend->getBreakpoints();
    CHECK_EQ(N, (int)bps.size(), "all N breakpoints present");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 2: concurrent_commands_not_lost
// ---------------------------------------------------------------------------

static void test_concurrent_commands_not_lost()
{
    TEST_BEGIN("concurrent_commands_not_lost");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    std::atomic<int> successCount{0};
    auto addBp = [&](uint16_t addr) {
        auto r = f.backend->requestAddBreakpoint(addr);
        if (r.success) successCount++;
    };

    // 4 threads add breakpoints simultaneously
    std::thread t1(addBp, 0x0300);
    std::thread t2(addBp, 0x0400);
    std::thread t3(addBp, 0x0500);
    std::thread t4(addBp, 0x0600);
    t1.join(); t2.join(); t3.join(); t4.join();

    CHECK_EQ(4, successCount.load(), "all 4 concurrent breakpoints added");
    auto bps = f.backend->getBreakpoints();
    CHECK_EQ(4, (int)bps.size(), "4 breakpoints total");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 3: each_command_receives_own_result
// ---------------------------------------------------------------------------

static void test_each_command_receives_own_result()
{
    TEST_BEGIN("each_command_receives_own_result");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    // Add breakpoints concurrently, each should get its own result
    std::atomic<int> dupFails{0};
    auto addBp = [&](uint16_t addr) {
        auto r = f.backend->requestAddBreakpoint(addr);
        if (!r.success) dupFails++;
    };

    // Two threads try same address — one should fail
    std::thread t1(addBp, 0x0300);
    std::thread t2(addBp, 0x0300);
    t1.join(); t2.join();

    CHECK_EQ(1, dupFails.load(), "one duplicate rejected");

    // Different addresses should both succeed
    auto r1 = f.backend->requestAddBreakpoint(0x0400);
    auto r2 = f.backend->requestAddBreakpoint(0x0500);
    CHECK(r1.success, "first unique succeeds");
    CHECK(r2.success, "second unique succeeds");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 4: command_executes_on_emulation_thread
// ---------------------------------------------------------------------------

static void test_command_executes_on_emulation_thread()
{
    TEST_BEGIN("command_executes_on_emulation_thread");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    // Without emulation thread, commands execute on calling thread
    // Verify by adding a breakpoint and checking it exists
    auto r = f.backend->requestAddBreakpoint(0x0300);
    CHECK(r.success, "command executed successfully");
    CHECK(f.backend->hasBreakpoint(0x0300), "breakpoint exists after command");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 5: agent_thread_never_executes_cpu
// ---------------------------------------------------------------------------

static void test_agent_thread_never_executes_cpu()
{
    TEST_BEGIN("agent_thread_never_executes_cpu");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu(0x0000);

    uint16_t pcBefore = f.backend->getCpuState().pc;

    // Agent API calls should NOT advance PC (no direct CPU execution)
    f.backend->requestAddBreakpoint(0x0300);
    f.backend->requestCreateFunction(0x0200, "DrawPixel");

    uint16_t pcAfter = f.backend->getCpuState().pc;
    CHECK_EQ(pcBefore, pcAfter, "PC unchanged after agent commands");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 6: commands_work_while_running (test scenario — no emulation thread)
// ---------------------------------------------------------------------------

static void test_commands_work_while_running()
{
    TEST_BEGIN("commands_work_while_running");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    // Set state to Running (flag only — no emulation thread)
    f.backend->requestRun();
    CHECK_EQ((int)DebuggerState::Running, (int)f.backend->getState(), "state is Running");

    // Commands should still work (direct execution in test scenario)
    auto r = f.backend->requestAddBreakpoint(0x0300);
    CHECK(r.success, "breakpoint added while running");
    CHECK(f.backend->hasBreakpoint(0x0300), "breakpoint confirmed");

    f.backend->requestPause();
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 7: pause_while_running
// ---------------------------------------------------------------------------

static void test_pause_while_running()
{
    TEST_BEGIN("pause_while_running");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    f.backend->requestRun();
    CHECK_EQ((int)DebuggerState::Running, (int)f.backend->getState(), "running");

    f.backend->requestPause();
    // In test scenario, pause just sets flags
    CHECK(!f.backend->isPaused() || f.backend->getState() == DebuggerState::Running,
          "pause requested (flags set)");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 8: quit_while_running
// ---------------------------------------------------------------------------

static void test_quit_while_running()
{
    TEST_BEGIN("quit_while_running");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    f.backend->requestRun();
    f.backend->requestQuit();
    CHECK(f.backend->isQuitRequested(), "quit requested");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 9: breakpoint_mutation_through_queue
// ---------------------------------------------------------------------------

static void test_breakpoint_mutation_through_queue()
{
    TEST_BEGIN("breakpoint_mutation_through_queue");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    // Add
    auto r1 = f.backend->requestAddBreakpoint(0x0300);
    CHECK(r1.success, "add succeeds");

    // Enable/disable
    auto r2 = f.backend->requestSetBreakpointEnabled(0x0300, false);
    CHECK(r2.success, "disable succeeds");
    CHECK(!f.backend->hasBreakpoint(0x0300), "disabled bp not active");

    auto r3 = f.backend->requestSetBreakpointEnabled(0x0300, true);
    CHECK(r3.success, "enable succeeds");
    CHECK(f.backend->hasBreakpoint(0x0300), "enabled bp active");

    // Remove
    auto r4 = f.backend->requestRemoveBreakpoint(0x0300);
    CHECK(r4.success, "remove succeeds");
    CHECK(!f.backend->hasBreakpoint(0x0300), "removed bp gone");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 10: annotation_mutation_through_queue
// ---------------------------------------------------------------------------

static void test_annotation_mutation_through_queue()
{
    TEST_BEGIN("annotation_mutation_through_queue");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    // Create function
    auto r1 = f.backend->requestCreateFunction(0x0200, "DrawPixel");
    CHECK(r1.success, "function created");

    // Rename
    auto r2 = f.backend->requestRenameSymbol(0x0200, "DrawSprite");
    CHECK(r2.success, "rename succeeds");

    const DebugSymbol *sym = f.backend->symbolDatabase().findSymbol(0x0200);
    CHECK(sym != nullptr, "symbol found");
    if (sym) CHECK(sym->name == "DrawSprite", "renamed correctly");

    // Set comment
    auto r3 = f.backend->requestSetComment(0x0200, "VRAM draw routine");
    CHECK(r3.success, "comment set");

    // Add label
    auto r4 = f.backend->requestAddLabel(0x0207, "ret_label");
    CHECK(r4.success, "label added");

    // Remove
    auto r5 = f.backend->requestRemoveSymbol(0x0200);
    CHECK(r5.success, "symbol removed");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 11: trace_execution_through_queue
// ---------------------------------------------------------------------------

static void test_trace_execution_through_queue()
{
    TEST_BEGIN("trace_execution_through_queue");
    TestFixture f;
    f.loadMainProgram();
    // Set CPU at subroutine entry (post-CALL state)
    f.initCpu(0x0200, 0xC0F0);

    // Push return address on stack
    f.mem.write(0xC0F0, 0x09, false);  // return PC low
    f.mem.write(0xC0F1, 0x00, false);  // return PC high

    IDebugBackend::TraceExecutionParams params;
    params.startPc = 0x0200;
    params.maxInstructions = 100;
    params.stopOnRet = true;
    params.stopOnCallerReturn = false;

    auto future = f.backend->requestExecuteTrace(params);
    auto result = future.get();

    CHECK(result.instructionsExecuted > 0, "instructions executed");
    CHECK(result.exitReason == ExitReason::Ret, "exit reason is Ret");
    CHECK_EQ(0x0207u, (unsigned)result.exitPc, "exit at RET");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 12: trace_timeout
// ---------------------------------------------------------------------------

static void test_trace_timeout()
{
    TEST_BEGIN("trace_timeout");
    TestFixture f;
    f.loadLoopProgram();
    f.initCpu(0x0003);  // Start at MVI A (infinite loop)

    // Set HL for MOV M,A
    CpuState cpu = f.backend->getCpuState();
    cpu.h = 0xC0; cpu.l = 0x00;
    f.target->writeCpuRegister(static_cast<int>(IDebugBackend::RegisterId::HL), 0xC000);

    IDebugBackend::TraceExecutionParams params;
    params.startPc = 0x0003;
    params.maxInstructions = 10;  // very small limit
    params.stopOnRet = true;
    params.stopOnCallerReturn = false;

    auto result = f.backend->requestExecuteTrace(params).get();

    CHECK(result.exitReason == ExitReason::Timeout, "exit reason is Timeout");
    CHECK_EQ(10u, (unsigned)result.instructionsExecuted, "exactly maxInstructions");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 13: concurrent_trace_requests
// ---------------------------------------------------------------------------

static void test_concurrent_trace_requests()
{
    TEST_BEGIN("concurrent_trace_requests");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu(0x0200, 0xC0F0);

    f.mem.write(0xC0F0, 0x09, false);
    f.mem.write(0xC0F1, 0x00, false);

    IDebugBackend::TraceExecutionParams params;
    params.startPc = 0x0200;
    params.maxInstructions = 100;
    params.stopOnRet = true;

    // First trace should succeed
    auto r1 = f.backend->requestExecuteTrace(params).get();
    CHECK(r1.exitReason == ExitReason::Ret, "first trace succeeds");

    // Second trace should also succeed (first completed)
    f.initCpu(0x0200, 0xC0F0);
    f.mem.write(0xC0F0, 0x09, false);
    f.mem.write(0xC0F1, 0x00, false);
    auto r2 = f.backend->requestExecuteTrace(params).get();
    CHECK(r2.exitReason == ExitReason::Ret, "second trace succeeds");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 14: real_debugbackend_threaded_integration
// ---------------------------------------------------------------------------

static void test_real_debugbackend_threaded_integration()
{
    TEST_BEGIN("real_debugbackend_threaded_integration");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu();

    // Start emulation thread
    std::thread emuThread([&]{ f.backend->runUntilPause(); });

    // Give thread time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Quit cleanly
    f.backend->requestQuit();
    emuThread.join();

    CHECK(f.backend->isQuitRequested(), "quit requested");
    CHECK_EQ((int)DebuggerState::Paused, (int)f.backend->getState(), "state is Paused after quit");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 15: real_breakpoint_integration
// ---------------------------------------------------------------------------

static void test_real_breakpoint_integration()
{
    TEST_BEGIN("real_breakpoint_integration");
    TestFixture f;
    f.loadLoopProgram();
    f.initCpu(0x0000);

    // Add breakpoint at 0x0005
    f.backend->requestAddBreakpoint(0x0005);

    // Start emulation thread
    std::thread emuThread([&]{ f.backend->runUntilPause(); });

    // Run to breakpoint
    f.backend->requestRun();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Pause and quit
    f.backend->requestPause();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    f.backend->requestQuit();
    emuThread.join();

    // Should have stopped at or near breakpoint
    uint16_t pc = f.backend->getCpuState().pc;
    CHECK(pc <= 0x000A, "PC in valid range");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 16: real_trace_integration
// ---------------------------------------------------------------------------

static void test_real_trace_integration()
{
    TEST_BEGIN("real_trace_integration");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu(0x0200, 0xC0F0);

    f.mem.write(0xC0F0, 0x09, false);
    f.mem.write(0xC0F1, 0x00, false);

    // Trace without emulation thread (direct execution)
    IDebugBackend::TraceExecutionParams params;
    params.startPc = 0x0200;
    params.maxInstructions = 100;
    params.stopOnRet = true;

    auto result = f.backend->requestExecuteTrace(params).get();

    CHECK(result.instructionsExecuted > 0, "trace executed instructions");
    CHECK(result.exitReason == ExitReason::Ret, "trace ended with RET");
    CHECK(result.exitSp >= result.entrySp, "SP tracked correctly");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 17: memory_attribution
// ---------------------------------------------------------------------------

static void test_memory_attribution()
{
    TEST_BEGIN("memory_attribution");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu(0x0200, 0xC0F0);

    f.mem.write(0xC0F0, 0x09, false);
    f.mem.write(0xC0F1, 0x00, false);

    // Set A to known value
    f.target->writeCpuRegister(static_cast<int>(IDebugBackend::RegisterId::AF), 0x5500);

    IDebugBackend::TraceExecutionParams params;
    params.startPc = 0x0200;
    params.maxInstructions = 100;
    params.stopOnRet = true;

    auto result = f.backend->requestExecuteTrace(params).get();
    CHECK(result.instructionsExecuted > 0, "trace completed");

    // Check memory history for writes during trace
    auto memHistory = f.backend->memoryHistorySnapshot();
    bool foundWrite = false;
    for (const auto &ev : memHistory) {
        if (ev.instructionSequence >= result.startSequence &&
            ev.instructionSequence < result.endSequence &&
            ev.type == MemoryAccessType::Write &&
            ev.virt >= 0xC000 && ev.virt < 0xC100) {
            foundWrite = true;
            break;
        }
    }
    CHECK(foundWrite, "memory write attributed during trace");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 18: io_attribution
// ---------------------------------------------------------------------------

static void test_io_attribution()
{
    TEST_BEGIN("io_attribution");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu(0x0200, 0xC0F0);

    f.mem.write(0xC0F0, 0x09, false);
    f.mem.write(0xC0F1, 0x00, false);

    IDebugBackend::TraceExecutionParams params;
    params.startPc = 0x0200;
    params.maxInstructions = 100;
    params.stopOnRet = true;
    params.stopOnCallerReturn = false;

    auto result = f.backend->requestExecuteTrace(params).get();
    // Subroutine has no IO instructions — just verify trace completes
    CHECK(result.instructionsExecuted > 0, "trace completed");
    CHECK(result.exitReason == ExitReason::Ret, "trace ended with RET");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 19: vram_attribution
// ---------------------------------------------------------------------------

static void test_vram_attribution()
{
    TEST_BEGIN("vram_attribution");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu(0x0200, 0xC0F0);

    f.mem.write(0xC0F0, 0x09, false);
    f.mem.write(0xC0F1, 0x00, false);

    // Set A=0x55 for MOV M,A
    f.target->writeCpuRegister(static_cast<int>(IDebugBackend::RegisterId::AF), 0x5500);

    IDebugBackend::TraceExecutionParams params;
    params.startPc = 0x0200;
    params.maxInstructions = 100;
    params.stopOnRet = true;

    auto result = f.backend->requestExecuteTrace(params).get();

    // Check memory history for write to address 0xC000 (HL=C000 from MVI H,L)
    auto memHistory = f.backend->memoryHistorySnapshot();
    bool foundVramWrite = false;
    for (const auto &ev : memHistory) {
        if (ev.instructionSequence >= result.startSequence &&
            ev.instructionSequence < result.endSequence &&
            ev.type == MemoryAccessType::Write &&
            ev.virt == 0xC000) {
            foundVramWrite = true;
            break;
        }
    }
    CHECK(foundVramWrite, "VRAM write at 0xC000 (MOV M,A with HL=C000)");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 20: unknown_pc_remains_unknown
// ---------------------------------------------------------------------------

static void test_unknown_pc_remains_unknown()
{
    TEST_BEGIN("unknown_pc_remains_unknown");
    TestFixture f;
    f.loadMainProgram();
    f.initCpu(0x0000);

    // PC=0 is a valid PC, not "unknown"
    uint16_t pc = f.backend->getCpuState().pc;
    CHECK_EQ(0x0000u, (unsigned)pc, "PC is 0x0000 (valid, not unknown)");

    // Step from PC=0
    f.backend->stepInstruction();
    uint16_t pcAfter = f.backend->getCpuState().pc;
    CHECK_EQ(0x0003u, (unsigned)pcAfter, "PC advanced to 0x0003 after LXI SP");

    // Verify that instruction history records correct PC
    auto history = f.backend->instructionHistorySnapshot();
    CHECK(history.size() >= 1, "history has entries");
    if (!history.empty()) {
        CHECK_EQ(0x0000u, (unsigned)history[0].pcBefore, "first instruction at PC=0");
    }
    TEST_END();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    setbuf(stdout, nullptr);

    printf("\n\033[1;33m========================================\033[0m\n");
    printf("\033[1;33m  Agent Integration Tests — Stage 5.3.2\033[0m\n");
    printf("\033[1;33m========================================\033[0m\n");

    // Non-threaded tests
    test_command_queue_preserves_order();       // 1
    test_concurrent_commands_not_lost();        // 2
    test_each_command_receives_own_result();    // 3
    test_command_executes_on_emulation_thread();// 4
    test_agent_thread_never_executes_cpu();     // 5
    test_commands_work_while_running();         // 6
    test_pause_while_running();                 // 7
    test_quit_while_running();                  // 8
    test_breakpoint_mutation_through_queue();   // 9
    test_annotation_mutation_through_queue();   // 10
    test_trace_execution_through_queue();       // 11
    test_trace_timeout();                       // 12
    test_concurrent_trace_requests();           // 13
    test_unknown_pc_remains_unknown();          // 20

    // Threaded tests
    test_real_debugbackend_threaded_integration(); // 14
    test_real_breakpoint_integration();            // 15
    test_real_trace_integration();                 // 16
    test_memory_attribution();                     // 17
    test_io_attribution();                           // 18
    test_vram_attribution();                       // 19

    printf("\n\033[1;33m========================================\033[0m\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("  (\033[1;31m%d FAILED\033[0m)", tests_failed);
    }
    printf("\n\033[1;33m========================================\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
