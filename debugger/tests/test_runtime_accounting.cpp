// Runtime memory accounting tests — Stage 6.22 §1–§4
//
// The free-run path (Board/NoBoardTarget executing instructions without the
// debugger stepping them) used to report zero fetches, a port-trace sequence
// that never moved, and an execute_activity field that was always 0.  These
// tests drive the real DebugBackend + NoBoardTarget over a fixed program and
// pin the numbers down.
//
// Program (11 instruction bytes, 1 data read + 1 data write per loop):
//   0x0100  3E 42     MVI A,42h
//   0x0102  32 00 02  STA 0200h
//   0x0105  3A 01 02  LDA 0200h
//   0x0108  C3 00 01  JMP 0100h
//   0x0200  ..        data byte written and read back every loop

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "memory.h"
#include "i8080.h"
#include "i8080_hal.h"
#include "no_board_target.h"
#include "backend.h"
#include "mcp_json.h"

using namespace i8080cpu;

static Memory *test_memory = nullptr;

int i8080_hal_memory_read_byte(int addr)  { return test_memory->read(addr, false); }
void i8080_hal_memory_write_byte(int addr, int value) { test_memory->write(addr, value, false); }
int i8080_hal_memory_read_word(int addr, bool stack)
{
    return test_memory->read(addr, stack) | (test_memory->read(addr + 1, stack) << 8);
}
void i8080_hal_memory_write_word(int addr, int word, bool stack)
{
    test_memory->write(addr, word & 0xff, stack);
    test_memory->write(addr + 1, word >> 8, stack);
}
int i8080_hal_io_input(int port) { (void)port; return 0xFF; }
void i8080_hal_io_output(int port, int value) { (void)port; (void)value; }
void i8080_hal_iff(int on) { (void)on; }

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
        auto _e = (exp); auto _a = (act); \
        if (_e != _a) { \
            printf("  \033[41;97m FAIL \033[0m %s: expected %lld, got %lld (line %d)\n", \
                   msg, (long long)_e, (long long)_a, __LINE__); \
            _test_ok = false; \
        } else { \
            printf("  \033[46;30m ok \033[0m %s\n", msg); \
        } \
    } while(0)

#define TEST_END() \
        if (_test_ok) { \
            tests_passed++; \
            printf("  \033[46;30m PASSED \033[0m %s\n", _test_name); \
        } else { \
            tests_failed++; \
            printf("  \033[41;97m FAILED \033[0m %s\n", _test_name); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

static const uint8_t kProgram[] = {
    0x3E, 0x42,             // 0x0100  MVI A,42h
    0x32, 0x00, 0x02,       // 0x0102  STA 0200h
    0x3A, 0x01, 0x02,       // 0x0105  LDA 0200h
    0xC3, 0x00, 0x01,       // 0x0108  JMP 0100h
};
static const int    kOrigin   = 0x0100;
static const size_t kProgLen  = sizeof(kProgram);
static const int    kBytesPerLoop = 11;   // 2 + 3 + 3 + 3

struct Fixture
{
    Memory        mem;
    NoBoardTarget target;
    DebugBackend  backend;

    Fixture() : target(mem), backend(target)
    {
        test_memory = &mem;
        for (size_t i = 0; i < kProgLen; ++i)
            mem.write(static_cast<uint32_t>(kOrigin + i), kProgram[i], false);
        i8080_init();
        i8080_jump(kOrigin);
        i8080_setreg_sp(0xC000);
        // The program bytes above were written while the instrumentation was
        // already live — they would show up as 11 writes to the code block and
        // as log entries with pc 0.  Counters must describe the run, not the
        // loader.
        backend.clearRuntimeAccessMap();
    }
};

static RuntimeAccessBlock blockOf(DebugBackend &be, int block)
{
    auto map = be.getRuntimeAccessMap();
    return map[block];
}

// ---------------------------------------------------------------------------
// Test 1 — §1: fetches are accounted for in the free run
// ---------------------------------------------------------------------------

static void test_free_run_fetch_accounting()
{
    TEST_BEGIN("free run counts instruction fetches separately from data reads");
    Fixture f;
    const int loops = 10;

    // NoBoardTarget::executeFrame() runs exactly one instruction.
    for (int i = 0; i < loops * 4; ++i)
        f.target.executeFrame();

    const RuntimeAccessBlock code = blockOf(f.backend, kOrigin >> 8);
    const RuntimeAccessBlock data = blockOf(f.backend, 0x0200 >> 8);

    CHECK(code.fetch, "code block marked fetch");
    CHECK_EQ(loops * kBytesPerLoop, (int)code.fetch_count,
             "fetch_count = bytes of the instructions executed");
    CHECK_EQ(0, (int)code.read_count,
             "operand bytes are fetches, not data reads — nothing left in read_count");
    CHECK_EQ(0, (int)code.write_count, "no writes into the code block (no self-modifying code)");

    CHECK(!data.fetch, "data block is never marked fetch");
    CHECK_EQ(loops, (int)data.read_count, "LDA reads the data byte once per loop");
    CHECK_EQ(loops, (int)data.write_count, "STA writes the data byte once per loop");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 2 — §1: step and free run agree on the same stretch of code
// ---------------------------------------------------------------------------

static void test_step_and_free_run_agree()
{
    TEST_BEGIN("debug_step and the free run produce the same access map");
    const int instrs = 40;

    Fixture free_run;
    for (int i = 0; i < instrs; ++i) free_run.target.executeFrame();
    auto byRun = free_run.backend.getRuntimeAccessMap();

    Fixture stepped;
    for (int i = 0; i < instrs; ++i) stepped.backend.stepInstruction();
    auto byStep = stepped.backend.getRuntimeAccessMap();

    int diffBlocks = 0;
    for (int b = 0; b < 256; ++b) {
        if (byRun[b].fetch_count != byStep[b].fetch_count ||
            byRun[b].read_count  != byStep[b].read_count  ||
            byRun[b].write_count != byStep[b].write_count) {
            ++diffBlocks;
        }
    }
    CHECK_EQ(0, diffBlocks, "every block matches between the two execution paths");
    CHECK(byRun[1].fetch_count > 0, "and the numbers are not trivially zero");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 3 — §2: the instruction sequence advances during the free run
// ---------------------------------------------------------------------------

static void test_sequence_advances_in_free_run()
{
    TEST_BEGIN("instruction sequence grows in the free run and matches the step path");
    Fixture f;

    // First instruction of a fresh session is number 0 — the same number the
    // step path gives it, so a log entry cannot tell which path produced it.
    f.target.executeFrame();
    auto log = f.backend.getRuntimeAccessLog(100);
    CHECK(!log.empty(), "free run produced runtime access log entries");
    CHECK_EQ(0u, (unsigned)(log.front().address - log.front().pc),
             "first fetch is at pc + 0");

    uint64_t seqAfterOne = f.backend.instructionSequence();
    CHECK_EQ(0u, (unsigned)seqAfterOne, "sequence still 0 after the first instruction");

    for (int i = 0; i < 9; ++i) f.target.executeFrame();
    CHECK_EQ(9u, (unsigned)f.backend.instructionSequence(),
             "sequence reached 9 after 10 free-run instructions");

    // Stepping keeps its own long-standing numbering: one per instruction.
    Fixture s;
    for (int i = 0; i < 3; ++i) s.backend.stepInstruction();
    CHECK_EQ(3u, (unsigned)s.backend.instructionSequence(),
             "3 steps leave sequence at 3");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 4 — §1: the log's pc is the address of the accessing instruction
// ---------------------------------------------------------------------------

static void test_log_pc_and_fetch_window()
{
    TEST_BEGIN("log entries carry the accessing instruction and fetches stay inside it");
    Fixture f;
    for (int i = 0; i < 4; ++i) f.target.executeFrame();   // one full loop

    auto log = f.backend.getRuntimeAccessLog(100);
    int fetches = 0, reads = 0;
    bool windowOk = true;
    for (const auto &e : log) {
        if (e.type == RuntimeAccessLogEntry::Fetch) {
            ++fetches;
            if (e.address < e.pc || e.address > e.pc + 2) windowOk = false;
        } else if (e.type == RuntimeAccessLogEntry::Read) {
            ++reads;
        }
        if (e.pc < kOrigin || e.pc > kOrigin + (int)kProgLen) windowOk = false;
    }
    CHECK_EQ(kBytesPerLoop, fetches, "one loop = 11 fetched bytes logged");
    CHECK(fetches > 0, "fetch entries exist at all (regression of §1)");
    CHECK(windowOk, "every fetch lies within pc..pc+2 and pc is an instruction address");

    bool dataReadTagged = false;
    for (const auto &e : log)
        if (e.type == RuntimeAccessLogEntry::Read && e.address == 0x0201 && e.pc == 0x0105)
            dataReadTagged = true;
    CHECK(dataReadTagged, "the LDA data read is logged against the LDA instruction (0x0105)");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 5 — §4: execute counters are filled by the free run
// ---------------------------------------------------------------------------

static void test_execute_activity_in_free_run()
{
    TEST_BEGIN("per-address execute counters grow in the free run");
    Fixture f;
    for (int i = 0; i < 40; ++i) f.target.executeFrame();   // 10 loops

    auto snap = f.backend.activitySnapshot();
    CHECK_EQ(10u, (unsigned)snap.executeCount[0x0100], "MVI A executed 10 times");
    CHECK_EQ(10u, (unsigned)snap.executeCount[0x0102], "STA 0200h executed 10 times");
    CHECK_EQ(10u, (unsigned)snap.executeCount[0x0105], "LDA 0200h executed 10 times");
    CHECK_EQ(10u, (unsigned)snap.executeCount[0x0108], "JMP executed 10 times");
    CHECK_EQ(0u, (unsigned)snap.executeCount[0x0103], "an operand byte is not an entry point");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Test 6 — §3: active_blocks counts what the payload actually lists
// ---------------------------------------------------------------------------

static void test_active_blocks_match_payload()
{
    TEST_BEGIN("active block count matches the serialised block list");
    Fixture f;
    for (int i = 0; i < 40; ++i) f.target.executeFrame();   // 10 loops

    const auto map = f.backend.getRuntimeAccessMap();        // all 256 blocks

    int active = 0;
    for (const auto &b : map)
        if (mcp_json::runtimeBlockActive(b)) ++active;
    const auto arr = mcp_json::runtimeAccessBlocksToJson(map);

    CHECK_EQ(256u, (unsigned)map.size(), "the map keeps all 256 blocks");
    CHECK_EQ((unsigned)active, (unsigned)arr.size(),
             "active_blocks == len(blocks) — both use the same predicate");
    CHECK(active > 0 && active < 256, "the filter really filters");

    // The code block is fetched-only: no data read and no write happens in
    // 0x0100–0x01FF.  Until §1 landed it was invisible to the MCP response.
    bool codeListed = false, dataList = false;
    for (const auto &j : arr) {
        const std::string addr = j.value("address", std::string());
        if (addr == mcp_json::hex16(0x0100)) {
            codeListed = true;
            CHECK(j["fetch"].get<bool>(), "code block carries the fetch flag");
            CHECK_EQ(110, (int)j["fetch_count"].get<int>(), "110 fetched bytes");
            CHECK_EQ(0, (int)j["read_count"].get<int>(), "and no data reads attributed to it");
        }
        if (addr == mcp_json::hex16(0x0200)) dataList = true;
    }
    CHECK(codeListed, "a fetch-only block is listed in debug_get_memory_access_map");
    CHECK(dataList, "the data block is listed too");
    TEST_END();
}

// ---------------------------------------------------------------------------

int main()
{
    std::printf("[runtime_accounting] Stage 6.22 §1–§4\n");

    test_free_run_fetch_accounting();
    test_step_and_free_run_agree();
    test_sequence_advances_in_free_run();
    test_log_pc_and_fetch_window();
    test_execute_activity_in_free_run();
    test_active_blocks_match_payload();

    std::printf("\n[runtime_accounting] %d/%d tests passed (%d failed)\n",
                tests_passed, tests_run, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
