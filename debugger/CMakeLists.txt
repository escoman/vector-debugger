cmake_minimum_required(VERSION 2.8.12)
project(vector-debugger)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -g -pthread")

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------

set(ROOT_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SRC_DIR     ${ROOT_DIR}/src)
set(DBG_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DBG_TST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests)
set(DBG_GUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gui)
set(IMGUI_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)

# ---------------------------------------------------------------------------
# Include paths — we need headers from the main project (memory.h, i8080.h)
# and from the debugger module (backend.h, opcode_info.h).
# ---------------------------------------------------------------------------

include_directories(${SRC_DIR})
include_directories(${DBG_SRC_DIR})
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${ROOT_DIR}/fast-filters/sources)  # coredsp for resampler
include_directories(${ROOT_DIR}/coreutil/sources)       # coreutil for coredsp

# ---------------------------------------------------------------------------
# Find Boost (required for Board sources)
# ---------------------------------------------------------------------------

find_package(Boost REQUIRED COMPONENTS program_options system thread chrono filesystem)

# ---------------------------------------------------------------------------
# Sources
#
# We compile the CPU core (i8080.cpp) and memory model (memory.cpp) from the
# main project.  We deliberately DO NOT link hal.cpp — the test binary
# provides its own minimal HAL implementation inside test_backend.cpp.
# ---------------------------------------------------------------------------

set(CORE_SOURCES
    ${SRC_DIR}/i8080.cpp
    ${SRC_DIR}/memory.cpp
)

# Board sources for full emulator integration (Stage 3.2)
set(BOARD_SOURCES
    ${SRC_DIR}/board.cpp
    ${SRC_DIR}/cadence.cpp
    ${SRC_DIR}/tv.cpp
    ${SRC_DIR}/sound.cpp
    ${SRC_DIR}/filler.cpp
    ${SRC_DIR}/options.cpp
    ${SRC_DIR}/util.cpp
    ${SRC_DIR}/resampler.cpp
    ${SRC_DIR}/hal.cpp
    ${SRC_DIR}/icon.cpp
)

set(DBG_SOURCES
    ${DBG_SRC_DIR}/backend.cpp
    ${DBG_SRC_DIR}/disassembler.cpp
    ${DBG_SRC_DIR}/debug_memory.cpp
)

set(TEST_SOURCES
    ${DBG_TST_DIR}/test_backend.cpp
)

set(BOARD_TEST_SOURCES
    ${DBG_TST_DIR}/test_board_smoke.cpp
    ${DBG_TST_DIR}/dummy_resources.cpp
)

# ---------------------------------------------------------------------------
# Find SDL2 (required for Board sources)
# ---------------------------------------------------------------------------

set(CMAKE_MODULE_PATH ${ROOT_DIR}/cmake)
find_package(SDL2 REQUIRED)

# ---------------------------------------------------------------------------
# Test executable
# ---------------------------------------------------------------------------

add_executable(test_backend
    ${TEST_SOURCES}
    ${DBG_SOURCES}
    ${CORE_SOURCES}
)

target_compile_definitions(test_backend PRIVATE DEBUGGER_NO_BOARD)

target_link_libraries(test_backend
    ${Boost_LIBRARIES}
)

# ---------------------------------------------------------------------------
# Board integration test executable (Stage 3.2)
# ---------------------------------------------------------------------------

add_executable(test_board_smoke
    ${BOARD_TEST_SOURCES}
    ${DBG_SOURCES}
    ${BOARD_SOURCES}
    ${CORE_SOURCES}
)

target_include_directories(test_board_smoke PRIVATE ${DBG_GUI_DIR})

target_link_libraries(test_board_smoke
    ${SDL2_LIBRARY}
    ${Boost_LIBRARIES}
)

# ---------------------------------------------------------------------------
# Dear ImGui sources
# ---------------------------------------------------------------------------

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl2.cpp
)

# ---------------------------------------------------------------------------
# GUI executable
# ---------------------------------------------------------------------------

# Find OpenGL
find_package(OpenGL REQUIRED)

include_directories(${SDL2_INCLUDE_DIR})
include_directories(${IMGUI_DIR})
include_directories(${IMGUI_DIR}/backends)
include_directories(${DBG_GUI_DIR})

set(GUI_SOURCES
    ${DBG_GUI_DIR}/main.cpp
    ${DBG_GUI_DIR}/gui.cpp
    ${DBG_GUI_DIR}/board_wrapper.cpp
    ${DBG_GUI_DIR}/memory_inspector_window.cpp
    ${DBG_GUI_DIR}/stack_view_window.cpp
    ${DBG_GUI_DIR}/breakpoints_window.cpp
    ${DBG_GUI_DIR}/disassembly_window.cpp
    ${DBG_GUI_DIR}/execution_trace_window.cpp
)

add_executable(debugger_gui
    ${GUI_SOURCES}
    ${IMGUI_SOURCES}
    ${DBG_SOURCES}
    ${BOARD_SOURCES}
    ${CORE_SOURCES}
)

target_link_libraries(debugger_gui
    ${SDL2_LIBRARY}
    ${OPENGL_LIBRARIES}
    ${Boost_LIBRARIES}
    pthread
)
