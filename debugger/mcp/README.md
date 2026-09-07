# v06c-mcp — MCP Server for Vector-06C Debugger

## What is v06c-mcp?

`v06c-mcp` is a [Model Context Protocol](https://modelcontextprotocol.io/) (MCP) server that provides AI Agent access to the Vector-06C debugger through the existing Agent API.

It is a **thin protocol adapter** — all debugger logic lives in the Agent API layer. MCP only handles JSON-RPC transport and parameter serialization.

```
AI Agent → MCP Client → v06c-mcp → AgentApi → DebugBackend → Vector-06C
```

## Build

```bash
cd debugger/build
cmake .. -DENABLE_AI_AGENT=ON
make v06c-mcp
```

## Run (stdio transport)

```bash
./v06c-mcp
```

The server reads JSON-RPC messages from stdin and writes responses to stdout. Diagnostic messages go to stderr.

### MCP Client Configuration

Example Claude Desktop configuration (`claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "vector-debugger": {
      "command": "/path/to/v06c-mcp"
    }
  }
}
```

## MCP Tools (38 tools)

All tools have the `debug_` prefix. Each is a thin wrapper over an AgentApi method.

### Execution (5)
| Tool | Description |
|---|---|
| `debug_run` | Start or resume CPU execution |
| `debug_pause` | Pause CPU execution |
| `debug_step` | Execute one CPU instruction while paused |
| `debug_reset` | Reset the CPU to initial state |
| `debug_is_running` | Check if the CPU is currently running |

### CPU (3)
| Tool | Description |
|---|---|
| `debug_get_cpu_state` | Get CPU register state |
| `debug_get_registers` | Get formatted register summary (AF, BC, DE, HL) |
| `debug_set_register` | Set a register value by name |

### Memory (2)
| Tool | Description |
|---|---|
| `debug_read_memory` | Read bytes from memory |
| `debug_write_memory` | Write bytes to memory |

### I/O (2)
| Tool | Description |
|---|---|
| `debug_read_io` | Read an I/O port value |
| `debug_write_io` | Write a value to an I/O port |

### Breakpoints (5)
| Tool | Description |
|---|---|
| `debug_set_breakpoint` | Set a breakpoint |
| `debug_remove_breakpoint` | Remove a breakpoint |
| `debug_list_breakpoints` | List all breakpoints |
| `debug_clear_breakpoints` | Remove all breakpoints |
| `debug_set_breakpoint_enabled` | Enable/disable a breakpoint |

### Disassembly / Trace (3)
| Tool | Description |
|---|---|
| `debug_disassemble` | Disassemble instructions at address |
| `debug_get_instruction_history` | Get last N executed instructions |
| `debug_get_execution_trace` | Get execution trace events |

### Stack (1)
| Tool | Description |
|---|---|
| `debug_get_stack` | Get current stack contents |

### Symbols / Analysis (5)
| Tool | Description |
|---|---|
| `debug_get_symbols` | List known symbols |
| `debug_get_function` | Get function info at address |
| `debug_get_function_context` | Full function context (disasm, xrefs, trace) |
| `debug_get_xrefs` | Get cross-references to address |
| `debug_get_call_graph` | Get call graph edges |

### Memory Map / Video (3)
| Tool | Description |
|---|---|
| `debug_get_memory_map` | Get 256-block memory map |
| `debug_get_vram_info` | Get VRAM layout info |
| `debug_get_screen_info` | Get screen mode parameters |

### I/O Trace (1)
| Tool | Description |
|---|---|
| `debug_get_io_trace` | Get I/O port access trace |

### Debug State (1)
| Tool | Description |
|---|---|
| `debug_get_state` | Full debugger state snapshot |

### ROM (1)
| Tool | Description |
|---|---|
| `debug_load_rom` | Load a ROM file |

### Annotations (6)
| Tool | Description |
|---|---|
| `debug_set_comment` | Set comment at address |
| `debug_set_function_comment` | Set function comment |
| `debug_rename_function` | Rename a function |
| `debug_create_function` | Create a function |
| `debug_delete_function` | Delete a function |
| `debug_add_label` | Add a label at address |

## Architecture

```
debugger/mcp/
    mcp_adapter.h/cpp    — McpServer class (tool registration, callTool)
    mcp_json.h/cpp       — JSON serialization for Agent API types
    mcp_main.cpp         — v06c-mcp entry point (headless, stdio)
    tests/
        test_mcp_protocol.cpp — 37 tests
    README.md

debugger/thirdparty/cpp-mcp/ — cpp-mcp library (MIT, hkr04/cpp-mcp)
```

### Dependencies

```
v06c-mcp
    → debugger_mcp (MCP adapter)
    → debugger_agent (AgentApi)
    → debugger_core (types, DebugBackend)
    → cpp-mcp (MCP protocol)
    → pthread
```

**Does NOT depend on**: SDL, OpenGL, ImGui, Board sources.

## Limitations

- **stdio transport only** — no HTTP/SSE/WebSocket
- **No MCP Resources** — not implemented in this stage
- **No MCP Prompts** — not implemented in this stage
- **Single client** — one MCP server per debugger session
- **Headless mode** — uses NoBoardTarget (no video/sound)

## Tests

```bash
make test_mcp_protocol
./test_mcp_protocol
```

37 tests covering:
- Tool registration (all 38 tools)
- Schema validation (types, required params)
- Tool execution (via MockAgentBackend)
- Error propagation (AgentApiResult → MCP error)
- End-to-end (MCP → AgentApi → Mock → JSON)
- JSON serialization format
- I/O port boundaries

## Regression

All existing tests must pass:

```bash
./test_agent_api          # 77 tests
./test_agent_commands     # 15 tests
./test_agent_contract     # 49 tests
./test_agent_integration  # 46 tests
./test_mcp_protocol       # 37 tests
```
