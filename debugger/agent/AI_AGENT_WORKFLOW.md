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
2.  Select Profile
3.  Load Profile
4.  Determine Tasks
5.  Load required Knowledge
6.  Form Analysis Plan
7.  Check Debugger state
8.  Load ROM if needed
9.  Execute MCP operations
10. Analyze results
11. Form hypotheses
12. Verify hypotheses with additional MCP operations
13. Evaluate evidence
14. Form result
15. Note limitations and unknowns
```

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
3. Obtain CPU state, memory map, symbols as needed

Do **not** reload ROM if it is already loaded and the task does not require replacement.

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
