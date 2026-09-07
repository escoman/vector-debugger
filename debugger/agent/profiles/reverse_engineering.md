# Reverse Engineering Profile

## ID

reverse_engineering

## Description

Профиль для реверс-инжиниринга ROM Vector-06C: построение карты символов, анализ видеоподсистемы и I/O для понимания структуры и поведения программы.

## Tasks

1. **generate_map** — построение и уточнение карты функций/меток
2. **analyze_vram** — анализ видеоданных и паттернов отрисовки
3. **analyze_io** — анализ ввода-вывода и взаимодействия с оборудованием

## Knowledge

- vector06c/architecture
- vector06c/cpu
- vector06c/memory
- vector06c/video
- vector06c/io
- vector06c/keyboard
- vector06c/sound
- vector06c/rom_format

## Recommended Order

1. `generate_map` — основа для всего последующего анализа
2. `analyze_vram` — понимание графической части
3. `analyze_io` — понимание взаимодействия с оборудованием

## Usage

AI Agent начинает с `generate_map` для построения структуры ROM. Затем `analyze_vram` и `analyze_io` помогают понять, как программа взаимодействует с видеоподсистемой и периферией.

### Итоговый отчёт

```markdown
# Reverse Engineering Report

## ROM Structure
- Entry point: 0x????
- Functions: ???
- Data regions: ???

## Key Subsystems
| Subsystem | Address Range | Description |
|-----------|--------------|-------------|
| Video | ? | ... |
| Keyboard | ? | ... |
| Sound | ? | ... |
| I/O | ? | ... |

## Call Graph Highlights
- ...

## Annotated Disassembly
- ...
```
