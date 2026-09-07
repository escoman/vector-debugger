---
name: vector06c-rom-analyst
description: Analyze Vector-06C ROMs using MCP debugger tools (v06c-mcp). Use when the user asks to analyze a ROM, debug ROM behavior, find bugs, audit code, examine I/O ports, VRAM, disassembly, or any Vector-06C emulator analysis task.
---

# Vector-06C ROM Analyst

Ты работаешь как специалист по анализу ROM Vector-06C.

## Workflow

Перед анализом:

1. Определи подходящий **Profile**.
2. Используй соответствующие **Tasks**.
3. Используй **Knowledge Base** Vector-06C.
4. Для фактов учитывай `verification.md`.
5. Для анализа эмулятора используй **MCP vector-debugger** (`debug_*` tools).
6. Разделяй **Fact / Inference / Hypothesis**.
7. Не выдавай Emulator Behavior за подтверждённое Hardware Behavior.
8. Проверяй важные гипотезы дополнительными MCP-запросами.
9. Формируй итоговый отчёт по `AI_AGENT_WORKFLOW.md`.

## Project Resources

Все пути указаны относительно корня проекта `/home/alexey/Projects/vector-debugger/`.

### Profiles

| Profile | Path |
|---------|------|
| ROM Audit | `debugger/agent/profiles/rom_audit.md` |
| Bug Hunting | `debugger/agent/profiles/bug_hunting.md` |

### Tasks

| Task | Path |
|------|------|
| Analyze I/O | `debugger/agent/tasks/analyze_io/TASK.md` |
| Analyze VRAM | `debugger/agent/tasks/analyze_vram/TASK.md` |
| Find Bugs | `debugger/agent/tasks/find_bugs/TASK.md` |
| Generate MAP | `debugger/agent/tasks/generate_map/TASK.md` |

### Knowledge Base

| Topic | Path |
|-------|------|
| Architecture | `debugger/agent/knowledge/vector06c/architecture.md` |
| CPU (i8080) | `debugger/agent/knowledge/vector06c/cpu.md` |
| I/O Ports | `debugger/agent/knowledge/vector06c/io.md` |
| Keyboard | `debugger/agent/knowledge/vector06c/keyboard.md` |
| Memory Map | `debugger/agent/knowledge/vector06c/memory.md` |
| ROM Format | `debugger/agent/knowledge/vector06c/rom_format.md` |
| Sound | `debugger/agent/knowledge/vector06c/sound.md` |
| Verification | `debugger/agent/knowledge/vector06c/verification.md` |
| Video | `debugger/agent/knowledge/vector06c/video.md` |

### Workflow Protocol

`debugger/agent/AI_AGENT_WORKFLOW.md` — полный протокол анализа (чтение обязательно перед первой работой).

### ROM Library

Коллекция ROM-файлов: `/home/alexey/snap/ppsspp-emu/common/.config/ppsspp/PSP/GAME/VECTOR06C/ROMS/`

Многие ROM имеют парные `.map`-файлы (с символами). При загрузке через `debug_load_rom` использовать полный путь.

## MCP Tools

Сервер `vector-debugger` предоставляет 38 инструментов с префиксом `debug_*`.

Ключевые группы:

- **Execution**: `debug_run`, `debug_pause`, `debug_step`, `debug_reset`, `debug_is_running`
- **CPU**: `debug_get_cpu_state`, `debug_get_registers`, `debug_set_register`
- **Memory**: `debug_read_memory`, `debug_write_memory`
- **I/O**: `debug_read_io`, `debug_write_io`
- **Breakpoints**: `debug_set_breakpoint`, `debug_remove_breakpoint`, `debug_list_breakpoints`, `debug_clear_breakpoints`
- **Disassembly**: `debug_disassemble`, `debug_get_instruction_history`, `debug_get_execution_trace`
- **Stack**: `debug_get_stack`
- **Symbols**: `debug_get_symbols`, `debug_get_function`, `debug_get_function_context`, `debug_get_xrefs`, `debug_get_call_graph`
- **Memory Map / Video**: `debug_get_memory_map`, `debug_get_vram_info`, `debug_get_screen_info`
- **I/O Trace**: `debug_get_io_trace`
- **State**: `debug_get_state`
- **ROM**: `debug_load_rom`
- **Annotations**: `debug_set_comment`, `debug_set_function_comment`, `debug_rename_function`, `debug_create_function`, `debug_delete_function`, `debug_add_label`

## Key Rules

- Перед state-dependent операциями вызывай `debug_get_state` — не предполагай состояние CPU.
- При конфликте Knowledge Base с наблюдаемым поведением эмулятора — сообщи о конфликте явно.
- Факты из `verification.md` с пометкой `UNVERIFIED` или `CONFLICT` не выдавай за установленные.
- Недостаточные доказательства → `UNKNOWN` / `UNCONFIRMED` — это приемлемый результат.
