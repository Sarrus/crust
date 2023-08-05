add_executable(crust main.c daemon.c terminal.c state.c messaging.c)

if(WITH_GPIO)
    target_compile_definitions(crust PRIVATE GPIO)
    target_sources(crust PRIVATE node.c)
    target_link_libraries(crust gpiod)
endif()