# Vector-06C Debugger

Интерактивный отладчик для эмуляции процессора КР580ВМ80А (Intel 8080), используемого в компьютере «Вектор-06Ц».

## Описание

Проект предоставляет:

- **DebugBackend** — библиотека для отладочной эмуляции: пошаговое выполнение, трассировка инструкций, контроль точек останова, дамп памяти.
- **v06c-debugger** — графический интерфейс на базе Dear ImGui + SDL2 с панелью регистров, дизассемблером, историей инструкций, окном Vector Screen и кнопками управления (Step/Run/Pause/Reset).
- **test_backend** — набор автоматических тестов для проверки корректности работы бэкенда.

## Зависимости

Для сборки необходимы:

- CMake (версии 2.8.12 или выше)
- Компилятор с поддержкой **C++17** (GCC 7+, Clang 5+)
- **Boost** (компоненты: program_options, system, thread, chrono, filesystem)
- **SDL2**
- **OpenGL**

### Установка зависимостей (Ubuntu/Debian)

```bash
sudo apt-get install build-essential cmake libboost-all-dev libsdl2-dev libgl-dev
```

### Установка зависимостей (Fedora)

```bash
sudo dnf install gcc gcc-c++ cmake make boost-devel SDL2-devel mesa-libGL-devel
```

### Дополнительные зависимости

Проект использует библиотеки из основного репозитория Vector-06C:
- `fast-filters/` — coredsp (для Resampler)
- `coreutil/` — coreutil (для SIMD)

Эти библиотеки поставляются с основным проектом и не требуют отдельной установки.

## Сборка

Dear ImGui (v1.91.8) поставляется вместе с проектом в каталоге `thirdparty/imgui/` и не требует отдельной установки.

```bash
cd debugger
mkdir build && cd build
cmake ..
make
```

После сборки в каталоге `build/` появятся:

- `test_backend` — unit-тесты бэкенда (107 тестов)
- `test_symbol_database` — тесты базы символов (22 теста)
- `test_board_smoke` — smoke-тест с реальным Board (1 тест)
- `test_vram_mapping` — тесты маппинга видеопамяти (17 тестов)
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

## Запуск smoke-теста с реальным Board

```bash
cd debugger/build
./test_board_smoke
```

Ожидается: 1/1 тест пройден.

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
