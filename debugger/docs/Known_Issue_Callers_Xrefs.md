# Known Issue — пустые `callers` / `xrefs` / `call_graph` в v06c-mcp

- **Статус:** Open (обнаружено Agent'ом при ROM-анализе; починка отложена по решению —
  зафиксировано как TODO, код не менялся)
- **Обнаружено:** Stage 4 ROM-анализ (отчёт `Stage3.md`, func `render_glyph_vram @0x0551`)
- **Влияние:** `debug_get_xrefs`, `debug_get_call_graph` и поле `callers` в
  `debug_get_function_context` возвращают пустые списки для всех функций.
- **Приоритет:** средний (блокирует авто-построение графа вызовов; обходится на клиенте)

## Важно: дефект в ЭТОМ репозитории

`v06c-mcp` собирается из `debugger/`. Агент заключил «дефект во внешнем сервере,
исходников нет», потому что видел только Python-сканер (`abi_scan.py`) в `vector-games`.
Но `abi_scan.py:474` лишь честно считает `len(context.get("callers") or [])` — источник
проблемы здесь, в серверном слое.

## Корень (подтверждено кодом)

Единственный, кто наполняет `xrefs_` / `callTargets_`, — это
`SymbolDatabase::rebuildXrefs()` (`src/symbol_database.cpp:241`). Он вызывается **только
из тестов** (`agent/tests/test_agent_api.cpp`, `tests/test_symbol_database.cpp`) и **ни
разу** из реального потока: ни в `DebugBackend::loadRom()`, ни в `analyzeCode`, ни в
`createFunction`, ни в MCP-обработчиках.

Все спорные методы читают ровно эти незаполненные поля:

| Метод | Что читает | Файл:строка | Результат в рантайме |
|---|---|---|---|
| `debug_get_xrefs` | `db.xrefsTo()` / `db.xrefsFrom()` | `agent/agent_api.cpp:2245` | пусто |
| `debug_get_function_context` → `callers` | `db.xrefsTo()` + фильтр по CALL-opcode | `agent/agent_api.cpp:2948` | пусто |
| `debug_get_call_graph` | `db.callGraph()` (построен на `xrefs_`+`callTargets_`) | `src/symbol_database.cpp:332` | пусто |

### Не является дефектом
Поле `callees` в `debug_get_function_context` считается НЕ из `xrefs_`, а из
дизассемблированных инструкций самой функции (`agent/agent_api.cpp:2962`). Для
функции-листа (без вложенных CALL, как `render_glyph`) пустой `callees` корректен.

### Отличать от RDB links
`debug_add_rdb_link` / RDB `links` (`rdb/rdb_controller.h:124`) — это ручные персистентные
связи, они работают. Авто-граф (`xrefs`/`call_graph`) задуман как производный от скана кода
через `rebuildXrefs` — ломается именно он.

## Вторичный риск: рассинхрон линейного скана

Даже после привязки вызова `rebuildXrefs` — текущая реализация идёт **линейно от 0x0000**
(если нет Code-регионов из MAP), продвигаясь по длине декодированной инструкции
(`src/symbol_database.cpp:262-302`). На ROM с вкраплениями данных рассинхрон «съедает»
байты `CD 51 05`, и часть реальных вызовов не будет найдена. В отчёте callers
(`0x25F6`, `0x2B72`, `0x2B81`) нашлись только побайтовым поиском, а не линейного прогоном.

## План починки (для будущей итерации)

1. **Привязать `rebuildXrefs` к реальному потоку** — ленивый пересчёт по флагу
   `xrefsDirty_`: сбрасывать на `loadRom`/`clear`/изменении символов-регионов;
   устанавливать после `analyzeCode`/`createFunction`; вызывать из
   `getXrefs`/`getFunctionContext`/`getCallGraph` через не-const
   `backend_.symbolDatabase()`, `readByte = backend_.readMemory` (peek, без мутаций).
2. **Устойчивость к рассинхрону** — сеять линейный проход с границ каждой известной
   функции/Code-региона (диапазоны уже отдаёт `analyzeCodeMulti`) и/или фильтровать
   xrefs по признаку «target ∈ известная функция/метка» (режет ложные срабатывания).
3. **Регресс-тесты** (`test_agent_api.cpp`): синтетический образ с заведомым
   `CD <lo> <hi>` + data-вкрапления → `getXrefs`/`getFunctionContext.callers` возвращают
   адрес вызова (не пусто). Существующий `test_get_xrefs_empty` оставить как есть.

Все три запроса — read-only; скан ≤ 64K по флагу, без тяжёлой работы на каждый вызов.

## Обходной путь (применялся в Stage 3/4)

Места вызова восстанавливали полным дизассемблированием `0x0100..0x50FE` + побайтовым
поиском `CD <lo> <hi>` по каждой функции на клиенте (`abi_scan.py`) — независимо от
сломанного `debug_get_xrefs`.
