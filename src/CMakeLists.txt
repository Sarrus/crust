add_executable(crust main.c daemon.c terminal.c state.c messaging.c)

if(DEFINED WITH_GPIO)
    add_compile_definitions(GPIO)
    target_sources(crust PRIVATE node.c)
    target_link_libraries(crust gpiod)
endif()