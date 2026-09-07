// ---------------------------------------------------------------------------
// mcp_adapter.cpp — Stage 6.4
//
// McpServer implementation: registers 38 debug_* tools as thin wrappers
// over AgentApi methods. Each handler: parse params → call AgentApi →
// serialize result to MCP content.
//
// MCP → AgentApi only. Never touches Board, Memory, DebugAdapter directly.
// ---------------------------------------------------------------------------

#include "mcp_adapter.h"
#include "mcp_json.h"

#include <algorithm>
#include <cstdint>
#include <sstream>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

McpServer::McpServer(AgentApi &api)
    : api_(api)
{
    mcp::server::configuration conf;
    conf.name = "v06c-mcp";
    conf.version = "0.6.4";
    server_ = std::make_unique<mcp::server>(conf);
    server_->set_server_info("v06c-mcp", "0.6.4");
    server_->set_capabilities({{"tools", {{"listChanged", false}}}});
}

McpServer::~McpServer() = default;

// ---------------------------------------------------------------------------
// MCP CallToolResult helpers (Stage 6.4.1)
//
// MCP protocol requires CallToolResult format:
// {
//   "content": [{"type": "text", "text": "..."}],
//   "isError": true/false
// }
// ---------------------------------------------------------------------------

mcp::json McpServer::textContent(const mcp::json &data) {
    return mcp::json::array({
        {{"type", "text"}, {"text", data.dump(2)}}
    });
}

mcp::json McpServer::errorContent(const std::string &errorCode, const std::string &message) {
    mcp::json errData = {
        {"error_code", errorCode},
        {"message",    message}
    };
    return mcp::json::array({
        {{"type", "text"}, {"text", errData.dump(2)}}
    });
}

mcp::json McpServer::requireVoidResult(const AgentApiResult<void> &result) {
    if (!result.success) {
        throw mcp::mcp_exception(mcp::error_code::internal_error, result.error_message);
    }
    return textContent(mcp_json::successVoidResult());
}

// ---------------------------------------------------------------------------
// Register all tools
// ---------------------------------------------------------------------------

void McpServer::registerAllTools() {
    registerExecutionTools();
    registerCpuTools();
    registerMemoryTools();
    registerIoTools();
    registerBreakpointTools();
    registerDisassemblyTools();
    registerStackTools();
    registerSymbolTools();
    registerMemoryMapTools();
    registerIoTraceTools();
    registerDebugStateTools();
    registerRomTools();
    registerAnnotationTools();
}

void McpServer::runStdio() {
    server_->start_stdio();
}

std::vector<std::string> McpServer::registeredToolNames() const {
    std::vector<std::string> names;
    for (auto &t : server_->get_tools()) {
        names.push_back(t.name);
    }
    return names;
}

void McpServer::registerTool(const mcp::tool &tool, mcp::tool_handler handler) {
    handlers_[tool.name] = handler;
    server_->register_tool(tool, handler);
}

mcp::json McpServer::callTool(const std::string &toolName, const mcp::json &params) {
    auto it = handlers_.find(toolName);
    if (it == handlers_.end()) {
        throw mcp::mcp_exception(mcp::error_code::method_not_found,
            "tool not found: " + toolName);
    }
    return it->second(params, "test_session");
}

// ---------------------------------------------------------------------------
// Schema enhancement helpers (Stage 6.4.1)
//
// cpp-mcp tool_builder doesn't support numeric constraints directly.
// We manually add minimum/maximum to parameter schemas where appropriate.
// ---------------------------------------------------------------------------

static void addNumericConstraint(mcp::tool &tool, const std::string &paramName,
                                  std::optional<int> minimum = std::nullopt,
                                  std::optional<int> maximum = std::nullopt) {
    if (!tool.parameters_schema.contains("properties")) return;
    auto &props = tool.parameters_schema["properties"];
    if (!props.contains(paramName)) return;
    
    auto &paramSchema = props[paramName];
    if (minimum.has_value()) {
        paramSchema["minimum"] = minimum.value();
    }
    if (maximum.has_value()) {
        paramSchema["maximum"] = maximum.value();
    }
}

// ---------------------------------------------------------------------------
// Param extraction helpers
// ---------------------------------------------------------------------------

static uint16_t getAddress(const mcp::json &params) {
    if (!params.contains("address")) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: address");
    }
    int addr = params["address"].get<int>();
    if (addr < 0 || addr > 0xFFFF) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params, "address out of range 0x0000..0xFFFF");
    }
    return static_cast<uint16_t>(addr);
}

static size_t getCount(const mcp::json &params, const char *name, size_t defaultVal) {
    if (!params.contains(name)) return defaultVal;
    int v = params[name].get<int>();
    if (v < 0) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            std::string(name) + " must be >= 0");
    }
    return static_cast<size_t>(v);
}

static uint8_t getUint8(const mcp::json &params, const char *name) {
    if (!params.contains(name)) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            std::string("missing required parameter: ") + name);
    }
    int v = params[name].get<int>();
    if (v < 0 || v > 0xFF) {
        throw mcp::mcp_exception(mcp::error_code::invalid_params,
            std::string(name) + " out of range 0x00..0xFF");
    }
    return static_cast<uint8_t>(v);
}

// ---------------------------------------------------------------------------
// Execution tools (5)
// ---------------------------------------------------------------------------

void McpServer::registerExecutionTools() {
    // debug_run
    {
        auto tool = mcp::tool_builder("debug_run")
            .with_description("Start or resume CPU execution.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.run();
            if (!r.success) return errorContent("run_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_pause
    {
        auto tool = mcp::tool_builder("debug_pause")
            .with_description("Pause CPU execution.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.pause();
            if (!r.success) return errorContent("pause_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_step
    {
        auto tool = mcp::tool_builder("debug_step")
            .with_description("Execute exactly one CPU instruction while paused.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.step();
            if (!r.success) return errorContent("step_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_reset
    {
        auto tool = mcp::tool_builder("debug_reset")
            .with_description("Reset the CPU to initial state.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.reset();
            if (!r.success) return errorContent("reset_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_is_running
    {
        auto tool = mcp::tool_builder("debug_is_running")
            .with_description("Check if the CPU is currently running.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            bool running = api_.isRunning();
            return textContent({{"running", running}});
        });
    }
}

// ---------------------------------------------------------------------------
// CPU tools (3)
// ---------------------------------------------------------------------------

void McpServer::registerCpuTools() {
    // debug_get_cpu_state
    {
        auto tool = mcp::tool_builder("debug_get_cpu_state")
            .with_description("Get the current CPU register state.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.getCpuState();
            if (!r.success) return errorContent("get_cpu_state_failed", r.error_message);
            return textContent(mcp_json::cpuStateToJson(r.value));
        });
    }

    // debug_get_registers (formatted view of getCpuState)
    {
        auto tool = mcp::tool_builder("debug_get_registers")
            .with_description("Get all CPU registers as a formatted summary.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.getCpuState();
            if (!r.success) return errorContent("get_registers_failed", r.error_message);
            mcp::json regSummary = {
                {"PC",  mcp_json::hex16(r.value.pc)},
                {"SP",  mcp_json::hex16(r.value.sp)},
                {"A",   mcp_json::hex8(r.value.a)},
                {"F",   mcp_json::hex8(r.value.flags)},
                {"B",   mcp_json::hex8(r.value.b)},
                {"C",   mcp_json::hex8(r.value.c)},
                {"D",   mcp_json::hex8(r.value.d)},
                {"E",   mcp_json::hex8(r.value.e)},
                {"H",   mcp_json::hex8(r.value.h)},
                {"L",   mcp_json::hex8(r.value.l)},
                {"AF",  mcp_json::hex16((static_cast<uint16_t>(r.value.a) << 8) | r.value.flags)},
                {"BC",  mcp_json::hex16((static_cast<uint16_t>(r.value.b) << 8) | r.value.c)},
                {"DE",  mcp_json::hex16((static_cast<uint16_t>(r.value.d) << 8) | r.value.e)},
                {"HL",  mcp_json::hex16((static_cast<uint16_t>(r.value.h) << 8) | r.value.l)}
            };
            return textContent(regSummary);
        });
    }

    // debug_set_register
    {
        auto tool = mcp::tool_builder("debug_set_register")
            .with_description("Set a CPU register value by name.")
            .with_string_param("name", "Register name (A, B, C, D, E, H, L, AF, BC, DE, HL, SP, PC)")
            .with_number_param("value", "16-bit value (0..65535)")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            if (!params.contains("name")) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: name");
            }
            std::string name = params["name"].get<std::string>();
            int val = params["value"].get<int>();
            if (val < 0 || val > 0xFFFF) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "value out of range 0..65535");
            }
            auto r = api_.setRegister(name, static_cast<uint16_t>(val));
            if (!r.success) return errorContent("set_register_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }
}

// ---------------------------------------------------------------------------
// Memory tools (2)
// ---------------------------------------------------------------------------

void McpServer::registerMemoryTools() {
    // debug_read_memory
    {
        auto tool = mcp::tool_builder("debug_read_memory")
            .with_description("Read bytes from Vector-06C memory.")
            .with_number_param("address", "Start address (0..65535)")
            .with_number_param("size", "Number of bytes to read (>= 1)")
            .build();
        // Stage 6.4.1: Add numeric constraints to schema
        addNumericConstraint(tool, "address", 0, 65535);
        addNumericConstraint(tool, "size", 1, std::nullopt);
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            size_t size = getCount(params, "size", 1);
            if (size == 0) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "size must be >= 1");
            }
            auto r = api_.readMemory(addr, size);
            if (!r.success) return errorContent("read_memory_failed", r.error_message);
            mcp::json bytesArr = mcp::json::array();
            for (auto b : r.value) bytesArr.push_back(b);
            return textContent({
                {"address", mcp_json::hex16(addr)},
                {"size",    static_cast<int>(r.value.size())},
                {"bytes",   bytesArr}
            });
        });
    }

    // debug_write_memory
    {
        auto tool = mcp::tool_builder("debug_write_memory")
            .with_description("Write bytes to Vector-06C memory.")
            .with_number_param("address", "Start address (0..65535)")
            .with_array_param("data", "Array of byte values (0..255)", "integer")
            .build();
        // Stage 6.4.1: Add numeric constraints to schema
        addNumericConstraint(tool, "address", 0, 65535);
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            if (!params.contains("data")) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: data");
            }
            std::vector<uint8_t> data;
            for (auto &v : params["data"]) {
                int byte = v.get<int>();
                if (byte < 0 || byte > 0xFF) {
                    throw mcp::mcp_exception(mcp::error_code::invalid_params, "byte value out of range 0..255");
                }
                data.push_back(static_cast<uint8_t>(byte));
            }
            auto r = api_.writeMemory(addr, data);
            if (!r.success) return errorContent("write_memory_failed", r.error_message);
            return textContent({
                {"address", mcp_json::hex16(addr)},
                {"written", static_cast<int>(data.size())}
            });
        });
    }
}

// ---------------------------------------------------------------------------
// I/O tools (2)
// ---------------------------------------------------------------------------

void McpServer::registerIoTools() {
    // debug_read_io
    {
        auto tool = mcp::tool_builder("debug_read_io")
            .with_description("Read a value from an I/O port.")
            .with_number_param("port", "I/O port number (0..255)")
            .build();
        // Stage 6.4.1: Add numeric constraints to schema
        addNumericConstraint(tool, "port", 0, 255);
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint8_t port = getUint8(params, "port");
            auto r = api_.readIo(port);
            if (!r.success) return errorContent("read_io_failed", r.error_message);
            return textContent({
                {"port",  mcp_json::hex8(port)},
                {"value", mcp_json::hex8(r.value)}
            });
        });
    }

    // debug_write_io
    {
        auto tool = mcp::tool_builder("debug_write_io")
            .with_description("Write a value to an I/O port.")
            .with_number_param("port", "I/O port number (0..255)")
            .with_number_param("value", "Byte value to write (0..255)")
            .build();
        // Stage 6.4.1: Add numeric constraints to schema
        addNumericConstraint(tool, "port", 0, 255);
        addNumericConstraint(tool, "value", 0, 255);
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint8_t port = getUint8(params, "port");
            uint8_t value = getUint8(params, "value");
            auto r = api_.writeIo(port, value);
            if (!r.success) return errorContent("write_io_failed", r.error_message);
            return textContent({
                {"port",  mcp_json::hex8(port)},
                {"value", mcp_json::hex8(value)}
            });
        });
    }
}

// ---------------------------------------------------------------------------
// Breakpoint tools (5)
// ---------------------------------------------------------------------------

void McpServer::registerBreakpointTools() {
    // debug_set_breakpoint
    {
        auto tool = mcp::tool_builder("debug_set_breakpoint")
            .with_description("Set a breakpoint at an address.")
            .with_number_param("address", "Breakpoint address (0..65535)")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            auto r = api_.setBreakpoint(addr);
            if (!r.success) return errorContent("set_breakpoint_failed", r.error_message);
            return textContent({{"breakpoint", mcp_json::hex16(addr)}});
        });
    }

    // debug_remove_breakpoint
    {
        auto tool = mcp::tool_builder("debug_remove_breakpoint")
            .with_description("Remove a breakpoint from an address.")
            .with_number_param("address", "Breakpoint address (0..65535)")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            auto r = api_.clearBreakpoint(addr);
            if (!r.success) return errorContent("remove_breakpoint_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_list_breakpoints
    {
        auto tool = mcp::tool_builder("debug_list_breakpoints")
            .with_description("List all breakpoints.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.listBreakpoints();
            if (!r.success) return errorContent("list_breakpoints_failed", r.error_message);
            return textContent({
                {"count",      static_cast<int>(r.value.size())},
                {"breakpoints", mcp_json::breakpointsToJson(r.value)}
            });
        });
    }

    // debug_clear_breakpoints
    {
        auto tool = mcp::tool_builder("debug_clear_breakpoints")
            .with_description("Remove all breakpoints.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.clearAllBreakpoints();
            if (!r.success) return errorContent("clear_breakpoints_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_set_breakpoint_enabled
    {
        auto tool = mcp::tool_builder("debug_set_breakpoint_enabled")
            .with_description("Enable or disable a breakpoint.")
            .with_number_param("address", "Breakpoint address (0..65535)")
            .with_boolean_param("enabled", "true to enable, false to disable")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            if (!params.contains("enabled")) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: enabled");
            }
            bool enabled = params["enabled"].get<bool>();
            auto r = api_.setBreakpointEnabled(addr, enabled);
            if (!r.success) return errorContent("set_breakpoint_enabled_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }
}

// ---------------------------------------------------------------------------
// Disassembly / Trace tools (3)
// ---------------------------------------------------------------------------

void McpServer::registerDisassemblyTools() {
    // debug_disassemble
    {
        auto tool = mcp::tool_builder("debug_disassemble")
            .with_description("Disassemble instructions starting at an address.")
            .with_number_param("address", "Start address (0..65535)")
            .with_number_param("count", "Number of instructions (default 10)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            size_t count = getCount(params, "count", 10);
            auto r = api_.disassemble(addr, count);
            if (!r.success) return errorContent("disassemble_failed", r.error_message);
            return textContent({
                {"address",      mcp_json::hex16(addr)},
                {"count",        static_cast<int>(r.value.size())},
                {"instructions", mcp_json::disassembledInstructionsToJson(r.value)}
            });
        });
    }

    // debug_get_instruction_history
    {
        auto tool = mcp::tool_builder("debug_get_instruction_history")
            .with_description("Get the last N executed instructions.")
            .with_number_param("count", "Number of entries (default 100)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            size_t count = getCount(params, "count", 100);
            auto r = api_.getInstructionHistory(count);
            if (!r.success) return errorContent("get_instruction_history_failed", r.error_message);
            return textContent({
                {"count",   static_cast<int>(r.value.size())},
                {"entries", mcp_json::historyEntriesToJson(r.value)}
            });
        });
    }

    // debug_get_execution_trace
    {
        auto tool = mcp::tool_builder("debug_get_execution_trace")
            .with_description("Get execution trace events.")
            .with_number_param("max_entries", "Maximum entries (default 1000)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            size_t maxEntries = getCount(params, "max_entries", 1000);
            auto r = api_.getExecutionTrace(maxEntries);
            if (!r.success) return errorContent("get_execution_trace_failed", r.error_message);
            return textContent({
                {"count",  static_cast<int>(r.value.size())},
                {"events", mcp_json::instructionEventsToJson(r.value)}
            });
        });
    }
}

// ---------------------------------------------------------------------------
// Stack tools (1)
// ---------------------------------------------------------------------------

void McpServer::registerStackTools() {
    // debug_get_stack
    {
        auto tool = mcp::tool_builder("debug_get_stack")
            .with_description("Get the current stack contents.")
            .with_number_param("limit", "Maximum entries (default 32)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            size_t limit = getCount(params, "limit", 32);
            auto r = api_.getStack(limit);
            if (!r.success) return errorContent("get_stack_failed", r.error_message);
            return textContent({
                {"count",   static_cast<int>(r.value.size())},
                {"entries", mcp_json::stackEntriesToJson(r.value)}
            });
        });
    }
}

// ---------------------------------------------------------------------------
// Symbol / Analysis tools (5)
// ---------------------------------------------------------------------------

void McpServer::registerSymbolTools() {
    // debug_get_symbols
    {
        auto tool = mcp::tool_builder("debug_get_symbols")
            .with_description("List known symbols (functions and labels).")
            .with_number_param("limit", "Maximum symbols (0 = all)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            size_t limit = getCount(params, "limit", 0);
            auto r = api_.getSymbols(limit);
            if (!r.success) return errorContent("get_symbols_failed", r.error_message);
            return textContent({
                {"count",   static_cast<int>(r.value.size())},
                {"symbols", mcp_json::symbolInfosToJson(r.value)}
            });
        });
    }

    // debug_get_function
    {
        auto tool = mcp::tool_builder("debug_get_function")
            .with_description("Get symbol information for a function at an address.")
            .with_number_param("address", "Function start address (0..65535)")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            auto r = api_.getFunction(addr);
            if (!r.success) return errorContent("get_function_failed", r.error_message);
            return textContent(mcp_json::symbolInfoToJson(r.value));
        });
    }

    // debug_get_function_context
    {
        auto tool = mcp::tool_builder("debug_get_function_context")
            .with_description("Get full context for a function (disassembly, xrefs, trace data).")
            .with_number_param("address", "Function start address (0..65535)")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            auto r = api_.getFunctionContext(addr);
            if (!r.success) return errorContent("get_function_context_failed", r.error_message);
            return textContent(mcp_json::functionContextToJson(r.value));
        });
    }

    // debug_get_xrefs
    {
        auto tool = mcp::tool_builder("debug_get_xrefs")
            .with_description("Get cross-references to an address.")
            .with_number_param("address", "Target address (0..65535)")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            auto r = api_.getXrefs(addr);
            if (!r.success) return errorContent("get_xrefs_failed", r.error_message);
            return textContent({
                {"count", static_cast<int>(r.value.size())},
                {"xrefs", mcp_json::xrefResultsToJson(r.value)}
            });
        });
    }

    // debug_get_call_graph
    {
        auto tool = mcp::tool_builder("debug_get_call_graph")
            .with_description("Get the call graph edges.")
            .with_number_param("address", "Optional: filter by function address", false)
            .with_number_param("limit", "Maximum edges (0 = all)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            std::optional<uint16_t> addr;
            if (params.contains("address")) {
                addr = getAddress(params);
            }
            size_t limit = getCount(params, "limit", 0);
            auto r = api_.getCallGraph(addr, limit);
            if (!r.success) return errorContent("get_call_graph_failed", r.error_message);
            return textContent({
                {"count", static_cast<int>(r.value.size())},
                {"edges", mcp_json::callGraphEdgesToJson(r.value)}
            });
        });
    }
}

// ---------------------------------------------------------------------------
// Memory Map / Video tools (3)
// ---------------------------------------------------------------------------

void McpServer::registerMemoryMapTools() {
    // debug_get_memory_map
    {
        auto tool = mcp::tool_builder("debug_get_memory_map")
            .with_description("Get the 256-block memory map.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.getMemoryMap();
            if (!r.success) return errorContent("get_memory_map_failed", r.error_message);
            return textContent({
                {"count",  static_cast<int>(r.value.size())},
                {"blocks", mcp_json::memoryMapBlocksToJson(r.value)}
            });
        });
    }

    // debug_get_vram_info
    {
        auto tool = mcp::tool_builder("debug_get_vram_info")
            .with_description("Get VRAM layout information.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.getVramInfo();
            if (!r.success) return errorContent("get_vram_info_failed", r.error_message);
            return textContent(mcp_json::vramInfoToJson(r.value));
        });
    }

    // debug_get_screen_info
    {
        auto tool = mcp::tool_builder("debug_get_screen_info")
            .with_description("Get current screen mode parameters.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.getScreenInfo();
            if (!r.success) return errorContent("get_screen_info_failed", r.error_message);
            return textContent(mcp_json::screenInfoToJson(r.value));
        });
    }
}

// ---------------------------------------------------------------------------
// I/O Trace tools (1)
// ---------------------------------------------------------------------------

void McpServer::registerIoTraceTools() {
    // debug_get_io_trace
    {
        auto tool = mcp::tool_builder("debug_get_io_trace")
            .with_description("Get I/O port access trace events.")
            .with_number_param("max_entries", "Maximum entries (default 1000)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            size_t maxEntries = getCount(params, "max_entries", 1000);
            auto r = api_.getIoTrace(maxEntries);
            if (!r.success) return errorContent("get_io_trace_failed", r.error_message);
            return textContent({
                {"count",  static_cast<int>(r.value.size())},
                {"events", mcp_json::ioAccessEventsToJson(r.value)}
            });
        });
    }
}

// ---------------------------------------------------------------------------
// Debug State tools (1)
// ---------------------------------------------------------------------------

void McpServer::registerDebugStateTools() {
    // debug_get_state
    {
        auto tool = mcp::tool_builder("debug_get_state")
            .with_description("Get full debugger state snapshot.")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            auto r = api_.getDebugState();
            if (!r.success) return errorContent("get_state_failed", r.error_message);
            return textContent(mcp_json::debugStateToJson(r.value));
        });
    }
}

// ---------------------------------------------------------------------------
// ROM tools (1)
// ---------------------------------------------------------------------------

void McpServer::registerRomTools() {
    // debug_load_rom
    {
        auto tool = mcp::tool_builder("debug_load_rom")
            .with_description("Load a ROM file into memory.")
            .with_string_param("path", "Path to the ROM file")
            .with_number_param("org", "Origin address (default 0)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            if (!params.contains("path")) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: path");
            }
            std::string path = params["path"].get<std::string>();
            uint32_t org = 0;
            if (params.contains("org")) {
                int orgVal = params["org"].get<int>();
                if (orgVal < 0 || orgVal > 0xFFFF) {
                    throw mcp::mcp_exception(mcp::error_code::invalid_params, "org out of range 0..65535");
                }
                org = static_cast<uint32_t>(orgVal);
            }
            auto r = api_.loadRom(path, org);
            if (!r.success) return errorContent("load_rom_failed", r.error_message);
            return textContent(mcp_json::loadRomResultToJson(r.value));
        });
    }
}

// ---------------------------------------------------------------------------
// Annotation tools (6)
// ---------------------------------------------------------------------------

void McpServer::registerAnnotationTools() {
    // debug_set_comment
    {
        auto tool = mcp::tool_builder("debug_set_comment")
            .with_description("Set a comment at an address.")
            .with_number_param("address", "Address (0..65535)")
            .with_string_param("comment", "Comment text")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            if (!params.contains("comment")) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: comment");
            }
            std::string comment = params["comment"].get<std::string>();
            auto r = api_.setComment(addr, comment);
            if (!r.success) return errorContent("set_comment_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_set_function_comment
    {
        auto tool = mcp::tool_builder("debug_set_function_comment")
            .with_description("Set a comment for a function.")
            .with_number_param("address", "Function address (0..65535)")
            .with_string_param("comment", "Comment text")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            if (!params.contains("comment")) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: comment");
            }
            std::string comment = params["comment"].get<std::string>();
            auto r = api_.setFunctionComment(addr, comment);
            if (!r.success) return errorContent("set_function_comment_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_rename_function
    {
        auto tool = mcp::tool_builder("debug_rename_function")
            .with_description("Rename a function.")
            .with_number_param("address", "Function address (0..65535)")
            .with_string_param("name", "New function name")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            if (!params.contains("name")) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: name");
            }
            std::string name = params["name"].get<std::string>();
            auto r = api_.renameFunction(addr, name);
            if (!r.success) return errorContent("rename_function_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_create_function
    {
        auto tool = mcp::tool_builder("debug_create_function")
            .with_description("Create a new function at an address.")
            .with_number_param("address", "Function start address (0..65535)")
            .with_number_param("size", "Function size in bytes (0 = auto)", false)
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            size_t size = getCount(params, "size", 0);
            auto r = api_.createFunction(addr, static_cast<uint16_t>(size));
            if (!r.success) return errorContent("create_function_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_delete_function
    {
        auto tool = mcp::tool_builder("debug_delete_function")
            .with_description("Delete a function at an address.")
            .with_number_param("address", "Function address (0..65535)")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            auto r = api_.deleteFunction(addr);
            if (!r.success) return errorContent("delete_function_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }

    // debug_add_label
    {
        auto tool = mcp::tool_builder("debug_add_label")
            .with_description("Add a label at an address.")
            .with_number_param("address", "Label address (0..65535)")
            .with_string_param("name", "Label name")
            .build();
        registerTool(tool, [this](const mcp::json &params, const std::string &) -> mcp::json {
            uint16_t addr = getAddress(params);
            if (!params.contains("name")) {
                throw mcp::mcp_exception(mcp::error_code::invalid_params, "missing required parameter: name");
            }
            std::string name = params["name"].get<std::string>();
            auto r = api_.addLabel(addr, name);
            if (!r.success) return errorContent("add_label_failed", r.error_message);
            return textContent(mcp_json::successVoidResult());
        });
    }
}
