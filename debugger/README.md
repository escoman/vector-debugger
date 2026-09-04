# Vector-06C Debugger

Интерактивный отладчик для эмуляции процессора КР580ВМ80А (Intel 8080), используемого в компьютере «Вектор-06Ц».

## Описание

Проект предоставляет:

- **DebugBackend** — библиотека для отладочной эмуляции: пошаговое выполнение, трассировка инструкций, контроль точек останова, дамп памяти.
- **v06c-debugger** — графический интерфейс на базе Dear ImGui + SDL2 с панелью регистров, дизассемблером, историей инструкций, окном Vector Screen и кнопками управления (Step/Run/Pause/Reset).
- **test_backend** — набор автоматических тестов для проверки корректности работы бэкенда.

## Зависимости

Для сборки необходимы:

- **CMake** (версии 2.8.12 или выше) — генерирует Makefile'ы из `CMakeLists.txt`.
- **Компилятор с поддержкой C++17** (GCC 7+, Clang 5+) — исходный код использует `std::optional`, `if constexpr`, structured bindings и другие возможности C++17.
- **Boost** (компоненты: program_options, system, thread, chrono, filesystem) — используется модулем `Board` (основной эмулятор): файловая система для загрузки ROM, потоки для эмуляционного цикла, program_options для разбора аргументов.
- **SDL2** — создание окна, обработка ввода, OpenGL-контекст. Также нужен для `icon_set()` (установка иконки окна из встроенных данных).
- **OpenGL** (libGL) — рендеринг через ImGui backend `imgui_impl_opengl2`.
- **objcopy** (из binutils) — встраивает бинарные ресурсы (`boots.bin`, `icon64.rgba`) в ELF-объект для линковки с исполняемым файлом.

### Установка зависимостей (Ubuntu/Debian)

```bash
sudo apt-get install build-essential cmake libboost-all-dev libsdl2-dev libgl-dev
```

`build-essential` включает GCC, make и binutils (нужный нам `objcopy`).

### Установка зависимостей (Fedora)

```bash
sudo dnf install gcc gcc-c++ cmake make boost-devel SDL2-devel mesa-libGL-devel
```

### Дополнительные зависимости

Проект использует библиотеки из основного репозитория Vector-06C:
- `fast-filters/` — coredsp (для Resampler)
- `coreutil/` — coreutil (для SIMD)

Эти библиотеки поставляются с основным проектом и не требуют отдельной установки.

## Dear ImGui — ветка `docking`

Отладчик использует **докинг** (перетаскиваемые и стыкуемые окна), поэтому требуется ветка **`docking`**, а не релиз из master-ветки.

**Почему не подходит релиз (например, v1.91.8 из master):**
- API докинга (`DockSpace`, `DockBuilderAddNode`, `DockBuilderSplitNode`, `DockBuilderDockWindow`, `ImGuiDockNodeFlags_PassthruCentralNode`, `ImGuiConfigFlags_DockingEnable`) отсутствует в релизных тегах master-ветки.
- Эти функции доступны только в ветке `docking`, где докинг является штатным режимом.

**Почему не включена в репозиторий:** Dear ImGui — сторонняя библиотека с собственным циклом releases. Чтобы не дублировать чужой код и иметь возможность обновляться, она подключается как git-клон.

## Сборка

### 1. Клонировать Dear ImGui из ветки `docking`

```bash
cd debugger
mkdir -p thirdparty
git clone --branch docking https://github.com/ocornut/imgui.git thirdparty/imgui
```

> **Примечание:** `--branch docking` клонирует только нужную ветку. Флаг `--depth 1` можно добавить для ускорения, если полная история не нужна.

### 2. Конфигурация и сборка

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

`cmake ..` находит Boost, SDL2 и OpenGL через `find_package()`, после чего генерирует Makefile'ы. `make` компилирует все цели параллельно.

После сборки в каталоге `build/` появятся:

- `test_backend` — unit-тесты бэкенда (107 тестов)
- `test_symbol_database` — тесты базы символов (22 теста)
- `test_board_smoke` — smoke-тест с реальным Board (1 тест)
- `test_vram_mapping` — тесты маппинга видеопамяти (17 тестов)
- `test_workspace` — тесты менеджера рабочих пространств
- `test_gui_smoke` — smoke-тест запуска GUI
- `v06c-debugger` — графический отладчик

## Запуск тестов

```bash
cd debugger/build
./test_backend
```

Ожидается: все тесты пройдены (107/107).

```bash
./test_symbol_database
```

Ожидается: 22/22.

```bash
./test_vram_mapping
```

Ожидается: 17/17.

```bash
./test_workspace
```

Ожидается: все тесты пройдены. Требует Dear ImGui из ветки `docking` (использует DockBuilder API).

## Запуск smoke-теста с реальным Board

```bash
cd debugger/build
./test_board_smoke
```

Ожидается: 1/1 тест пройден.

## Запуск smoke-теста GUI

```bash
cd debugger/build
./test_gui_smoke
```

Запускает `v06c-debugger` и проверяет, что он не падает при старте. Требует собранного `v06c-debugger` (CMake-зависимость проставлена автоматически).

## Запуск отладчика

```bash
cd debugger/build

# Без ROM — загрузится встроенный бут-ПЗУ Вектора
./v06c-debugger

# ROM-файл (загружается с адреса 0x0100, после таблицы векторов прерываний)
./v06c-debugger ../../testroms/clrs.rom

# R0M-файл (загружается с адреса 0x0000, сырой образ памяти)
./v06c-debugger ../../testroms/image.r0m

# С явным указанием адреса загрузки (переопределяет автоопределение)
./v06c-debugger ../../testroms/custom.bin 0x8000
```

**Правила загрузки ROM-файлов:**
- `.rom` — загружаются с адреса `0x0100` (после таблицы векторов прерываний 0x0000–0x00FF)
- `.r0m` — загружаются с адреса `0x0000` (сырой образ памяти)
- Второй аргумент — явное указание адреса загрузки (hex), переопределяет автоопределение по расширению

## Структура каталогов

```
debugger/
├── src/            # DebugBackend, дизассемблер, события
├── gui/            # Графический интерфейс (ImGui + SDL2)
├── tests/          # Автоматические тесты
├── thirdparty/     # Сторонние библиотеки (Dear ImGui)
└── CMakeLists.txt  # Конфигурация сборки
```
