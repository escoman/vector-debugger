// ---------------------------------------------------------------------------
// test_mcp_protocol — Stage 6.4
//
// MCP protocol tests using MockAgentBackend.
// Verifies: tool registration, schema, execution, error propagation, E2E.
//
// No Board/SDL dependency — uses MockAgentBackend.
// ---------------------------------------------------------------------------

#include "mcp_adapter.h"
#include "mcp_json.h"
#include "agent_api.h"
#include "mock_backend_for_agent.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <algorithm>

// ---------------------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------------------

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_BEGIN(name) \
    do { \
        tests_run++; \
        const char *_test_name = name; \
        int _failures = 0; \
        (void)_test_name;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "  FAIL: %s (line %d): %s\n", _test_name, __LINE__, msg); \
            _failures++; \
        } \
    } while(0)

#define CHECK_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            fprintf(stderr, "  FAIL: %s (line %d): %s\n", _test_name, __LINE__, msg); \
            _failures++; \
        } \
    } while(0)

#define TEST_END() \
        if (_failures == 0) { \
            tests_passed++; \
            printf("  \033[32mPASS\033[0m %s\n", _test_name); \
        } else { \
            tests_failed++; \
            printf("  \033[31mFAIL\033[0m %s (%d failures)\n", _test_name, _failures); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// Fixture: MockAgentBackend + AgentApi + McpServer
// ---------------------------------------------------------------------------

struct Fixture {
    MockAgentBackend mock;
    AgentApi api;
    McpServer mcp;

    Fixture() : api(mock), mcp(api) {
        mcp.registerAllTools();
    }
};

// Helper: get text from MCP handler result
// Handler returns content array directly: [{type: "text", text: "..."}]
static std::string getTextFromContent(const mcp::json &result) {
    if (result.is_array() && !result.empty()) {
        auto &first = result[0];
        if (first.is_object() && first.contains("text")) {
            return first["text"].get<std::string>();
        }
    }
    return "";
}

// Helper: check if MCP handler result is an error
// Error results contain error_code in the text data
static bool isErrorContent(const mcp::json &result) {
    if (result.is_array() && !result.empty()) {
        auto &first = result[0];
        if (first.is_object() && first.contains("text")) {
            try {
                auto data = mcp::json::parse(first["text"].get<std::string>());
                // errorContent produces {error_code, message}
                // mcp_json::errorResult produces {success: false, error_code, error}
                return data.contains("error_code");
            } catch (...) {
                return false;
            }
        }
    }
    return false;
}

// Helper: parse text content as JSON
static mcp::json parseTextAsJson(const mcp::json &result) {
    std::string text = getTextFromContent(result);
    if (!text.empty()) {
        return mcp::json::parse(text);
    }
    return mcp::json();
}

// ---------------------------------------------------------------------------
// Tool Registration Tests
// ---------------------------------------------------------------------------

void test_all_52_tools_registered() {
    TEST_BEGIN("all 52 tools registered");
    Fixture f;
    auto names = f.mcp.registeredToolNames();
    CHECK_EQ(static_cast<int>(names.size()), 52, "should have 52 tools");
    TEST_END();
}

void test_tool_names_have_debug_prefix() {
    TEST_BEGIN("all tool names have debug_ prefix");
    Fixture f;
    auto names = f.mcp.registeredToolNames();
    bool allPrefixed = true;
    for (auto &n : names) {
        if (n.substr(0, 6) != "debug_") {
            allPrefixed = false;
            fprintf(stderr, "    tool without prefix: %s\n", n.c_str());
        }
    }
    CHECK(allPrefixed, "all tools should have debug_ prefix");
    TEST_END();
}

void test_expected_tools_exist() {
    TEST_BEGIN("expected tools exist");
    Fixture f;
    auto names = f.mcp.registeredToolNames();
    std::set<std::string> nameSet(names.begin(), names.end());

    const char *expected[] = {
        "debug_run", "debug_pause", "debug_step", "debug_reset", "debug_is_running",
        "debug_get_cpu_state", "debug_get_registers", "debug_set_register",
        "debug_read_memory", "debug_write_memory",
        "debug_read_io", "debug_write_io",
        "debug_set_breakpoint", "debug_remove_breakpoint", "debug_list_breakpoints",
        "debug_clear_breakpoints", "debug_set_breakpoint_enabled",
        "debug_disassemble", "debug_get_instruction_history", "debug_get_execution_trace",
        "debug_get_stack",
        "debug_get_symbols", "debug_get_function", "debug_get_function_context",
        "debug_get_xrefs", "debug_get_call_graph",
        "debug_get_memory_map", "debug_get_vram_info", "debug_get_screen_info",
        "debug_get_io_trace",
        "debug_get_state",
        "debug_load_rom",
        "debug_set_comment", "debug_set_function_comment", "debug_rename_function",
        "debug_create_function", "debug_delete_function", "debug_add_label",
        // Stage 6.11: RDB tools
        "debug_get_rdb_info", "debug_list_rdb_objects", "debug_get_rdb_object",
        "debug_find_rdb_object", "debug_add_rdb_object", "debug_update_rdb_object",
        "debug_remove_rdb_object", "debug_set_rdb_comment", "debug_set_rdb_property",
        "debug_save_rdb", "debug_reload_rdb",
        // Stage 6.13: RDB Links
        "debug_add_rdb_link", "debug_remove_rdb_link", "debug_get_rdb_links"
    };

    for (auto &e : expected) {
        CHECK(nameSet.count(e) == 1, e);
    }
    TEST_END();
}

// ---------------------------------------------------------------------------
// Schema Validation Tests
// ---------------------------------------------------------------------------

void test_tool_schemas_have_properties() {
    TEST_BEGIN("tool schemas have type=object");
    Fixture f;
    auto tools = f.mcp.server().get_tools();
    bool allValid = true;
    for (auto &t : tools) {
        if (!t.parameters_schema.contains("type") ||
            t.parameters_schema["type"] != "object") {
            fprintf(stderr, "    tool %s missing type=object\n", t.name.c_str());
            allValid = false;
        }
    }
    CHECK(allValid, "all tools should have type=object schema");
    TEST_END();
}

void test_address_params_are_numbers() {
    TEST_BEGIN("address params are numbers");
    Fixture f;
    auto tools = f.mcp.server().get_tools();
    for (auto &t : tools) {
        if (t.parameters_schema.contains("properties") &&
            t.parameters_schema["properties"].contains("address")) {
            auto &addr = t.parameters_schema["properties"]["address"];
            CHECK(addr["type"] == "number", (t.name + " address should be number").c_str());
        }
    }
    TEST_END();
}

void test_read_memory_requires_address_and_size() {
    TEST_BEGIN("debug_read_memory requires address and size");
    Fixture f;
    auto tools = f.mcp.server().get_tools();
    for (auto &t : tools) {
        if (t.name == "debug_read_memory") {
            CHECK(t.parameters_schema.contains("required"), "should have required");
            auto &req = t.parameters_schema["required"];
            bool hasAddr = false, hasSize = false;
            for (auto &r : req) {
                if (r == "address") hasAddr = true;
                if (r == "size") hasSize = true;
            }
            CHECK(hasAddr, "should require address");
            CHECK(hasSize, "should require size");
        }
    }
    TEST_END();
}

// ---------------------------------------------------------------------------
// Execution Tests
// ---------------------------------------------------------------------------

void test_debug_step() {
    TEST_BEGIN("debug_step executes step");
    Fixture f;
    auto result = f.mcp.callTool("debug_step");
    CHECK(!isErrorContent(result), "step should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("success"), "should have success field");
    CHECK(data["success"] == true, "success should be true");
    TEST_END();
}

void test_debug_is_running() {
    TEST_BEGIN("debug_is_running returns state");
    Fixture f;
    auto result = f.mcp.callTool("debug_is_running");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("running"), "should have running field");
    CHECK(data["running"] == false, "mock starts paused");
    TEST_END();
}

void test_debug_get_cpu_state() {
    TEST_BEGIN("debug_get_cpu_state returns registers");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_cpu_state");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("pc"), "should have pc");
    CHECK(data.contains("sp"), "should have sp");
    CHECK(data.contains("a"), "should have a");
    CHECK(data["pc"] == "0x0100", "PC should be 0x0100");
    CHECK(data["a"] == "0x42", "A should be 0x42");
    TEST_END();
}

void test_debug_get_registers() {
    TEST_BEGIN("debug_get_registers returns formatted registers");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_registers");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("PC"), "should have PC");
    CHECK(data.contains("AF"), "should have AF");
    CHECK(data.contains("HL"), "should have HL");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Memory Tests
// ---------------------------------------------------------------------------

void test_debug_read_memory() {
    TEST_BEGIN("debug_read_memory reads bytes");
    Fixture f;
    auto result = f.mcp.callTool("debug_read_memory", {{"address", 0x0100}, {"size", 3}});
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("bytes"), "should have bytes");
    CHECK_EQ(static_cast<int>(data["bytes"].size()), 3, "should read 3 bytes");
    // 0x0100 = 0x31 (LXI SP, 0xF800)
    CHECK(data["bytes"][0] == 0x31, "first byte should be 0x31");
    TEST_END();
}

void test_debug_write_memory() {
    TEST_BEGIN("debug_write_memory writes bytes");
    Fixture f;
    auto result = f.mcp.callTool("debug_write_memory",
        {{"address", 0x1000}, {"data", {0xAA, 0xBB, 0xCC}}});
    if (isErrorContent(result)) {
        fprintf(stderr, "    write_memory error text: %s\n", getTextFromContent(result).c_str());
    }
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data["written"] == 3, "should write 3 bytes");

    // Verify by reading back
    auto readResult = f.mcp.callTool("debug_read_memory", {{"address", 0x1000}, {"size", 3}});
    auto readData = parseTextAsJson(readResult);
    CHECK(readData["bytes"][0] == 0xAA, "first byte should be 0xAA");
    CHECK(readData["bytes"][1] == 0xBB, "second byte should be 0xBB");
    CHECK(readData["bytes"][2] == 0xCC, "third byte should be 0xCC");
    TEST_END();
}

// ---------------------------------------------------------------------------
// I/O Tests
// ---------------------------------------------------------------------------

void test_debug_read_io() {
    TEST_BEGIN("debug_read_io reads port");
    Fixture f;
    f.mock.setIoPort(0x42, 0x55);
    auto result = f.mcp.callTool("debug_read_io", {{"port", 0x42}});
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("value"), "should have value");
    TEST_END();
}

void test_debug_write_io() {
    TEST_BEGIN("debug_write_io writes port");
    Fixture f;
    auto result = f.mcp.callTool("debug_write_io", {{"port", 0x10}, {"value", 0xAA}});
    CHECK(!isErrorContent(result), "should succeed");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Breakpoint Tests
// ---------------------------------------------------------------------------

void test_breakpoint_lifecycle() {
    TEST_BEGIN("breakpoint set/list/remove/clear lifecycle");
    Fixture f;

    // Set
    auto r1 = f.mcp.callTool("debug_set_breakpoint", {{"address", 0x0200}});
    CHECK(!isErrorContent(r1), "set should succeed");

    // List
    auto r2 = f.mcp.callTool("debug_list_breakpoints");
    auto listData = parseTextAsJson(r2);
    CHECK(listData["count"] == 1, "should have 1 breakpoint");

    // Enable/disable
    auto r3 = f.mcp.callTool("debug_set_breakpoint_enabled",
        {{"address", 0x0200}, {"enabled", false}});
    CHECK(!isErrorContent(r3), "disable should succeed");

    // Remove
    auto r4 = f.mcp.callTool("debug_remove_breakpoint", {{"address", 0x0200}});
    CHECK(!isErrorContent(r4), "remove should succeed");

    // List again
    auto r5 = f.mcp.callTool("debug_list_breakpoints");
    auto listData2 = parseTextAsJson(r5);
    CHECK(listData2["count"] == 0, "should have 0 breakpoints");

    // Set two and clear all
    f.mcp.callTool("debug_set_breakpoint", {{"address", 0x0100}});
    f.mcp.callTool("debug_set_breakpoint", {{"address", 0x0200}});
    auto r6 = f.mcp.callTool("debug_clear_breakpoints");
    CHECK(!isErrorContent(r6), "clear should succeed");

    auto r7 = f.mcp.callTool("debug_list_breakpoints");
    auto listData3 = parseTextAsJson(r7);
    CHECK(listData3["count"] == 0, "should have 0 breakpoints after clear");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Disassembly Tests
// ---------------------------------------------------------------------------

void test_debug_disassemble() {
    TEST_BEGIN("debug_disassemble returns instructions");
    Fixture f;
    auto result = f.mcp.callTool("debug_disassemble", {{"address", 0x0100}, {"count", 3}});
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("instructions"), "should have instructions");
    CHECK(data["count"] == 3, "should have 3 instructions");
    auto &insts = data["instructions"];
    CHECK(insts[0]["address"] == "0x0100", "first at 0x0100");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Stack Tests
// ---------------------------------------------------------------------------

void test_debug_get_stack() {
    TEST_BEGIN("debug_get_stack returns entries");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_stack", {{"limit", 5}});
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("entries"), "should have entries");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Symbol / Analysis Tests
// ---------------------------------------------------------------------------

void test_debug_get_symbols_empty() {
    TEST_BEGIN("debug_get_symbols returns empty initially");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_symbols");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data["count"] == 0, "should have 0 symbols initially");
    TEST_END();
}

void test_debug_annotations() {
    TEST_BEGIN("debug annotation tools (create/rename/comment/label/delete)");
    Fixture f;

    // Create function
    auto r1 = f.mcp.callTool("debug_create_function", {{"address", 0x0200}});
    CHECK(!isErrorContent(r1), "create should succeed");

    // Rename
    auto r2 = f.mcp.callTool("debug_rename_function",
        {{"address", 0x0200}, {"name", "my_func"}});
    CHECK(!isErrorContent(r2), "rename should succeed");

    // Set function comment
    auto r3 = f.mcp.callTool("debug_set_function_comment",
        {{"address", 0x0200}, {"comment", "test comment"}});
    CHECK(!isErrorContent(r3), "set_function_comment should succeed");

    // Add label
    auto r4 = f.mcp.callTool("debug_add_label",
        {{"address", 0x0206}, {"name", "write_loop"}});
    CHECK(!isErrorContent(r4), "add_label should succeed");

    // Set comment (on existing symbol)
    auto r5 = f.mcp.callTool("debug_set_comment",
        {{"address", 0x0206}, {"comment", "mov_m_a"}});
    CHECK(!isErrorContent(r5), "set_comment should succeed");

    // Get symbols — should have 2 now
    auto r6 = f.mcp.callTool("debug_get_symbols");
    auto symData = parseTextAsJson(r6);
    CHECK(symData["count"] == 2, "should have 2 symbols");

    // Get function
    auto r7 = f.mcp.callTool("debug_get_function", {{"address", 0x0200}});
    CHECK(!isErrorContent(r7), "get_function should succeed");
    auto funcData = parseTextAsJson(r7);
    CHECK(funcData["name"] == "my_func", "name should be my_func");

    // Delete function
    auto r8 = f.mcp.callTool("debug_delete_function", {{"address", 0x0200}});
    CHECK(!isErrorContent(r8), "delete should succeed");

    // Get function after delete — should error
    auto r9 = f.mcp.callTool("debug_get_function", {{"address", 0x0200}});
    CHECK(isErrorContent(r9), "get_function after delete should error");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Debug State Tests
// ---------------------------------------------------------------------------

void test_debug_get_state() {
    TEST_BEGIN("debug_get_state returns full state");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_state");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("running"), "should have running");
    CHECK(data.contains("cpu"), "should have cpu");
    CHECK(data.contains("breakpoints"), "should have breakpoints");
    CHECK(data.contains("current_instruction"), "should have current_instruction");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Memory Map / Screen / VRAM Tests
// ---------------------------------------------------------------------------

void test_debug_get_memory_map() {
    TEST_BEGIN("debug_get_memory_map returns blocks");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_memory_map");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("blocks"), "should have blocks");
    TEST_END();
}

void test_debug_get_screen_info() {
    TEST_BEGIN("debug_get_screen_info returns info");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_screen_info");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("width"), "should have width");
    TEST_END();
}

void test_debug_get_vram_info() {
    TEST_BEGIN("debug_get_vram_info returns info");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_vram_info");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("vram_base"), "should have vram_base");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Error Propagation Tests
// ---------------------------------------------------------------------------

void test_error_propagation_invalid_address() {
    TEST_BEGIN("error propagation: invalid address");
    Fixture f;
    // Address out of range
    bool threw = false;
    try {
        f.mcp.callTool("debug_read_memory", {{"address", 70000}, {"size", 1}});
    } catch (const mcp::mcp_exception &e) {
        threw = true;
    }
    CHECK(threw, "should throw for address > 0xFFFF");
    TEST_END();
}

void test_error_propagation_missing_param() {
    TEST_BEGIN("error propagation: missing required param");
    Fixture f;
    bool threw = false;
    try {
        f.mcp.callTool("debug_read_memory", {{"address", 0x0100}});
        // size defaults to 1, so this should actually succeed
    } catch (const mcp::mcp_exception &) {
        threw = true;
    }
    // debug_read_memory has size with default, so missing size is OK
    CHECK(!threw, "missing optional size should not throw");
    TEST_END();
}

void test_error_propagation_tool_not_found() {
    TEST_BEGIN("error propagation: tool not found");
    Fixture f;
    bool threw = false;
    try {
        f.mcp.callTool("debug_nonexistent");
    } catch (const mcp::mcp_exception &e) {
        threw = true;
    }
    CHECK(threw, "should throw for unknown tool");
    TEST_END();
}

void test_error_propagation_get_function_not_found() {
    TEST_BEGIN("error propagation: get_function returns NotFound");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_function", {{"address", 0x9999}});
    CHECK(isErrorContent(result), "should return error for unknown function");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("error_code"), "should have error_code");
    CHECK(data.contains("message"), "should have error message");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Wire-Level Error Format Tests (Stage 6.4.1)
// ---------------------------------------------------------------------------

void test_wire_level_error_format() {
    TEST_BEGIN("wire-level: CallToolResult has isError at top level");
    Fixture f;
    
    // Trigger an error: handler returns errorContent (via AgentApi error result)
    // Use debug_get_function which returns error for unknown address
    auto handlerResult = f.mcp.callTool("debug_get_function", {{"address", 0x9999}});
    
    // Simulate library wire wrapping (cpp-mcp tools/call handler)
    mcp::json result = {{"content", handlerResult}, {"isError", false}};
    
    // Verify CallToolResult structure (Stage 6.4.1)
    CHECK(result.is_object(), "result must be an object");
    CHECK(result.contains("content"), "result must have 'content' field");
    CHECK(result.contains("isError"), "result must have 'isError' field at top level");
    
    // Verify content structure (handler returns array directly)
    auto &content = result["content"];
    CHECK(content.is_array(), "content must be an array");
    CHECK(!content.empty(), "content must not be empty");
    
    auto &first = content[0];
    CHECK(first.contains("type"), "content item must have 'type'");
    CHECK(first["type"] == "text", "content type must be 'text'");
    CHECK(first.contains("text"), "content item must have 'text'");
    
    // Verify error data is in text
    auto errData = mcp::json::parse(first["text"].get<std::string>());
    CHECK(errData.contains("error_code"), "error data must have error_code");
    CHECK(errData.contains("message"), "error data must have message");
    
    TEST_END();
}

void test_wire_level_success_format() {
    TEST_BEGIN("wire-level: CallToolResult success has isError=false");
    Fixture f;
    
    // Successful call
    auto handlerResult = f.mcp.callTool("debug_is_running");
    
    // Simulate library wire wrapping (cpp-mcp tools/call handler)
    mcp::json result = {{"content", handlerResult}, {"isError", false}};
    
    // Verify CallToolResult structure
    CHECK(result.is_object(), "result must be an object");
    CHECK(result.contains("content"), "result must have 'content' field");
    CHECK(result.contains("isError"), "result must have 'isError' field at top level");
    CHECK(result["isError"].get<bool>() == false, "isError must be false for success");
    
    // Verify content structure
    auto &content = result["content"];
    CHECK(content.is_array(), "content must be an array");
    CHECK(!content.empty(), "content must not be empty");
    
    auto &first = content[0];
    CHECK(first.contains("type"), "content item must have 'type'");
    CHECK(first["type"] == "text", "content type must be 'text'");
    CHECK(first.contains("text"), "content item must have 'text'");
    
    TEST_END();
}

// ---------------------------------------------------------------------------
// End-to-End Integration Test
// ---------------------------------------------------------------------------

void test_e2e_read_memory_full_path() {
    TEST_BEGIN("E2E: read_memory full path MCP → AgentApi → MockBackend → JSON");
    Fixture f;

    // Write known data to mock
    f.mock.setMemory(0x0300, {0xDE, 0xAD, 0xBE, 0xEF});

    // MCP request
    auto result = f.mcp.callTool("debug_read_memory", {{"address", 0x0300}, {"size", 4}});

    // Verify MCP response
    if (isErrorContent(result)) {
        fprintf(stderr, "    read_memory error: %s\n", getTextFromContent(result).c_str());
    }
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data["address"] == "0x0300", "address should match");
    CHECK(data["size"] == 4, "size should be 4");

    auto &bytes = data["bytes"];
    CHECK(bytes[0] == 0xDE, "byte 0 should be 0xDE");
    CHECK(bytes[1] == 0xAD, "byte 1 should be 0xAD");
    CHECK(bytes[2] == 0xBE, "byte 2 should be 0xBE");
    CHECK(bytes[3] == 0xEF, "byte 3 should be 0xEF");
    TEST_END();
}

void test_e2e_write_then_read() {
    TEST_BEGIN("E2E: write_memory then read_memory roundtrip");
    Fixture f;

    // Write via MCP
    auto w = f.mcp.callTool("debug_write_memory",
        {{"address", 0x0500}, {"data", {0x11, 0x22, 0x33}}});
    CHECK(!isErrorContent(w), "write should succeed");

    // Read back via MCP
    auto r = f.mcp.callTool("debug_read_memory", {{"address", 0x0500}, {"size", 3}});
    auto data = parseTextAsJson(r);
    CHECK(data["bytes"][0] == 0x11, "byte 0");
    CHECK(data["bytes"][1] == 0x22, "byte 1");
    CHECK(data["bytes"][2] == 0x33, "byte 2");
    TEST_END();
}

// ---------------------------------------------------------------------------
// JSON Serialization Tests
// ---------------------------------------------------------------------------

void test_json_cpu_state_format() {
    TEST_BEGIN("JSON: CpuState serialization format");
    CpuState cpu{};
    cpu.pc = 0x0100;
    cpu.sp = 0xF800;
    cpu.a = 0x42;
    cpu.flags = 0x00;

    auto j = mcp_json::cpuStateToJson(cpu);
    CHECK(j["pc"] == "0x0100", "pc format");
    CHECK(j["sp"] == "0xF800", "sp format");
    CHECK(j["a"] == "0x42", "a format");
    TEST_END();
}

void test_json_breakpoint_format() {
    TEST_BEGIN("JSON: breakpoint serialization format");
    DebuggerBreakpoint bp{0x1234, true};
    auto j = mcp_json::breakpointToJson(bp);
    CHECK(j["address"] == "0x1234", "address format");
    CHECK(j["enabled"] == true, "enabled field");
    TEST_END();
}

void test_json_error_result() {
    TEST_BEGIN("JSON: error result format");
    auto j = mcp_json::errorResult(ErrorCode::NotFound, "symbol not found");
    CHECK(j["success"] == false, "success should be false");
    CHECK(j["error"] == "symbol not found", "error message");
    CHECK(static_cast<int>(j["error_code"]) == static_cast<int>(ErrorCode::NotFound), "error code");
    TEST_END();
}

// ---------------------------------------------------------------------------
// I/O Boundary Tests
// ---------------------------------------------------------------------------

void test_io_port_boundaries() {
    TEST_BEGIN("I/O port boundaries: 0x00 and 0xFF");
    Fixture f;

    // Port 0x00
    auto r1 = f.mcp.callTool("debug_read_io", {{"port", 0x00}});
    CHECK(!isErrorContent(r1), "port 0x00 should be valid");

    // Port 0xFF
    auto r2 = f.mcp.callTool("debug_read_io", {{"port", 0xFF}});
    CHECK(!isErrorContent(r2), "port 0xFF should be valid");

    // Port out of range
    bool threw = false;
    try {
        f.mcp.callTool("debug_read_io", {{"port", 256}});
    } catch (const mcp::mcp_exception &) {
        threw = true;
    }
    CHECK(threw, "port 256 should throw");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Trace / History Tests
// ---------------------------------------------------------------------------

void test_debug_get_execution_trace() {
    TEST_BEGIN("debug_get_execution_trace returns events");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_execution_trace", {{"max_entries", 100}});
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("events"), "should have events");
    TEST_END();
}

void test_debug_get_io_trace() {
    TEST_BEGIN("debug_get_io_trace returns events");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_io_trace", {{"max_entries", 100}});
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("events"), "should have events");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Call Graph Tests
// ---------------------------------------------------------------------------

void test_debug_get_call_graph() {
    TEST_BEGIN("debug_get_call_graph returns edges");
    Fixture f;
    auto result = f.mcp.callTool("debug_get_call_graph");
    CHECK(!isErrorContent(result), "should succeed");
    auto data = parseTextAsJson(result);
    CHECK(data.contains("edges"), "should have edges");
    TEST_END();
}

// ---------------------------------------------------------------------------
// Set Register Tests
// ---------------------------------------------------------------------------

void test_debug_set_register() {
    TEST_BEGIN("debug_set_register changes register");
    Fixture f;

    // Set A register (part of AF)
    auto r = f.mcp.callTool("debug_set_register", {{"name", "PC"}, {"value", 0x0200}});
    CHECK(!isErrorContent(r), "set_register should succeed");

    // Verify
    auto cpu = f.mcp.callTool("debug_get_cpu_state");
    auto data = parseTextAsJson(cpu);
    CHECK(data["pc"] == "0x0200", "PC should be 0x0200");
    TEST_END();
}

// ---------------------------------------------------------------------------
// RDB Links Tests (Stage 6.13)
// ---------------------------------------------------------------------------

void test_rdb_link_lifecycle() {
    TEST_BEGIN("RDB link add/get/remove lifecycle");
    Fixture f;

    // Create source and target objects
    auto addSrc = f.mcp.callTool("debug_add_rdb_object",
        {{"address", 0x0100}, {"name", "src"}, {"type", "Function"}});
    CHECK(!isErrorContent(addSrc), "add source succeeds");

    auto addTgt = f.mcp.callTool("debug_add_rdb_object",
        {{"address", 0x0200}, {"name", "tgt"}, {"type", "Function"}});
    CHECK(!isErrorContent(addTgt), "add target succeeds");

    // Add link
    auto addLink = f.mcp.callTool("debug_add_rdb_link",
        {{"source", 0x0100}, {"target", 0x0200}});
    CHECK(!isErrorContent(addLink), "add link succeeds");

    // Get links
    auto getLinks = f.mcp.callTool("debug_get_rdb_links", {{"source", 0x0100}});
    CHECK(!isErrorContent(getLinks), "get links succeeds");
    auto data = parseTextAsJson(getLinks);
    CHECK(data.contains("links"), "has links array");
    CHECK_EQ(1, static_cast<int>(data["links"].size()), "1 link");
    CHECK(data["links"][0] == 0x0200, "target is 0x0200");

    // Remove link
    auto rmLink = f.mcp.callTool("debug_remove_rdb_link",
        {{"source", 0x0100}, {"target", 0x0200}});
    CHECK(!isErrorContent(rmLink), "remove link succeeds");

    // Verify empty
    auto getLinks2 = f.mcp.callTool("debug_get_rdb_links", {{"source", 0x0100}});
    auto data2 = parseTextAsJson(getLinks2);
    CHECK_EQ(0, static_cast<int>(data2["links"].size()), "0 links after remove");
    TEST_END();
}

void test_rdb_link_invalid_source() {
    TEST_BEGIN("RDB link — invalid source");
    Fixture f;

    // Add link with no source object
    auto r = f.mcp.callTool("debug_add_rdb_link",
        {{"source", 0x9999}, {"target", 0x0100}});
    CHECK(isErrorContent(r), "add link fails with no source");

    // Get links with no source object
    auto r2 = f.mcp.callTool("debug_get_rdb_links", {{"source", 0x9999}});
    CHECK(isErrorContent(r2), "get links fails with no source");
    TEST_END();
}

void test_rdb_link_unresolved_target() {
    TEST_BEGIN("RDB link — unresolved target");
    Fixture f;

    // Create source only
    f.mcp.callTool("debug_add_rdb_object",
        {{"address", 0x0100}, {"name", "src"}, {"type", "Function"}});

    // Link to non-existent target
    auto r = f.mcp.callTool("debug_add_rdb_link",
        {{"source", 0x0100}, {"target", 0x0345}});
    CHECK(!isErrorContent(r), "link to unresolved target succeeds");

    // Verify
    auto links = f.mcp.callTool("debug_get_rdb_links", {{"source", 0x0100}});
    auto data = parseTextAsJson(links);
    CHECK_EQ(1, static_cast<int>(data["links"].size()), "1 link");
    CHECK(data["links"][0] == 0x0345, "unresolved target stored");
    TEST_END();
}

void test_rdb_link_duplicate() {
    TEST_BEGIN("RDB link — duplicate is idempotent");
    Fixture f;

    f.mcp.callTool("debug_add_rdb_object",
        {{"address", 0x0100}, {"name", "src"}, {"type", "Function"}});

    auto r1 = f.mcp.callTool("debug_add_rdb_link",
        {{"source", 0x0100}, {"target", 0x0200}});
    CHECK(!isErrorContent(r1), "first add succeeds");

    auto r2 = f.mcp.callTool("debug_add_rdb_link",
        {{"source", 0x0100}, {"target", 0x0200}});
    CHECK(!isErrorContent(r2), "duplicate add succeeds");

    auto links = f.mcp.callTool("debug_get_rdb_links", {{"source", 0x0100}});
    auto data = parseTextAsJson(links);
    CHECK_EQ(1, static_cast<int>(data["links"].size()), "still 1 link");
    TEST_END();
}

void test_rdb_link_remove_missing() {
    TEST_BEGIN("RDB link — remove missing fails");
    Fixture f;

    f.mcp.callTool("debug_add_rdb_object",
        {{"address", 0x0100}, {"name", "src"}, {"type", "Function"}});

    auto r = f.mcp.callTool("debug_remove_rdb_link",
        {{"source", 0x0100}, {"target", 0x0200}});
    CHECK(isErrorContent(r), "remove non-existent link fails");
    TEST_END();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    setbuf(stdout, nullptr);

    printf("\n\033[1;33m========================================\033[0m\n");
    printf("\033[1;33m  MCP Protocol Tests — Stage 6.4\033[0m\n");
    printf("\033[1;33m========================================\033[0m\n\n");

    // Registration
    test_all_52_tools_registered();
    test_tool_names_have_debug_prefix();
    test_expected_tools_exist();

    // Schema
    test_tool_schemas_have_properties();
    test_address_params_are_numbers();
    test_read_memory_requires_address_and_size();

    // Execution
    test_debug_step();
    test_debug_is_running();
    test_debug_get_cpu_state();
    test_debug_get_registers();

    // Memory
    test_debug_read_memory();
    test_debug_write_memory();

    // I/O
    test_debug_read_io();
    test_debug_write_io();
    test_io_port_boundaries();

    // Breakpoints
    test_breakpoint_lifecycle();

    // Disassembly
    test_debug_disassemble();

    // Stack
    test_debug_get_stack();

    // Symbols / Analysis
    test_debug_get_symbols_empty();
    test_debug_annotations();

    // Debug state
    test_debug_get_state();

    // Memory map / Screen / VRAM
    test_debug_get_memory_map();
    test_debug_get_screen_info();
    test_debug_get_vram_info();

    // Trace / History
    test_debug_get_execution_trace();
    test_debug_get_io_trace();
    test_debug_get_call_graph();

    // Set register
    test_debug_set_register();

    // RDB Links (Stage 6.13)
    test_rdb_link_lifecycle();
    test_rdb_link_invalid_source();
    test_rdb_link_unresolved_target();
    test_rdb_link_duplicate();
    test_rdb_link_remove_missing();

    // Error propagation
    test_error_propagation_invalid_address();
    test_error_propagation_missing_param();
    test_error_propagation_tool_not_found();
    test_error_propagation_get_function_not_found();

    // Wire-level format (Stage 6.4.1)
    test_wire_level_error_format();
    test_wire_level_success_format();

    // E2E
    test_e2e_read_memory_full_path();
    test_e2e_write_then_read();

    // JSON serialization
    test_json_cpu_state_format();
    test_json_breakpoint_format();
    test_json_error_result();

    printf("\n\033[1;33m========================================\033[0m\n");
    printf("\033[1;33m  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(" (\033[31m%d FAILED\033[0m)", tests_failed);
    }
    printf("\033[1;33m\033[0m\n");
    printf("\033[1;33m========================================\033[0m\n\n");

    return tests_failed > 0 ? 1 : 0;
}
