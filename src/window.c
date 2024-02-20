/******************************************************************************
 * Consolidated, Realtime Updates on Status of Trains (CRUST)
 * Copyright (C) 2022-2024 Michael R. Bell <michael@black-dragon.io>
 *
 * This file is part of CRUST. For more information, visit
 * <https://github.com/Sarrus/crust>
 *
 * CRUST is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * CRUST is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * CRUST. If not, see <https://www.gnu.org/licenses/>.
 *****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include "window.h"
#include "terminal.h"
#include "state.h"
#include "client.h"
#include "messaging.h"
#include "options.h"

enum crustWindowMode {
    LOG,
    HOME
};

#define CRUST_WINDOW_MODE enum crustWindowMode
#define CRUST_WINDOW_DEFAULT_MODE HOME

struct crustLineMapEntry {
    char character;
    unsigned long xPos;
    unsigned long yPos;
};

#define CRUST_LINE_MAP_ENTRY struct crustLineMapEntry
CRUST_LINE_MAP_ENTRY * lineMap = NULL;
unsigned int lineMapLength = 0;

_Noreturn void crust_window_stop()
{
    endwin();
    exit(EXIT_SUCCESS);
}

void crust_window_handle_signal(int signal)
{
    crust_window_stop();
}

void crust_window_load_layout()
{
    FILE * layoutFile = fopen(crustOptionWindowConfigFilePath, "r");
    if(layoutFile == NULL)
    {
        crust_terminal_print("Unable to open layout file.");
        exit(EXIT_FAILURE);
    }

    char * line = NULL;
    char * lineNextSegment = NULL;
    char * intConvEndPos = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while((linelen = getline(&line, &linecap, layoutFile)) != -1)
    {
        if(line[0] != '#' && line[0] != '\r' && line[0] != '\n')
        {
            lineMapLength++;
            lineMap = realloc(lineMap, sizeof(CRUST_LINE_MAP_ENTRY) * lineMapLength);
            if(lineMap == NULL)
            {
                crust_terminal_print("Memory allocation failure");
                exit(EXIT_FAILURE);
            }
            lineNextSegment = line;
            for(int i = 0; i < 3; i++)
            {
                line = strsep(&lineNextSegment, ",\r\n");
                if(line == NULL)
                {
                    crust_terminal_print("Invalid line in mapping file");
                    exit(EXIT_FAILURE);
                }
                switch(i)
                {
                    case 0:
                        errno = 0;
                        lineMap[lineMapLength - 1].xPos = strtoul(line, &intConvEndPos, 10);
                        if(errno
                            || *intConvEndPos != '\0'
                            || intConvEndPos == line)
                        {
                            crust_terminal_print("Invalid line in mapping file");
                            exit(EXIT_FAILURE);
                        }
                        break;

                    case 1:
                        errno = 0;
                        lineMap[lineMapLength - 1].yPos = strtoul(line, &intConvEndPos, 10);
                        if(errno
                           || *intConvEndPos != '\0'
                           || intConvEndPos == line)
                        {
                            crust_terminal_print("Invalid line in mapping file");
                            exit(EXIT_FAILURE);
                        }
                        break;

                    case 2:
                        if(strlen(line) != 1)
                        {
                            crust_terminal_print("Invalid line in mapping file");
                            exit(EXIT_FAILURE);
                        }
                        lineMap[lineMapLength - 1].character = line[0];
                        break;

                    default:
                        break;
                }
            }
        }
    }
}

bool crust_window_harvest_remote_id(char ** message, CRUST_IDENTIFIER * remoteID)
{
    char * conversionStopPoint;
    *remoteID = strtoul(*message, &conversionStopPoint, 10);

    if(conversionStopPoint == *message)
    {
        return false;
    }
    else
    {
        *message = conversionStopPoint;
        return true;
    }
}

CRUST_OPCODE crust_window_interpret_message(char * message, CRUST_MIXED_OPERATION_INPUT * operationInput,
                                            CRUST_STATE * state,
                                            CRUST_IDENTIFIER * remoteID)
{
    // Return NOP if the message is too short
    if (strlen(message) < 2)
    {
        return NO_OPERATION;
    }

    char * messageAfterIdentifier = &message[2];

    if(!crust_window_harvest_remote_id(&messageAfterIdentifier, remoteID))
    {
        crust_terminal_print_verbose("No remote identifier");
        return NO_OPERATION;
    }

    // Ignore block 0
    if(*remoteID == 0)
    {
        return NO_OPERATION;
    }

    switch(message[0])
    {
        case 'B':
            switch(message[1])
            {
                case 'L':
                    // Initialise a block and try to fill it.
                    crust_block_init(&operationInput->block, state);
                    if(crust_interpret_block(messageAfterIdentifier, operationInput->block, state))
                    {
                        crust_terminal_print_verbose("Invalid block description message");
                        free(operationInput->block);
                        return NO_OPERATION;
                    }
                    return INSERT_BLOCK;

                default:
                    return NO_OPERATION;
            }

        default:
            return NO_OPERATION;
    }
}

void crust_window_enter_mode(CRUST_WINDOW_MODE targetMode)
{
    switch(targetMode)
    {
        case LOG:

            break;

        case HOME:
            initscr();
            if(cbreak() != OK || noecho() != OK || nonl() != OK || nodelay(stdscr, TRUE) != OK)
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
            for(int i = 5; i > 0; i--)
            {
                move(11, 15);
                addch(i + 0x30);
                refresh();
                sleep(1);
            }
            break;
    }
}

void crust_window_print(char * message, CRUST_WINDOW_MODE mode)
{
    switch(mode)
    {
        case LOG:
            crust_terminal_print(message);
            break;

        default:
            move(10, 0);
            clrtoeol();
            addstr(message);
            refresh();
            break;
    }
}

void crust_window_refresh_screen()
{
    clear();
    for(int i = 0; i < lineMapLength; i++)
    {
        move(lineMap[i].yPos, lineMap[i].xPos);
        addch(lineMap[i].character);
    }
}

_Noreturn void crust_window_loop(CRUST_STATE * state, struct pollfd * pollList, CRUST_WINDOW_MODE mode)
{
    char readBuffer[CRUST_MAX_MESSAGE_LENGTH];
    int readPointer = 0;
    for(;;)
    {
        // Poll the server and the keyboard
        poll(pollList, 2, -1);

        // Handle data from the server
        if(pollList[1].revents & POLLHUP)
        {
            crust_window_print("Server disconnected...", mode);
            sleep(5);
            crust_window_stop();
        }
        if(pollList[1].revents & POLLRDNORM)
        {
            // Try to read a character
            if(!read(pollList[1].fd, &readBuffer[readPointer], 1))
            {
                crust_window_print("Server disconnected...", mode);
                sleep(5);
                crust_window_stop();
            }

            // Recognise the end of the message
            if(readBuffer[readPointer] == '\n')
            {
                readBuffer[readPointer] = '\0';
                if(mode == HOME)
                {
                    crust_window_refresh_screen();
                }
                readPointer = 0;
            }
            else if(readPointer > CRUST_MAX_MESSAGE_LENGTH)
            {
                crust_window_print("Oversized message...", mode);
                sleep(5);
                crust_window_stop();
            }
            else
            {
                readPointer++;
            }
        }

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

    // Load the layout file
    crust_window_load_layout();

    // Poll stdin for user input
    pollList[0].fd = STDIN_FILENO;
    pollList[0].events = POLLRDNORM;

    // Poll the server for updates
    pollList[1].fd = crust_client_connect();
    pollList[1].events = POLLRDNORM;

    write(pollList[1].fd, "SL\n", 3);

    CRUST_WINDOW_MODE windowStartingMode;

    if(crustOptionWindowEnterLog)
    {
        windowStartingMode = LOG;
    }
    else
    {
        windowStartingMode = CRUST_WINDOW_DEFAULT_MODE;
    }

    crust_window_enter_mode(windowStartingMode);

    crust_window_loop(state, pollList, windowStartingMode);
}