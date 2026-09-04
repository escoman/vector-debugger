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
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)  # stb_image.h

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
# NOTE: hal.cpp is deliberately excluded — each target provides its own HAL.
# NOTE: icon.cpp is replaced with debugger's own version (uses stb_image for PNG)
set(BOARD_SOURCES
    ${SRC_DIR}/board.cpp
    ${SRC_DIR}/cadence.cpp
    ${SRC_DIR}/tv.cpp
    ${SRC_DIR}/sound.cpp
    ${SRC_DIR}/filler.cpp
    ${SRC_DIR}/options.cpp
    ${SRC_DIR}/util.cpp
    ${SRC_DIR}/resampler.cpp
    ${DBG_SRC_DIR}/icon.cpp
)

set(DBG_SOURCES
    ${DBG_SRC_DIR}/backend.cpp
    ${DBG_SRC_DIR}/disassembler.cpp
    ${DBG_SRC_DIR}/debug_memory.cpp
    ${DBG_SRC_DIR}/symbol_database.cpp
    ${DBG_SRC_DIR}/project_file.cpp
    ${DBG_SRC_DIR}/vram_mapping.cpp
    ${DBG_SRC_DIR}/no_board_target.cpp
)

# Debug Adapter — bridge between Debugger Core and Vector Emulator (Stage 3.13/3.13a)
# Contains HAL function definitions and IDebugTarget implementation.
set(ADAPTER_SOURCES
    ${DBG_SRC_DIR}/debug_adapter.cpp
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

target_link_libraries(test_backend
    ${Boost_LIBRARIES}
)

# ---------------------------------------------------------------------------
# Symbol Database test executable (Stage 4.1)
# ---------------------------------------------------------------------------

add_executable(test_symbol_database
    ${DBG_TST_DIR}/test_symbol_database.cpp
    ${DBG_SRC_DIR}/symbol_database.cpp
)

# ---------------------------------------------------------------------------
# VRAM Mapping test executable (Stage 4 Enhanced)
# ---------------------------------------------------------------------------

add_executable(test_vram_mapping
    ${DBG_TST_DIR}/test_vram_mapping.cpp
    ${DBG_SRC_DIR}/vram_mapping.cpp
)

# ---------------------------------------------------------------------------
# Board integration test executable (Stage 3.2)
# ---------------------------------------------------------------------------

add_executable(test_board_smoke
    ${BOARD_TEST_SOURCES}
    ${DBG_SOURCES}
    ${BOARD_SOURCES}
    ${SRC_DIR}/hal.cpp
    ${CORE_SOURCES}
)

target_link_libraries(test_board_smoke
    ${SDL2_LIBRARY}
    ${Boost_LIBRARIES}
)

# ---------------------------------------------------------------------------
# Workspace Manager test executable (Stage 5.1)
# ---------------------------------------------------------------------------

add_executable(test_workspace
    ${DBG_TST_DIR}/test_workspace.cpp
    ${DBG_GUI_DIR}/workspace_manager.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
)

target_include_directories(test_workspace PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${DBG_GUI_DIR}
)

target_link_libraries(test_workspace
    ${Boost_LIBRARIES}
)

# ---------------------------------------------------------------------------
# GUI Smoke Test (Stage 5.1)
# ---------------------------------------------------------------------------
# Standalone test that spawns v06c-debugger and checks it doesn't crash.
# No project libraries needed — just POSIX fork/exec/wait.

add_executable(test_gui_smoke
    ${DBG_TST_DIR}/test_gui_smoke.cpp
)

# Ensure v06c-debugger is built before running this test
add_dependencies(test_gui_smoke v06c-debugger)

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
# debugger_core — static library (Stage 3.13a)
#
# Contains Debugger Core (backend, disassembler, etc.) + NoBoardTarget.
# Does NOT depend on Board, ImGui, SDL2, or OpenGL.
# Does NOT contain debug_adapter.cpp (which has HAL + Board dependencies).
# ---------------------------------------------------------------------------

add_library(debugger_core STATIC
    ${DBG_SOURCES}
)

target_include_directories(debugger_core PUBLIC
    ${DBG_SRC_DIR}
    ${SRC_DIR}
    ${Boost_INCLUDE_DIRS}
    ${ROOT_DIR}/fast-filters/sources
    ${ROOT_DIR}/coreutil/sources
)

target_link_libraries(debugger_core
    ${Boost_LIBRARIES}
    pthread
)

# ---------------------------------------------------------------------------
# debugger_adapter — static library (Stage 3.13a)
#
# Contains DebugAdapter (IDebugTarget implementation + HAL functions).
# Links with Board sources and CORE_SOURCES for full emulator integration.
# ---------------------------------------------------------------------------

add_library(debugger_adapter STATIC
    ${ADAPTER_SOURCES}
    ${BOARD_SOURCES}
    ${CORE_SOURCES}
)

target_include_directories(debugger_adapter PUBLIC
    ${DBG_SRC_DIR}
    ${SRC_DIR}
)

target_link_libraries(debugger_adapter
    debugger_core
    ${SDL2_LIBRARY}
    ${Boost_LIBRARIES}
    pthread
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
    ${DBG_GUI_DIR}/workspace_manager.cpp
    ${DBG_GUI_DIR}/memory_inspector_window.cpp
    ${DBG_GUI_DIR}/stack_view_window.cpp
    ${DBG_GUI_DIR}/breakpoints_window.cpp
    ${DBG_GUI_DIR}/disassembly_window.cpp
    ${DBG_GUI_DIR}/execution_trace_window.cpp
    ${DBG_GUI_DIR}/io_inspector_window.cpp
    ${DBG_GUI_DIR}/vector_screen_window.cpp
    ${DBG_GUI_DIR}/memory_map_window.cpp
    ${DBG_GUI_DIR}/functions_window.cpp
    ${DBG_GUI_DIR}/xrefs_window.cpp
    ${DBG_GUI_DIR}/call_graph_window.cpp
    ${DBG_GUI_DIR}/search_window.cpp
)

# ---------------------------------------------------------------------------
# Binary resources for v06c-debugger (boot ROM, window icon)
# ---------------------------------------------------------------------------

find_program(OBJCOPY objcopy)

# Detect output format for objcopy based on platform
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(DBG_OBJCOPY_FMT "-O" "elf64-x86-64")
else()
    set(DBG_OBJCOPY_FMT "-O" "elf32-i386")
endif()

# Copy boots.bin to build directory and convert to object file
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boots.bin
    COMMAND ${CMAKE_COMMAND} -E copy ${ROOT_DIR}/boot/boots.bin ${CMAKE_CURRENT_BINARY_DIR}/boots.bin
    DEPENDS ${ROOT_DIR}/boot/boots.bin
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/boots.bin.o
    COMMAND ${OBJCOPY} -I binary ${DBG_OBJCOPY_FMT} boots.bin boots.bin.o
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/boots.bin
)

# Copy icon64.png to build directory and convert to object file
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon64.png
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/res/icon64.png ${CMAKE_CURRENT_BINARY_DIR}/icon64.png
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/icon64.png
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon64.png.o
    COMMAND ${OBJCOPY} -I binary ${DBG_OBJCOPY_FMT} icon64.png icon64.png.o
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/icon64.png
)

add_executable(v06c-debugger
    ${GUI_SOURCES}
    ${IMGUI_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/boots.bin.o
    ${CMAKE_CURRENT_BINARY_DIR}/icon64.png.o
)

target_link_libraries(v06c-debugger
    debugger_adapter
    debugger_core
    ${SDL2_LIBRARY}
    ${OPENGL_LIBRARIES}
    ${Boost_LIBRARIES}
    pthread
)

# Binary resources produce absolute symbols (_binary_*_size) which cannot
# be used with position-independent code on x86_64. Disable PIE for this target.
set_target_properties(v06c-debugger PROPERTIES
    LINK_FLAGS "-no-pie"
    POSITION_INDEPENDENT_CODE OFF
)
