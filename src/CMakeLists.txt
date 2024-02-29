add_executable(crust
        main.c
        daemon.c
        terminal.c
        state.c
        messaging.c
        client.c)

if(WITH_GPIO)
    target_compile_definitions(crust PRIVATE GPIO)
    target_sources(crust PRIVATE node.c)
    target_link_libraries(crust gpiod)
endif()

if(WITH_NCURSES)
    target_compile_definitions(crust PRIVATE NCURSES)
    target_sources(crust PRIVATE window.c)
    target_link_libraries(crust ncurses)
endif()