//
// Created by Michael Bell on 26/08/2023.
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include "window.h"
#include "terminal.h"

_Noreturn void crust_window_run()
{
    char buffer;
    WINDOW * window = initscr();
    if(window == NULL || cbreak() != OK || noecho() != OK || nonl() != OK)
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

    for(int i = 0; i < 10; i++)
    {
        move(10, 0);
        switch(i % 4)
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
        refresh();
        sleep(1);
    }
    endwin();
    exit(EXIT_SUCCESS);
}