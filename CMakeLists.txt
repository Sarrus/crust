cmake_minimum_required(VERSION 3.20)
project(crust C)
include(ExternalProject)

set(CMAKE_C_STANDARD 99)

ExternalProject_Add(libcyaml
        GIT_REPOSITORY "https://github.com/tlsa/libcyaml.git"
        GIT_TAG "v1.4.1"
        SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/libcyaml
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make -C ${CMAKE_CURRENT_LIST_DIR}/libcyaml/ VARIANT=release
        INSTALL_COMMAND ""
)

add_subdirectory(src)

add_dependencies(crust libcyaml)
target_link_libraries(crust ${CMAKE_CURRENT_LIST_DIR}/libcyaml/build/release/libcyaml.a)

if(APPLE)
    target_compile_definitions(crust PRIVATE MACOS)
else ()
    target_compile_definitions(crust PRIVATE _GNU_SOURCE)
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


