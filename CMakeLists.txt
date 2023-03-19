cmake_minimum_required(VERSION 3.20)
project(crust C)

set(CMAKE_C_STANDARD 99)

if(APPLE)
    add_compile_definitions(MACOS)
endif()

if(DEFINED WITH_TESTING)
    add_compile_definitions(TESTING)
endif()

add_executable(crust main.c daemon.c terminal.c state.c messaging.c)

if(DEFINED WITH_GPIO)
    add_compile_definitions(GPIO)
    target_sources(crust PRIVATE node.c)
    target_link_libraries(crust gpiod)
endif()