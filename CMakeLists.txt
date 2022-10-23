cmake_minimum_required(VERSION 3.20)
project(crust C)

set(CMAKE_C_STANDARD 99)

if(APPLE)
    add_compile_definitions(MACOS)
endif()

add_executable(crust main.c daemon.c daemon.h options.h terminal.c terminal.h state.c state.h messaging.c messaging.h)
