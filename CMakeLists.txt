cmake_minimum_required(VERSION 3.20)
project(crust C)

set(CMAKE_C_STANDARD 99)

add_subdirectory(src)

target_link_libraries(crust curses)

if(APPLE)
    target_compile_definitions(crust PRIVATE MACOS)
endif()

if(WITH_TESTING)
    target_compile_definitions(crust PRIVATE TESTING)
endif()

install(TARGETS crust)

if(WITH_SYSTEMD)
    target_compile_definitions(crust PRIVATE SYSTEMD)
    target_link_libraries(crust systemd)
    install(FILES platforms/systemd/crust-daemon.service DESTINATION /usr/lib/systemd/system/)
    if(WITH_GPIO)
        install(FILES platforms/systemd/crust-node.service DESTINATION /usr/lib/systemd/system/)
    endif()
endif()


