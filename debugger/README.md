# Vector-06C Debugger

Интерактивный отладчик для эмуляции процессора КР580ВМ80А (Intel 8080), используемого в компьютере «Вектор-06Ц».

## Описание

Проект предоставляет:

- **DebugBackend** — библиотека для отладочной эмуляции: пошаговое выполнение, трассировка инструкций, контроль точек останова, дамп памяти.
- **debugger_gui** — графический интерфейс на базе Dear ImGui + SDL2 с панелью регистров, дизассемблером, историей инструкций и кнопками управления (Step/Run/Pause/Reset).
- **test_backend** — набор автоматических тестов для проверки корректности работы бэкенда.

## Зависимости

Для сборки необходимы:

- CMake (версии 2.8.12 или выше)
- Компилятор с поддержкой C++14 (GCC, Clang)
- SDL2
- OpenGL

### Установка зависимостей (Ubuntu/Debian)

```bash
sudo apt-get install build-essential cmake libsdl2-dev libgl-dev
```

### Установка зависимостей (Fedora)

```bash
sudo dnf install gcc gcc-c++ cmake make SDL2-devel mesa-libGL-devel
```

## Сборка

Dear ImGui (v1.91.8) поставляется вместе с проектом в каталоге `thirdparty/imgui/` и не требует отдельной установки.

```bash
cd debugger
mkdir build && cd build
cmake ..
make
```

После сборки в каталоге `build/` появятся два исполняемых файла:

- `test_backend` — тесты бэкенда
- `debugger_gui` — графический отладчик

## Запуск тестов

```bash
cd debugger/build
./test_backend
```

Ожидается: все тесты пройдены (21/21).

## Запуск отладчика

```bash
cd debugger/build
./debugger_gui
```

## Структура каталогов

```
debugger/
├── src/            # DebugBackend, дизассемблер, события
├── gui/            # Графический интерфейс (ImGui + SDL2)
├── tests/          # Автоматические тесты
├── thirdparty/     # Сторонние библиотеки (Dear ImGui)
└── CMakeLists.txt  # Конфигурация сборки
```
