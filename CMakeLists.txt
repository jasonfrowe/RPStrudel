cmake_minimum_required(VERSION 3.21)

include(${CMAKE_CURRENT_LIST_DIR}/tools/rp6502.cmake)

project(RPStrudel C ASM)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(RPStrudel)
rp6502_map(RPStrudel src/xram.h "XRAM_.*")

# OPL2 patches and sound names, loaded into XRAM.
set(PATCHES_BIN ${CMAKE_CURRENT_BINARY_DIR}/patches.bin)
add_custom_command(
    OUTPUT ${PATCHES_BIN}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/patches.py ${PATCHES_BIN}
    DEPENDS tools/patches.py
    VERBATIM
)
rp6502_asset(RPStrudel XRAM(XRAM_PATCHES) ${PATCHES_BIN})
rp6502_asset(RPStrudel help src/help.txt)
rp6502_executable(RPStrudel DATA default RESET default)
rp6502_web(RPStrudel CONFIG [[
    title: 'RPStrudel',
]])

target_include_directories(RPStrudel PRIVATE src)
# -Oz: RAM is the limit (the pattern engine is ~45K of code), and it saves
# ~17K over -Os. Also at link time: with LTO that is where the code is
# generated, and CMake's Release flags would otherwise make it -O3.
target_compile_options(RPStrudel PRIVATE -Oz -Wall)
target_link_options(RPStrudel PRIVATE -Oz)

target_sources(RPStrudel PRIVATE
    src/main.c
    src/clock.c
    src/opl.c
    src/screen.c
    src/patches.c
    src/rat.c
    src/pattern.c
    src/mini.c
    src/prog.c
    src/player.c
)
