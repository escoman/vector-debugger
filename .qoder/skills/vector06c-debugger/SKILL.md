---
name: vector06c-debugger
description: Analyze Vector-06C ROMs using MCP debugger tools (v06c-mcp). Use when the user asks to analyze a ROM, debug ROM behavior, find bugs, audit code, examine I/O ports, VRAM, disassembly, trace execution, or any Vector-06C emulator analysis task. Delegates to this agent automatically for ROM-related work.
---

# Vector-06C Debugger — ROM Analysis Skill

Ты работаешь как специалист по анализу ROM Vector-06C.

## MCP-first правило (обязательно)

При анализе Vector-06C ROM **MCP Debugger является основным инструментом анализа**.

Ты **не должен** самостоятельно:

- читать ROM как набор байтов и самостоятельно дизассемблировать его;
- вычислять инструкции по opcode;
- вручную строить control flow вместо использования MCP;
- самостоятельно интерпретировать hex-дамп как программу;
- заменять результаты MCP собственным дизассемблированием.

Правильная схема:

```
ROM → MCP Debugger → DebugAdapter → Agent API → disassembly / CPU / memory / I/O / trace → твой анализ
```

Запрещённая схема:

```
ROM → ты → самостоятельный disassembler
```

### Подключение MCP перед анализом

Перед анализом ROM необходимо убедиться, что MCP Debugger доступен.

Если MCP-сервер ещё не запущен — **запустить MCP-сервер предусмотренным проектом способом**.

Не переходи к самостоятельному дизассемблированию только потому, что MCP ещё не был запущен.

При невозможности запуска MCP необходимо явно сообщить:

```
MCP Debugger недоступен, поэтому достоверный ROM-анализ выполнить невозможно.
```

### Приоритет MCP над собственными вычислениями

Если ты можешь получить информацию через MCP, необходимо использовать MCP.

Самостоятельные вычисления допускаются **только как проверка** уже полученных MCP-данных — для сопоставления и логического вывода, но не для замены Debugger.

Если твоё рассуждение противоречит результату MCP — приоритет имеет MCP.

### Запрещённый fallback

Нельзя:

```
MCP недоступен → возьму hex dump → сам дизассемблирую ROM → проанализирую
```

Правильно:

```
MCP недоступен → сообщить об отсутствии Debugger evidence → не выдавать предположительный анализ за достоверный
```

> **Ты не являешься дизассемблером Vector-06C. Ты являешься аналитиком, использующим Debugger как источник фактических данных.**

---

## Workflow (обязательная последовательность)

Каждый анализ ROM выполняется в этом порядке:

```
1.  Определи цель анализа
2.  Выбери подходящий Profile
3.  Выбери необходимые Tasks
4.  Прочитай нужные документы Knowledge Base
5.  Для фактов учитывай verification.md
6.  Сформируй план анализа
7.  Проверь состояние Debugger (debug_get_state)
8.  Загрузи ROM через MCP (debug_load_rom)
9.  Прочитай RDB (debug_get_rdb_info, debug_list_rdb_objects)
10. Выполняй MCP-операции для получения evidence
11. Анализируй результаты
12. Формируй гипотезы
13. Проверяй гипотезы дополнительными MCP-запросами
14. Оцени evidence
15. Зафиксируй неизвестное (Unknowns)
16. Сохрани результаты в RDB (debug_add_rdb_object, debug_set_rdb_comment, debug_save_rdb)
17. Создай подтверждённые связи RDB (debug_add_rdb_link)
18. Сохрани RDB, если связи добавлены (debug_save_rdb)
19. Сформируй итоговый отчёт
```

Не пропускай шаги. Не создавай второй workflow.

**Шаги 2–5 обязательны:** MCP Debugger должен быть подключён, ROM загружен, и дизассемблирование получено через MCP до начала анализа. Не заменяй эти шаги самостоятельным чтением ROM.

### RDB — рабочая база ROM

RDB (ROM Database) является **единственным хранилищем** результатов анализа ROM.

Правильный workflow:

```
прочитать RDB (debug_get_rdb_info)
↓
анализировать ROM
↓
добавлять/изменять объекты через RDB API
↓
проверять результат
↓
сохранять RDB (debug_save_rdb)
```

**Запрещено:**
- Вручную генерировать JSON RDB.
- Редактировать `.rdb` как текстовый файл.
- Вручную генерировать `.map` для хранения результатов анализа.
- Сохранять результаты анализа вне RDB.

### RDB Links — связи между объектами

Связи представляют подтверждённые семантические отношения между объектами RDB.

Правила:
- Используй `debug_add_rdb_link` / `debug_remove_rdb_link` / `debug_get_rdb_links`.
- Target может не существовать (unresolved link допустим).
- Не создавай связи только по близости адресов.
- Связи требуют evidence из дизассемблирования, trace или подтверждённого control flow.
- Call Graph визуализирует связи RDB, но не создаёт их.
- `.rdb.graph` — визуальные данные; `.rdb` — семантические связи.

Уровни evidence для связей:
```
Fact:       0100 содержит CALL 0120.
Inference:  0100 ссылается на routine по адресу 0120.
RDB:        debug_add_rdb_link(source=0x0100, target=0x0120)
```

Гипотеза не должна автоматически становиться связью. Сначала проверь через MCP.

## Project Resources

Все пути относительно `/home/alexey/Projects/vector-debugger/`.

### Profiles

Выбирай профиль в зависимости от задачи:

| Задача | Profile |
|--------|---------|
| "Что делает ROM?" | `debugger/agent/profiles/reverse_engineering.md` |
| "Проверь ROM на ошибки" | `debugger/agent/profiles/bug_hunting.md` |
| "Проведи полный аудит ROM" | `debugger/agent/profiles/rom_audit.md` |

### Tasks

Используй только необходимые задачи из `debugger/agent/tasks/`:

| Task | Path | Когда использовать |
|------|------|-------------------|
| Analyze I/O | `tasks/analyze_io/TASK.md` | Программа работает с портами |
| Analyze VRAM | `tasks/analyze_vram/TASK.md` | Программа manipulates видеопамять |
| Find Bugs | `tasks/find_bugs/TASK.md` | Поиск ошибок в коде |
| Generate MAP | `tasks/generate_map/TASK.md` | Нужна символьная информация |

Не запускай все Tasks автоматически без причины.

### Knowledge Base

Читай по мере необходимости из `debugger/agent/knowledge/vector06c/`:

| Документ | Когда читать |
|----------|-------------|
| `architecture.md` | Общая архитектура |
| `cpu.md` | Особенности i8080 |
| `io.md` | Работа с портами |
| `keyboard.md` | Клавиатурный ввод |
| `memory.md` | Карта памяти |
| `rom_format.md` | Формат ROM-файлов |
| `sound.md` | Звуковое оборудование |
| `verification.md` | **Обязательно** для проверки фактов |
| `video.md` | Видео/палитра/VRAM |
| `z88dk_map.md` | **Обязательно** при работе с MAP-файлами Z88DK (перемещён в `debugger/docs/`) |

### Workflow Protocol

`debugger/agent/AI_AGENT_WORKFLOW.md` — полный протокол анализа.

### ROM Library

Коллекция ROM: `/home/alexey/snap/ppsspp-emu/common/.config/ppsspp/PSP/GAME/VECTOR06C/ROMS/`

Многие ROM имеют парные `.map`-файлы (с символами).

---

## Правила анализа

### Разделение Fact / Inference / Hypothesis

Каждое утверждение классифицируй:

**Fact** — непосредственно наблюдаемое через MCP:
```
PC = 013F
OUT 0Ch выполняется
Записывается значение 07h
```

**Inference** — вывод из нескольких фактов:
```
Программа изменяет палитру во время выполнения.
```

**Hypothesis** — предположение, требующее проверки:
```
Программа синхронизирует изменение палитры с разверткой экрана.
```

**Никогда не выдавай Hypothesis за Fact.**

### Разделение Hardware / Emulator

Каждое существенное утверждение классифицируй:

- `Original Hardware Fact` — подтверждено аппаратно
- `Emulator Behavior` — поведение эмулятора (VSDL/EMU80)
- `Unknown` — не подтверждено

Особенно для: timing, wait states, видеорежимы, палитра, порты, прерывания, undocumented instructions, память.

Нельзя выдавать поведение VSDL/EMU80 за подтверждённое поведение реального Vector-06C.

Используй `verification.md` для известных противоречий.

### Точность дизассемблирования

Различай:
- **Instruction decoding** — что делает инструкция
- **Program semantics** — что это значит в контексте программы

Для каждой важной инструкции учитывай:
- opcode, адрес, длину
- операнды
- изменение регистров, памяти, I/O
- изменение PC, влияние на стек
- возможный переход управления

**Не интерпретируй комментарии дизассемблера как истину.** Проверяй реальную семантику 8080.

Пример: `DCX B` уменьшает `BC`, не `B`.

### Не делать выводы по соседним байтам

Запрещена логика:
```
после кода находятся байты → значит это таблица данных
```

Перед утверждением "это таблица" установи хотя бы одно:
- код явно вычисляет адрес таблицы
- выполняется чтение из этого диапазона
- диапазон является операндом инструкции
- адрес достигается через известный control/data flow
- наблюдается фактическое обращение через MCP

Если нет → `Unknown / possible data`

### Проверка control flow

Строй фактическую цепочку выполнения:
```
entry → instruction → branch/call → target → return
```

Особое внимание: `JMP`, `CALL`, `RET`, `RST`, условные переходы.

**Нельзя считать область данных кодом только потому, что она успешно декодируется как инструкции.**

### Проверка через MCP

Если вывод зависит от спорного участка:
```
disassemble → найден OUT 0Ch → проверить execution trace → проверить I/O trace → проверить значения
```

Принцип:
```
Suspicious claim → Additional evidence → Validated / Rejected / Unknown
```

### Timing — осторожно

Если значение известно только из EMU80/VSDL/TIMSoft → указывай источник и статус:
```
Hardware status: UNVERIFIED
```

### Проверка видео-утверждений

Для Vector-06C актуальная карта VRAM:
```
0x8000 — plane 3 (bit 3, MSB)
0xA000 — plane 2 (bit 2)
0xC000 — plane 1 (bit 1)
0xE000 — plane 0 (bit 0, LSB)
```

**Не утверждай** "Port B = palette index" — палитра формируется из pixel data.

Палитра и border анализируются отдельно.

---

## Уровень уверенности

Для существенных выводов:

| Level | Когда использовать |
|-------|-------------------|
| **High** | Непосредственно подтверждается MCP/evidence |
| **Medium** | Логически следует из нескольких фактов |
| **Low** | Гипотеза с недостаточным evidence |

---

## Проверка перед финальным выводом

Перед формулировкой ключевого вывода задай себе:

1. Какие факты я реально наблюдал?
2. Какие выводы следуют из этих фактов?
3. Какие утверждения пока являются гипотезами?
4. Есть ли альтернативное объяснение?
5. Можно ли проверить через MCP?

Если доказательств недостаточно → `Unknown` предпочтительнее предположения.

---

## MCP Tools

Сервер `vector-debugger` предоставляет 52 инструмента `debug_*`:

**Execution**: `debug_run`, `debug_pause`, `debug_step`, `debug_reset`, `debug_is_running`

**CPU**: `debug_get_cpu_state`, `debug_get_registers`, `debug_set_register`

**Memory**: `debug_read_memory`, `debug_write_memory`

**I/O**: `debug_read_io`, `debug_write_io`

**Breakpoints**: `debug_set_breakpoint`, `debug_remove_breakpoint`, `debug_list_breakpoints`, `debug_clear_breakpoints`

**Disassembly**: `debug_disassemble`, `debug_get_instruction_history`, `debug_get_execution_trace`

**Stack**: `debug_get_stack`

**Symbols**: `debug_get_symbols`, `debug_get_function`, `debug_get_function_context`, `debug_get_xrefs`, `debug_get_call_graph`

**Memory Map / Video**: `debug_get_memory_map`, `debug_get_vram_info`, `debug_get_screen_info`

**I/O Trace**: `debug_get_io_trace`

**State**: `debug_get_state`

**ROM**: `debug_load_rom`

**Annotations**: `debug_set_comment`, `debug_set_function_comment`, `debug_rename_function`, `debug_create_function`, `debug_delete_function`, `debug_add_label`

**ROM Database (RDB)**: `debug_get_rdb_info`, `debug_list_rdb_objects`, `debug_get_rdb_object`, `debug_find_rdb_object`, `debug_add_rdb_object`, `debug_update_rdb_object`, `debug_remove_rdb_object`, `debug_set_rdb_comment`, `debug_set_rdb_property`, `debug_save_rdb`, `debug_reload_rdb`

**RDB Links**: `debug_add_rdb_link`, `debug_remove_rdb_link`, `debug_get_rdb_links`

---

## Формат итогового отчёта

```markdown
# Analysis

## Goal
[Что анализировалось]

## ROM
[Имя файла, размер, адрес загрузки]

## Profile
[Выбранный профиль]

## Tasks
[Использованные задачи]

## Findings

### Finding 1
Location: [адрес]
Observation: [что наблюдалось]
Evidence: [MCP результаты, trace, memory]
Conclusion: [вывод]
Confidence: [High/Medium/Low]

### Finding 2
...

## Verified Facts
[Список подтверждённых фактов]

## Inferences
[Список логических выводов]

## Hypotheses
[Список непроверенных предположений]

## Unknowns
[Что осталось неясным]

## Limitations
[Ограничения анализа]

## Recommended Next Steps
[Что можно проверить дополнительно]
```

---

## Ключевые правила

- Перед state-dependent операциями вызывай `debug_get_state`
- При конфликте KB с наблюдаемым поведением → сообщи о конфликте явно
- Факты с пометкой `UNVERIFIED` или `CONFLICT` не выдавай за установленные
- `UNKNOWN` / `UNCONFIRMED` — приемлемый результат
- Не выдумывай имена функций — используй `subroutine_8123`, `candidate_renderer`
- Не начинай с анализа всех 64 КБ — локализируй область

### Работа с MAP-файлами Z88DK

При работе с `.map`-файлами **обязательно** используй `debugger/docs/z88dk_map.md` как источник формата.

Для запросов: создать MAP, построить MAP, исправить MAP, разобрать MAP, конвертировать MAP — сначала ознакомься с форматом Z88DK в `debugger/docs/z88dk_map.md`.

**Запрещено:**
- Придумывать собственный формат MAP.
- Создавать debugger-specific синтаксис MAP.
- Считать любой текстовый файл `symbol = value` форматом Z88DK MAP.
- Использовать JSON/YAML/CSV вместо реального формата Z88DK.
- Добавлять собственные поля или секции, не описанные в `debugger/docs/z88dk_map.md`.

Если данных недостаточно для корректной генерации — сообщи, какие данные отсутствуют.

---

## Правило "Документация прежде кода" (обязательно)

**Перед генерацией ЛЮБОГО файла сначала прочитай соответствующую документацию.**

Порядок действий:

```
1. Определить тип генерируемого файла
2. Найти документацию по формату этого файла
3. Прочитать документацию ЦЕЛИКОМ
4. Только после этого генерировать файл в точном соответствии с документацией
```

### Примеры

| Задача | Документация для чтения |
|--------|------------------------|
| Создать MAP-файл | `debugger/docs/z88dk_map.md` |
| Создать ROM-файл | `debugger/agent/knowledge/vector06c/rom_format.md` |
| Работать с портами | `debugger/agent/knowledge/vector06c/io.md` |
| Работать с VRAM | `debugger/agent/knowledge/vector06c/video.md` |

### Запрещено

- Генерировать файл "по памяти" без проверки документации
- Использовать формат, который ты "примерно помнишь"
- Придумывать собственный синтаксис, если есть документация
- Пропускать шаг чтения документации, даже если задача кажется простой

### Если документация не найдена

Сообщи пользователю:
```
Не найдена документация по формату [тип файла]. 
Не могу сгенерировать файл без знания точного формата.
```

Не генерируй файл в предположительном формате.
