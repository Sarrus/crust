cmake_minimum_required(VERSION 3.20)
project(crust C)

set(CMAKE_C_STANDARD 99)

if(APPLE)
    add_compile_definitions(MACOS)
endif()

if(DEFINED WITH_TESTING)
    add_compile_definitions(TESTING)
endif()

add_subdirectory(src)

install(TARGETS crust)

if(DEFINED WITH_SYSTEMD)
    add_compile_definitions(SYSTEMD)
    target_link_libraries(crust systemd)
    install(FILES platforms/systemd/crust-daemon.service DESTINATION /usr/lib/systemd/system/)
    install(FILES platforms/systemd/crust-node.service DESTINATION /usr/lib/systemd/system/)
endif()


