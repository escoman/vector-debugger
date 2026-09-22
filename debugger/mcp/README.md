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

## MCP Tools (70 tools)

All tools have the `debug_` prefix. Each is a thin wrapper over an AgentApi method.

> The authoritative tool list is what the server returns from `tools/list` — clients must
> not hardcode the count. Machine-readable capability marker: `v06c.api_version` in the
> `initialize` result (2 = Stage 6.26 batch analysis tools). The grouped tables below were
> written for Stage 6.4 and do not list every tool added in Stages 6.11–6.25; the
> Stage 6.26 batch tools are documented at the end of this section.

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

### Batch Analysis — Stage 6.26 (6)
| Tool | Description |
|---|---|
| `debug_disassemble_image` | Batch linear disassembly of a large ROM-image range (start + length ≤ 64K); result equals N × `debug_disassemble_range`; no code/data classification |
| `debug_coverage_report` | Aggregated coverage: code ranges, uncovered ranges, branch targets, branch targets without analyzed code; does NOT modify RDB |
| `debug_diff_memory` | Complete byte-for-byte snapshot diff with old/new values per contiguous range; limit overflow is a `limit_exceeded` error, never a silent cut |
| `debug_find_bytecode_sequence` | Byte pattern search (optional per-byte mask) in a range; returns addresses only, no interpretation |
| `debug_find_immediate_in_range` | Find instructions whose numeric operand equals a value (interpretation by the Debugger disassembler) |
| `debug_get_vram_bytes` | Raw VRAM bytes from the memory-mapped region 0x8000–0xFFFF via the existing target API |

Limits (§16): `MAX_DISASSEMBLE_IMAGE_INSTRUCTIONS=40000`, `MAX_SEARCH_MATCHES=1000`
(hard cap 10000), `MAX_BYTECODE_PATTERN_LENGTH=32`, `MAX_DIFF_CHANGED_RANGES=4096`,
`MAX_DIFF_CHANGED_BYTES=32768`, `MAX_VRAM_READ_RANGE=32768` — see `AgentLimits`.

## Architecture

```
debugger/mcp/
    mcp_adapter.h/cpp    — McpServer class (tool registration, callTool)
    mcp_json.h/cpp       — JSON serialization for Agent API types
    mcp_main.cpp         — v06c-mcp entry point (headless with real Board)
    tests/
        test_mcp_protocol.cpp — 60 tests (Stage 6.4.1 … 6.26)
    README.md

debugger/thirdparty/cpp-mcp/ — cpp-mcp library (MIT, hkr04/cpp-mcp)
```

### Dependencies

```
v06c-mcp
    → debugger_mcp (MCP adapter)
    → debugger_agent (AgentApi)
    → debugger_adapter (DebugAdapter + Board)
    → debugger_core (types, DebugBackend)
    → cpp-mcp (MCP protocol)
    → SDL2 (for headless Board)
    → pthread
```

**Stage 6.4.1**: Uses real `DebugAdapter`/`Board` instead of `NoBoardTarget`. Headless mode achieved via `Options.novideo=true` and `Options.nosound=true` — no SDL window or audio device is created, but full Vector-06C hardware emulation is available.

**Does NOT depend on**: OpenGL, ImGui.

## Limitations

- **stdio transport only** — no HTTP/SSE/WebSocket
- **No MCP Resources** — not implemented in this stage
- **No MCP Prompts** — not implemented in this stage
- **Single client** — one MCP server per debugger session
- **Headless mode** — no video/sound (but real Board emulation)

## MCP Protocol Compliance (Stage 6.4.1)

### CallToolResult Format

All tool responses follow the MCP `CallToolResult` format:

```json
{
  "content": [{"type": "text", "text": "..."}],
  "isError": true/false
}
```

`isError` is at the **top level** of the result object, not inside content items.

### Numeric Constraints

Tool schemas include numeric constraints where applicable:
- `address`: `minimum: 0, maximum: 65535` (uint16_t)
- `port`: `minimum: 0, maximum: 255` (uint8_t)
- `size`: `minimum: 1` (where applicable)
- `value`: `minimum: 0, maximum: 255` or `65535` (depending on context)

## Tests

```bash
make test_mcp_protocol
./test_mcp_protocol
```

60 tests covering (Stage 6.4.1 … 6.26):
- Tool registration (all 70 tools, unique names — tools/list reflects reality)
- Schema validation (types, required params, numeric constraints)
- Tool execution (via MockAgentBackend)
- Error propagation (AgentApiResult → MCP error)
- **Wire-level format** (CallToolResult with isError at top level)
- End-to-end (MCP → AgentApi → Mock → JSON)
- JSON serialization format
- I/O port boundaries

### Integration (real ROMs, real server)

```bash
python3 debugger/tests/integration/test_stage626_integration.py   # path to v06c-mcp auto-detected in build/
```

Spawns `v06c-mcp` over stdio in a temporary work dir and checks the Stage 6.26 batch
tools against real ROM images (`putup.rom`, `TESTAY.ROM`; override paths via
`V06C_PUTUP_ROM` / `V06C_TESTAY_ROM`). Missing ROMs or server binary → `SKIP`, exit 0.
Includes the §20 equivalence check `debug_disassemble_image == N × debug_disassemble_range`
and the §16 `limit_exceeded` no-silent-cut check.

## Regression

All existing tests must pass:

```bash
./test_agent_api          # 77 tests
./test_agent_commands     # 15 tests
./test_agent_contract     # 49 tests
./test_agent_integration  # 46 tests
./test_mcp_protocol       # 60 tests (Stage 6.4.1 … 6.26)
```

All `test_*` binaries in the build directory must pass.
