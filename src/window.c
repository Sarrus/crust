//
// Created by Michael Bell on 26/08/2023.
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include <signal.h>
#include <poll.h>
#include "window.h"
#include "terminal.h"
#include "state.h"
#include "client.h"
#include "messaging.h"

_Noreturn void crust_window_stop()
{
    endwin();
    exit(EXIT_SUCCESS);
}

void crust_window_handle_signal(int signal)
{
    crust_window_stop();
}

_Noreturn void crust_window_loop(CRUST_STATE * state, struct pollfd * pollList)
{
    char * printString;
    char readBuffer[CRUST_MAX_MESSAGE_LENGTH];
    int readPointer = 0;
    int i = 0;
    for(;;)
    {
        // Poll the server and the keyboard
        poll(pollList, 2, -1);

        // Handle data from the server
        if(pollList[1].revents & POLLHUP)
        {
            addstr("Server disconnected...");
            refresh();
            sleep(5);
            crust_window_stop();
        }
        if(pollList[1].revents & POLLRDNORM)
        {
            // Try to read a character
            if(!read(pollList[1].fd, &readBuffer[readPointer], 1))
            {
                addstr("Server disconnected...");
                refresh();
                sleep(5);
                crust_window_stop();
            }

            // Recognise the end of the message
            if(readBuffer[readPointer] == '\n')
            {
                readPointer = 0;
            }
            else if(readPointer > CRUST_MAX_MESSAGE_LENGTH)
            {
                addstr("Oversized message...");
                refresh();
                sleep(5);
                crust_window_stop();
            }
            else
            {
                readPointer++;
            }
        }

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
        asprintf(&printString, " Blocks: %i Track Circuits: %i", state->blockIndexPointer, state->trackCircuitIndexPointer);
        addstr(printString);
        free(printString);
        refresh();

        // Handle data from the keyboard
        switch(getch())
        {
            case 'q':
                crust_window_stop();
        }
    }
}

_Noreturn void crust_window_run()
{
    struct pollfd pollList[2];

    // Create an initial state
    CRUST_STATE * state;
    crust_state_init(&state);

    // Register the signal handlers
    signal(SIGINT, crust_window_handle_signal);
    signal(SIGTERM, crust_window_handle_signal);

    // Poll stdin for user input
    pollList[0].fd = STDIN_FILENO;
    pollList[0].events = POLLRDNORM;

    // Poll the server for updates
    pollList[1].fd = crust_client_connect();
    pollList[1].events = POLLRDNORM;

    write(pollList[1].fd, "SL\n", 3);

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

    crust_window_loop(state, pollList);
}