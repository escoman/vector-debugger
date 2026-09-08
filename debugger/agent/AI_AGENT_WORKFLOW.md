# AI Agent Workflow Protocol

## Overview

This document defines the standard workflow for an external AI Agent analyzing
Vector-06C ROMs via the Vector Debugger infrastructure.

The AI Agent is **external** to `vector-debugger`. The debugger provides:

- **MCP** — stateless protocol layer exposing Debugger operations
- **Agent API** — structured data access (limits enforced by API)
- **Knowledge Base** — technical reference about Vector-06C hardware
- **Task Library** — declarative analysis methodology
- **Profiles** — research type definitions combining Tasks + Knowledge

The debugger does **not** contain LLM runtime, analysis state, or reasoning engine.

---

## MCP-First Analysis Rule

When analyzing Vector-06C ROMs, the **MCP Debugger is the primary analysis tool**.

The AI Agent must **not** independently:

- read ROM as raw bytes and disassemble it on its own;
- decode instructions by opcode;
- manually build control flow instead of using MCP;
- interpret hex dumps as programs on its own;
- replace MCP results with its own disassembly.

Correct data flow:

```
ROM → MCP Debugger → DebugAdapter → Agent API → disassembly / CPU / memory / I/O / trace → Agent analysis
```

Forbidden data flow:

```
ROM → Agent → self-made disassembler
```

### MCP must be connected before analysis

Before analyzing a ROM, ensure the MCP Debugger is available. If the MCP server is not running, start it using the project-provided method. Do not fall back to self-disassembly just because MCP was not yet started.

If MCP cannot be started, report explicitly:

```
MCP Debugger is not available; reliable ROM analysis cannot be performed.
```

### Disassembly via MCP only

Use `debug_disassemble` to obtain instructions. The MCP result is the authoritative source of disassembly. The Agent analyzes address, opcode/instruction, operands, and control flow as returned by the Debugger — not by re-decoding opcodes independently.

### Priority of MCP over own computation

If the Agent can obtain information via MCP, it must use MCP. Self-computation is allowed **only as a check** on already-obtained MCP data — for correlation and reasoning, not as a replacement for the Debugger. If the Agent’s reasoning contradicts an MCP result, the MCP result takes priority.

### Forbidden fallback

Do not use this fallback:

```
MCP unavailable → take hex dump → self-disassemble ROM → analyze
```

Correct behavior:

```
MCP unavailable → report lack of Debugger evidence → do not present speculative ROM analysis as factual
```

> **The AI Agent is not a Vector-06C disassembler. The Agent is an analyst that uses the Debugger as its source of factual data.**

---

## Component Roles

| Component | Responsibility |
|-----------|---------------|
| AI Agent | Understand task, select Profile/Tasks, read Knowledge, plan analysis, call MCP, interpret results, form hypotheses, produce report |
| Profile | Defines which Tasks to use, which Knowledge is needed, what output is expected |
| Task | Defines methodology for a specific operation: what to analyze, what data is needed, which MCP tools to use, how to interpret results |
| Knowledge Base | Technical reference about Vector-06C (hardware, ports, memory, video, sound) |
| MCP | Stateless transport interface to Debugger operations |

Profile does not execute analysis. Task is not a script. MCP does not contain methodology.

---

## Main Workflow

Every analysis follows this sequence:

```
1.  Define the task
2.  Ensure MCP Debugger is available (if not, report and stop)
3.  Select Profile
4.  Load Profile
5.  Determine Tasks
6.  Load required Knowledge
7.  Form Analysis Plan
8.  Check Debugger state
9.  Load ROM if needed (debug_load_rom)
10. Read RDB (debug_get_rdb_info, debug_list_rdb_objects)
11. Obtain disassembly via MCP (debug_disassemble)
12. Execute MCP operations
13. Analyze results
14. Form hypotheses
15. Verify hypotheses with additional MCP operations
16. Evaluate evidence
17. Form result
18. Save findings to RDB (debug_add_rdb_object, debug_set_rdb_comment, debug_save_rdb)
19. Create confirmed RDB links (debug_add_rdb_link)
20. Save RDB if links were added (debug_save_rdb)
21. Note limitations and unknowns
```

Steps 2, 9, 10, and 11 are mandatory. Do not replace them with self-analysis of the ROM binary.

---

## Analysis Plan

Before complex analysis, the Agent should form an internal plan.

Minimum structure:

```
Goal:
  What the analysis aims to determine.

Profile:
  Which profile is active.

Tasks:
  Which tasks will be executed.

Knowledge:
  Which KB documents are needed.

MCP Operations:
  Which MCP tools will be called and why.

Expected Evidence:
  What observations would support or refute the goal.

Validation Steps:
  How to verify findings.

Output:
  What the final deliverable looks like.
```

The Analysis Plan is not part of the API and does not need to be persisted.

---

## Evidence-First Analysis

The Agent must not make technical assertions based on assumption alone.

For each significant conclusion:

```
Observation:    What was directly seen (MCP result, memory content, instruction).
Evidence:       Supporting data (trace, memory map, multiple readings).
Interpretation: What the evidence means in context.
Conclusion:     The derived finding.
```

Example:

```
Observation:    Instruction writes to address C000h.
Evidence:       Execution trace + memory map shows C000h is in screen plane.
Interpretation: C000h belongs to bit plane 2 (8000h/A000h/C000h/E000h layout).
Conclusion:     Instruction modifies VRAM.
```

---

## Fact / Inference / Hypothesis

The Agent must distinguish three levels:

### Fact

Directly confirmed observation.

```
PC = 8123h
```

### Inference

Derived from multiple confirmed facts.

```
8123h lies inside a known code region.
```

### Hypothesis

Requires additional verification.

```
This routine may be responsible for sprite rendering.
```

**Never present a Hypothesis as a Fact.**

---

## Confidence

No complex mathematical system. Three levels:

| Level | Meaning |
|-------|---------|
| **high** | Sufficient independent confirmations exist |
| **medium** | Well-reasoned but has limitations |
| **low** | Based on incomplete data or requires further verification |

---

## Iterative Analysis Loop

```
Current hypothesis
    ↓
Select MCP operation
    ↓
Observe result
    ↓
Update hypothesis
    ↓
    ├── confirmed  → conclusion
    ├── rejected   → new hypothesis
    └── insufficient → additional test
```

The number of iterations should not be artificially limited if MCP API and resources allow continuing.

---

## Debugger State Awareness

Before state-dependent operations, the Agent must determine emulator state via:

```
debug_get_state
```

Do **not** assume:
- ROM is loaded
- CPU is paused
- CPU is running
- Breakpoint is active

After `run` / `pause` / `step` / `reset`, verify actual state. "run() accepted" is not equivalent to "CPU is running".

---

## ROM Handling

If analysis requires a ROM:

1. Call `debug_load_rom`
2. Verify load result
3. Read RDB (`debug_get_rdb_info`) to check existing database
4. Obtain CPU state, memory map, symbols as needed

Do **not** reload ROM if it is already loaded and the task does not require replacement.

---

## ROM Database (RDB)

RDB is the **sole persistent store** for ROM analysis results.

### RDB Workflow

```
read RDB (debug_get_rdb_info)
↓
analyze ROM
↓
add/modify objects via RDB API
↓
verify results
↓
save RDB (debug_save_rdb)
```

### RDB Rules

- All analysis findings (functions, labels, comments) go into RDB via MCP tools.
- Do **not** manually generate RDB JSON.
- Do **not** edit `.rdb` as a text file.
- Do **not** generate `.map` files to store analysis results.
- Save RDB after completing a batch of changes, not after every single operation.
- RDB is loaded automatically when a ROM is opened. If `.rdb` exists, it takes priority over `.map`.

### RDB Links

Links represent confirmed semantic relationships between RDB objects.

```
Analysis → Evidence → Create/update RDB objects → Create confirmed RDB links → Save RDB → Build Call Graph → Review
```

Rules:
- Use `debug_add_rdb_link` / `debug_remove_rdb_link` / `debug_get_rdb_links` for link management.
- Target object may not exist (unresolved link is allowed).
- Do not create links based on address proximity alone.
- Links require evidence from disassembly, trace, or confirmed control flow.
- Call Graph is a visualization of RDB links — it does not create them.
- `.rdb.graph` is visual data; `.rdb` contains semantic links.

Evidence levels for links:

```
Fact:       0100 contains CALL 0120.
Inference:  0100 references routine at 0120.
RDB:        debug_add_rdb_link(source=0x0100, target=0x0120)
```

A hypothesis should not automatically become a link. Verify first via MCP.

### Available RDB MCP Tools

| Tool | Purpose |
|------|---------|
| `debug_get_rdb_info` | RDB metadata: path, platform, dirty state, object count, ROM identity |
| `debug_list_rdb_objects` | List all objects sorted by address (with optional limit) |
| `debug_get_rdb_object` | Get single object by address |
| `debug_find_rdb_object` | Find object by name |
| `debug_add_rdb_object` | Add new object (address, name, type, size) |
| `debug_update_rdb_object` | Update existing object |
| `debug_remove_rdb_object` | Remove object |
| `debug_set_rdb_comment` | Set comment on object |
| `debug_set_rdb_property` | Set property on object |
| `debug_save_rdb` | Save RDB to disk |
| `debug_reload_rdb` | Reload from disk (discard unsaved changes) |
| `debug_add_rdb_link` | Add directed link from source to target |
| `debug_remove_rdb_link` | Remove directed link |
| `debug_get_rdb_links` | Get links from source object |

---

## Knowledge Base Usage

Knowledge Base is a technical reference, not an algorithm.

When Knowledge conflicts with observed emulator behavior:
- Report the conflict explicitly
- Do not silently choose one side

When `verification.md` contains `CONFLICT`:
- Include the conflict in reasoning
- Do not treat unverified claims as established facts

When a fact is marked `UNVERIFIED`:
- Do not present it as established

---

## Hardware vs Emulator Behavior

If a result confirms only current emulator behavior:

- **Incorrect**: "Original Vector-06C hardware definitely does X"
- **Correct**: "The current emulator implements X"
- **Correct**: "According to verified emulator behavior, X occurs"

If hardware behavior is independently confirmed, state so explicitly.

---

## Hypothesis Verification

After detecting suspicious behavior, do not immediately declare it a bug.

```
Observation
    ↓
Hypothesis
    ↓
Additional evidence
    ↓
Test
    ↓
Confirmed / Rejected / Uncertain
```

---

## Large Data Handling

Use existing Agent API limits:

```
MAX_DISASSEMBLY_COUNT
MAX_SYMBOLS_LIMIT
MAX_CALL_GRAPH_LIMIT
MAX_STACK_LIMIT
MAX_TRACE_ENTRIES
MAX_HISTORY_ENTRIES
```

Prefer small targeted queries over entire datasets.

Localize analysis before deep-diving:

```
ROM → memory map → symbols/functions → interesting function → disassembly → trace → specific instruction
```

Do not start with full 64 KB analysis if the task concerns a specific function.

---

## Working with Symbols

If ROM has `.map`/symbols: use symbols, functions, xrefs, call graph for precision.

Prefer: `function DRAW_FIRE at 8A20h`
Over: `routine at 8A20h`

If no symbols: use disassembly, trace, history, memory/I/O accesses, call graph.

Do not invent function names as established facts. Use temporary labels:

```
subroutine_8123
candidate_renderer
unknown_io_handler
```

---

## Bug Finding Report

Each bug finding must contain:

```
Location:           Address or range
Observed behavior:  What actually happens
Expected behavior:  What should happen (with justification)
Evidence:           MCP results, traces, memory dumps
Reasoning:          Why this is likely a bug
Confidence:         high / medium / low
Next verification:  Suggested follow-up check
```

Do not declare "bug" just because code looks unusual.

---

## Insufficient Evidence

If evidence is insufficient:

```
UNKNOWN
```

or:

```
UNCONFIRMED
```

is an acceptable result. Better to report "insufficient evidence" than to make an unjustified conclusion.

---

## Final Report Structure

```markdown
# Analysis

## Goal

## ROM

## Profile

## Tasks

## Findings

### Finding 1

Location:
Observation:
Evidence:
Conclusion:
Confidence:

### Finding 2
...

## Verified Facts

## Inferences

## Hypotheses

## Unknowns

## Limitations

## Recommended Next Steps
```

Simple tasks may use a shortened format.

---

## Reproducibility

For each significant finding, preserve:

- ROM used
- Address
- Relevant MCP operations
- Important parameters
- Observed result

This allows another Agent to repeat the verification. Not every MCP call needs to be saved.

---

## Self-Correction

If a new observation contradicts a previous conclusion:

- Do not hide the contradiction
- Revise the hypothesis
- Repeat necessary checks

Never silently ignore contradicting MCP results.

---

## Forbidden Assumptions

The Agent must not automatically assume:

- CPU clock
- Undocumented opcode behavior
- Hardware timing
- I/O semantics
- Memory behavior
- Video behavior
- Sound behavior

Unless confirmed by Knowledge Base, MCP observation, or another explicitly cited source.

---

## Minimal Agent Prompt Contract

```
You are analyzing a Vector-06C ROM.

MCP Debugger is your primary analysis tool.
Do not disassemble ROM independently — use debug_disassemble.
Do not decode opcodes on your own — use MCP results.
If MCP is unavailable, report this and do not perform speculative analysis.

Use the provided Profile, Tasks and Knowledge Base.

Use MCP tools to obtain evidence.

Do not invent hardware facts.

Distinguish:
- verified facts
- emulator behavior
- inference
- hypothesis
- unknown

When sources conflict, report the conflict.
If your reasoning contradicts MCP, MCP takes priority.

Do not treat an unverified claim as established fact.

Validate important hypotheses with additional MCP operations.

Produce an evidence-based final report.
```

This is a conceptual contract, not a ready-made system prompt for a specific LLM.

---

## Constraints

The following must **not** be added to the debugger:

- Analysis state in MCP
- Hypothesis engine
- Task runner / workflow engine
- AI memory / reasoning engine
- Agent executable / LLM integration
- RAG / embeddings / vector database

MCP remains a stateless protocol layer.
Task Library remains declarative Markdown.
Profiles remain declarative.
Analysis state lives in the external AI Agent.
