cmake_minimum_required(VERSION 3.21)

include(${CMAKE_CURRENT_LIST_DIR}/tools/rp6502.cmake)

if(RP6502_BASIC)
    project(MY-RP6502-PROJECT BASIC)
    add_executable(hello)
    rp6502_asset(hello help src/help.txt)
    rp6502_asset(hello hello.bas src/main.bas)
    rp6502_basic(hello hello.bas)
    rp6502_web(hello)
else()
    project(MY-RP6502-PROJECT C CXX ASM)
    add_executable(hello)
    rp6502_map(hello src/xram.h "XRAM_.*")
    rp6502_asset(hello help src/help.txt)
    rp6502_executable(hello DATA default RESET default)
    rp6502_web(hello)
    target_sources(hello PRIVATE
        src/main.c
    )
endif()
