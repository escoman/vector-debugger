// ---------------------------------------------------------------------------
// mcp_main.cpp — Stage 6.4
//
// Entry point for v06c-mcp executable.
// Creates a headless emulator (NoBoardTarget), AgentApi, McpServer,
// registers all 38 tools, and runs stdio transport.
//
// This file provides HAL stubs needed by i8080.cpp (same pattern as
// test_agent_integration.cpp).
// ---------------------------------------------------------------------------

#include "mcp_adapter.h"
#include "agent_api.h"
#include "backend.h"
#include "memory.h"
#include "no_board_target.h"
#include "options.h"

#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// HAL stubs — needed by i8080.cpp (same as test_agent_integration.cpp)
// ---------------------------------------------------------------------------

DebugBackend *g_adapter_backend = nullptr;

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
// main
// ---------------------------------------------------------------------------

int main()
{
    // Headless mode — no video, no sound
    Options.novideo = true;
    Options.nosound = true;

    // Create emulator core
    Memory mem;
    NoBoardTarget target(mem);
    DebugBackend backend(target);
    backend.reset();

    // Set HAL globals
    hal_memory = &mem;
    hal_dbg = &backend;

    // Create Agent API
    AgentApi api(backend);

    // Create MCP server and register all tools
    McpServer mcp(api);
    mcp.registerAllTools();

    // Log to stderr (stdout is used for MCP protocol)
    fprintf(stderr, "v06c-mcp: %zu tools registered\n", mcp.registeredToolNames().size());
    fprintf(stderr, "v06c-mcp: starting stdio transport\n");

    // Run stdio transport (blocks until stdin closes)
    mcp.runStdio();

    // Cleanup
    fprintf(stderr, "v06c-mcp: shutting down\n");
    hal_dbg = nullptr;
    hal_memory = nullptr;

    return 0;
}
