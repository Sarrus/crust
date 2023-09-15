//
// Created by Michael Bell on 26/08/2023.
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include <signal.h>
#include "window.h"
#include "terminal.h"
#include "state.h"
#include "client.h"

_Noreturn void crust_window_stop()
{
    endwin();
    exit(EXIT_SUCCESS);
}

void crust_window_handle_signal(int signal)
{
    crust_window_stop();
}

_Noreturn void crust_window_loop(CRUST_STATE * state, int serverConnection)
{
    char * printstring;
    int i = 0;
    for(;;)
    {
        i++;
        i %= 4;
        move(10, 0);
        switch(i)
        {
            case 0:
                addch('|');
                break;

            case 1:
                addch('/');
                break;

            case 2:
                addch('-');
                break;

            case 3:
                addch('\\');
                break;
        }
        asprintf(&printstring, " Blocks: %i Track Circuits: %i", state->blockIndexPointer, state->trackCircuitIndexPointer);
        addstr(printstring);
        free(printstring);
        refresh();
        sleep(1);
        switch(getch())
        {
            case 'q':
                crust_window_stop();
        }
    }
}

_Noreturn void crust_window_run()
{
    // Create an initial state
    CRUST_STATE * state;
    crust_state_init(&state);

    // Register the signal handlers
    signal(SIGINT, crust_window_handle_signal);
    signal(SIGTERM, crust_window_handle_signal);

    int serverConnection = crust_client_connect();

    WINDOW * window = initscr();
    if(window == NULL || cbreak() != OK || noecho() != OK || nonl() != OK || nodelay(window, true) != OK)
    {
        crust_terminal_print("Failed to initialize screen.");
        exit(EXIT_FAILURE);
    }

    addstr("   __________  __  _____________\n"
           "  / ____/ __ \\/ / / / ___/_  __/\n"
           " / /   / /_/ / / / /\\__ \\ / /   \n"
           "/ /___/ _, _/ /_/ /___/ // /    \n"
           "\\____/_/ |_|\\____//____//_/     \n"
           "Consolidated,\n"
           "      Realtime\n"
           "            Updates on\n"
           "                  Status of\n"
           "                        Trains\n");
    refresh();

    crust_window_loop(state, serverConnection);
}