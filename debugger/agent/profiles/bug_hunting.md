# Bug Hunting Profile

## ID

bug_hunting

## Description

Профиль для поиска ошибок в ROM Vector-06C: статический и динамический аудит, проверка стека, анализ VRAM и I/O на предмет аномалий.

## Tasks

1. **find_bugs** — общий аудит (memory, control flow, I/O, VRAM)
2. **stack_safety** — проверка целостности стека
3. **analyze_vram** — анализ видеоподсистемы на ошибки
4. **analyze_io** — анализ I/O на аномалии

## Knowledge

- vector06c/cpu
- vector06c/memory
- vector06c/video
- vector06c/io
- vector06c/rom_format
- vector06c/verification

## Recommended Order

1. `find_bugs` — общий обзор проблем
2. `stack_safety` — детальный анализ стека
3. `analyze_vram` — проверка видео
4. `analyze_io` — проверка I/O

## Usage

AI Agent выполняет каждый Task последовательно, агрегируя findings. Результаты классифицируются по severity (critical / probable / suspicious) и confidence (confirmed / probable / suspicious).

### Итоговый отчёт

```markdown
# Bug Hunting Report

## Summary
- Total findings: ???
- Critical: ???
- Probable: ???
- Suspicious: ???

## Critical Findings
| Address | Category | Evidence | Description |
|---------|----------|----------|-------------|

## Probable Findings
| Address | Category | Evidence | Description |
|---------|----------|----------|-------------|

## Suspicious Patterns
| Address | Category | Reason | Description |
|---------|----------|--------|-------------|

## Recommendations
- ...
```
