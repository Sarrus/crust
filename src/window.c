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
#include <regex.h>
#include "window.h"
#include "terminal.h"
#include "state.h"
#include "client.h"
#include "messaging.h"
#include "config.h"

enum crustWindowMode {
    LOG,
    WELCOME,
    HOME,
    MANUAL_INTERPOSE,
    MANUAL_CLEAR
};

#define CRUST_WINDOW_MODE enum crustWindowMode
#define CRUST_WINDOW_DEFAULT_MODE WELCOME

struct crustLineMapEntry {
    char character;
    unsigned long xPos;
    unsigned long yPos;
    long trackCircuitNumber;
    bool occupied;
    long berthNumber;
    long berthCharacterPos;
    char berthCharacter;
    char berthNumberCharacter;
    bool showBerth;
};

#define CRUST_LINE_MAP_ENTRY struct crustLineMapEntry
CRUST_LINE_MAP_ENTRY * lineMap = NULL;
unsigned int lineMapLength = 0;

#define CRUST_COLOUR_GREY 8

#define CRUST_COLOUR_PAIR_DEFAULT 1
#define CRUST_COLOUR_PAIR_CLEAR 2
#define CRUST_COLOUR_PAIR_OCCUPIED 3
#define CRUST_COLOUR_PAIR_HEADCODE 4
#define CRUST_COLOUR_PAIR_BERTH_NUMBER 5

#define CRUST_REGEX_CAPTURE_HEADCODE_FROM_BLOCK "^.*\\/.(.*):.*"
regex_t regexCaptureHeadcodeFromBlock;

_Noreturn void crust_window_stop()
{
    endwin();
    exit(EXIT_SUCCESS);
}

void crust_window_handle_signal(int signal)
{
    crust_window_stop();
}

void crust_window_compile_regexs()
{
    if(regcomp(&regexCaptureHeadcodeFromBlock, CRUST_REGEX_CAPTURE_HEADCODE_FROM_BLOCK, REG_EXTENDED))
    {
        crust_terminal_print("Error compiling regexes.");
        exit(EXIT_FAILURE);
    }
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
    char berthNumberString[CRUST_HEADCODE_LENGTH + 1];
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

            lineMap[lineMapLength -1].trackCircuitNumber = -1;
            lineMap[lineMapLength -1].occupied = false;
            lineMap[lineMapLength -1].berthNumber = -1;
            lineMap[lineMapLength -1].berthCharacterPos = -1;
            lineMap[lineMapLength -1].berthCharacter = '_';
            lineMap[lineMapLength -1].showBerth = false;

            lineNextSegment = line;
            for(int i = 0; i < 6; i++)
            {
                line = strsep(&lineNextSegment, ",\r\n");
                if(line == NULL)
                {
                    if(i < 3)
                    {
                        crust_terminal_print("Invalid line in mapping file");
                        exit(EXIT_FAILURE);
                    }

                }
                else
                {
                    switch (i)
                    {
                        case 0:
                            errno = 0;
                            lineMap[lineMapLength - 1].xPos = strtoul(line, &intConvEndPos, 10);
                            if (errno
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
                            if (errno
                                || *intConvEndPos != '\0'
                                || intConvEndPos == line)
                            {
                                crust_terminal_print("Invalid line in mapping file");
                                exit(EXIT_FAILURE);
                            }
                            break;

                        case 2:
                            if (strlen(line) != 1)
                            {
                                crust_terminal_print("Invalid line in mapping file");
                                exit(EXIT_FAILURE);
                            }
                            lineMap[lineMapLength - 1].character = line[0];
                            break;

                        case 3:
                            errno = 0;
                            lineMap[lineMapLength - 1].trackCircuitNumber = (long) strtoul(line, &intConvEndPos, 10);
                            if (errno
                                || *intConvEndPos != '\0'
                                || intConvEndPos == line)
                            {
                                lineMap[lineMapLength - 1].trackCircuitNumber = -1;
                            }
                            break;

                        case 4:
                            errno = 0;
                            lineMap[lineMapLength - 1].berthNumber = (long) strtoul(line, &intConvEndPos, 10);
                            if (errno
                                || *intConvEndPos != '\0'
                                || intConvEndPos == line)
                            {
                                lineMap[lineMapLength - 1].berthNumber = -1;
                            }
                            break;

                        case 5:
                            errno = 0;
                            lineMap[lineMapLength - 1].berthCharacterPos = (long) strtoul(line, &intConvEndPos, 10);
                            if (errno
                                || *intConvEndPos != '\0'
                                || intConvEndPos == line)
                            {
                                lineMap[lineMapLength - 1].berthCharacterPos = -1;
                            }
                            else
                            {
                                snprintf(berthNumberString, CRUST_HEADCODE_LENGTH + 1, "%li_________________ ", lineMap[lineMapLength - 1].berthNumber);
                                lineMap[lineMapLength - 1].berthNumberCharacter = berthNumberString[lineMap[lineMapLength - 1].berthCharacterPos];
                            }
                            break;

                        default:
                            break;
                    }
                }
            }
        }
    }
}

void crust_window_set_occupation(CRUST_IDENTIFIER trackCircuitID, bool occupation)
{
    for(int i = 0; i < lineMapLength; i++)
    {
        if(lineMap[i].trackCircuitNumber == trackCircuitID)
        {
            lineMap[i].occupied = occupation;
        }
    }
}

void crust_window_update_berth(CRUST_IDENTIFIER blockID, char * headcode)
{
    bool allUnderscores = true;
    size_t headcodeLength = strlen(headcode);
    for(int i = 0; i < headcodeLength; i++)
    {
        if(headcode[i] != '_')
        {
            allUnderscores = false;
        }
    }
    for(int i = 0; i < headcodeLength; i++)
    {
        for(int j = 0; j < lineMapLength; j++)
        {
            if(lineMap[j].berthNumber == blockID && lineMap[j].berthCharacterPos == i)
            {
                lineMap[j].berthCharacter = headcode[i];
                lineMap[j].showBerth = !allUnderscores;
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

CRUST_OPCODE crust_window_interpret_message(char * message, CRUST_IDENTIFIER * remoteID, char ** headcode)
{
    regmatch_t regexMatches[2];
    size_t headcodeLength;

    size_t length = strlen(message);
    // Return NOP if the message is too short
    if (length < 4)
    {
        return NO_OPERATION;
    }

    char * messageAfterIdentifier = &message[2];

    if(!crust_window_harvest_remote_id(&messageAfterIdentifier, remoteID))
    {
        crust_terminal_print_verbose("No remote identifier");
        return NO_OPERATION;
    }

    switch(message[0])
    {
        case 'B':
            switch(message[1])
            {
                case 'L':
                    if(regexec(&regexCaptureHeadcodeFromBlock, message, 2, regexMatches, 0)
                        || !(headcodeLength = regexMatches[1].rm_eo - regexMatches[1].rm_so))
                    {
                        return NO_OPERATION;
                    }
                    free(*headcode);
                    *headcode = malloc((sizeof(char) * headcodeLength) + 1);
                    for(int i = 0; i < headcodeLength; i++)
                    {
                        (*headcode)[i] = message[i + regexMatches[1].rm_so];
                    }
                    (*headcode)[headcodeLength] = '\0';
                    return UPDATE_BLOCK;
                default:
                    return NO_OPERATION;
            }
        case 'T':
            switch(message[1])
            {
                case 'C':
                    if(message[length - 2] == 'O' && message[length - 1] == 'C')
                    {
                        return OCCUPY_TRACK_CIRCUIT;
                    }
                    else if(message[length - 2] == 'C' && message[length - 1] == 'L')
                    {
                        return CLEAR_TRACK_CIRCUIT;
                    }
                    else
                    {
                        return NO_OPERATION;
                    }

                default:
                    return NO_OPERATION;
            }

        default:
            return NO_OPERATION;
    }
}

void crust_window_process_input(char * inputBuffer, int connectionFp, CRUST_WINDOW_MODE mode)
{
    char headcode[CRUST_HEADCODE_LENGTH + 1];
    unsigned long berth;
    char writeBuffer[CRUST_MAX_MESSAGE_LENGTH];
    switch(mode)
    {
        case MANUAL_INTERPOSE:
            for(int i = 0; i < CRUST_HEADCODE_LENGTH; i++)
            {
                headcode[i] = inputBuffer[i + 4];
            }
            headcode[CRUST_HEADCODE_LENGTH] = '\0';
            inputBuffer[4] = '\0';
            berth = strtoul(inputBuffer, NULL, 10);
            snprintf(writeBuffer, CRUST_MAX_MESSAGE_LENGTH, "IP%li/%s\n", berth, headcode);
            write(connectionFp, writeBuffer, strlen(writeBuffer));
            break;

        case MANUAL_CLEAR:
            inputBuffer[4] = '\0';
            berth = strtoul(inputBuffer, NULL, 10);
            snprintf(writeBuffer, CRUST_MAX_MESSAGE_LENGTH, "IP%li/____\n", berth);
            write(connectionFp, writeBuffer, strlen(writeBuffer));
            break;
    }
}

void crust_window_refresh_screen(CRUST_WINDOW_MODE mode, char * inputBuffer, int inputPointer)
{
    bool flasher = time(NULL) % 2;

    clear();
    for(int i = 0; i < lineMapLength; i++)
    {
        move(lineMap[i].yPos, lineMap[i].xPos);

        if(mode != HOME && flasher && lineMap[i].berthNumber > -1)
        {
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            attron(A_BOLD);
            addch(lineMap[i].berthNumberCharacter);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
        }
        if(lineMap[i].berthNumber > -1 && (lineMap[i].showBerth || mode == MANUAL_INTERPOSE))
        {
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_HEADCODE));
            attron(A_BOLD);
            addch(lineMap[i].berthCharacter);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_HEADCODE));
        }
        else if(lineMap[i].trackCircuitNumber < 0)
        {
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_DEFAULT));
            addch(lineMap[i].character);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_DEFAULT));
        }
        else if(lineMap[i].occupied == true)
        {
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_OCCUPIED));
            attron(A_BOLD);
            addch(lineMap[i].character);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_OCCUPIED));
        }
        else
        {
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_CLEAR));
            attron(A_BOLD);
            addch(lineMap[i].character);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_CLEAR));
        }
    }

    move(LINES - 1, 0);

    switch(mode)
    {
        case HOME:
            addstr("Q: Quit, I: Manual Interpose, C: Manual Clear");
            move(LINES - 1, COLS - 1);
            break;

        case MANUAL_INTERPOSE:
            addstr("MANUAL INTERPOSE type ");
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            attron(A_BOLD);
            addstr("BERTH: ");
            addch(inputBuffer[0]);
            addch(inputBuffer[1]);
            addch(inputBuffer[2]);
            addch(inputBuffer[3]);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            addstr(" (enter) then ");
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_HEADCODE));
            attron(A_BOLD);
            addstr("HEADCODE: ");
            addch(inputBuffer[4]);
            addch(inputBuffer[5]);
            addch(inputBuffer[6]);
            addch(inputBuffer[7]);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_HEADCODE));
            addstr(" (enter). Esc to cancel.");
            if(inputPointer < 4)
            {
                move(LINES - 1, 29 + inputPointer);
            }
            else
            {
                move(LINES - 1, 53 + inputPointer);
            }

            break;

        case MANUAL_CLEAR:
            addstr("MANUAL CLEAR type ");
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            attron(A_BOLD);
            addstr("BERTH: ");
            addch(inputBuffer[0]);
            addch(inputBuffer[1]);
            addch(inputBuffer[2]);
            addch(inputBuffer[3]);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            addstr(" (enter). Esc to cancel.");
            move(LINES - 1, 25 + inputPointer);
            break;
    }



    refresh();
}

void crust_window_enter_mode(CRUST_WINDOW_MODE * currentMode, CRUST_WINDOW_MODE targetMode)
{
    switch(targetMode)
    {
        case LOG:

            break;

        case WELCOME:
            initscr();
            if(cbreak() != OK || noecho() != OK || nonl() != OK || nodelay(stdscr, TRUE) != OK)
            {
                crust_terminal_print("Failed to initialize screen.");
                exit(EXIT_FAILURE);
            }

            if(has_colors() == FALSE)
            {
                crust_terminal_print("Colour support is required.");
                exit(EXIT_FAILURE);
            }

            start_color();
            init_color(COLOR_WHITE, 1000, 1000, 1000);
            init_color(CRUST_COLOUR_GREY, 800, 800, 800);
            init_pair(CRUST_COLOUR_PAIR_DEFAULT, CRUST_COLOUR_GREY, COLOR_BLACK);
            init_pair(CRUST_COLOUR_PAIR_CLEAR, COLOR_WHITE, COLOR_BLACK);
            init_pair(CRUST_COLOUR_PAIR_OCCUPIED, COLOR_RED, COLOR_BLACK);
            init_pair(CRUST_COLOUR_PAIR_HEADCODE, COLOR_CYAN, COLOR_BLACK);
            init_pair(CRUST_COLOUR_PAIR_BERTH_NUMBER, COLOR_YELLOW, COLOR_BLACK);

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

            *currentMode = HOME;

            break;

        case HOME:
        case MANUAL_INTERPOSE:
        case MANUAL_CLEAR:
            *currentMode = targetMode;
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

_Noreturn void crust_window_loop(CRUST_STATE * state, struct pollfd * pollList, CRUST_WINDOW_MODE * mode)
{
    char readBuffer[CRUST_MAX_MESSAGE_LENGTH];
    char inputBuffer[10];
    memset(inputBuffer, '_', 9);
    inputBuffer[9] = '\0';
    int readPointer = 0;
    int inputPointer = 0;
    CRUST_IDENTIFIER remoteID = 0;
    char * headcode = NULL;
    for(;;)
    {
        // Poll the server and the keyboard
        poll(pollList, 2, 1000);

        // Handle data from the server
        if(pollList[1].revents & POLLHUP)
        {
            crust_window_print("Server disconnected...", *mode);
            sleep(5);
            crust_window_stop();
        }
        if(pollList[1].revents & POLLRDNORM)
        {
            // Try to read a character
            if(!read(pollList[1].fd, &readBuffer[readPointer], 1))
            {
                crust_window_print("Server disconnected...", *mode);
                sleep(5);
                crust_window_stop();
            }

            // Recognise the end of the message
            if(readBuffer[readPointer] == '\n')
            {
                readBuffer[readPointer] = '\0';
                switch(crust_window_interpret_message(readBuffer, &remoteID, &headcode))
                {
                    case OCCUPY_TRACK_CIRCUIT:
                        crust_window_set_occupation(remoteID, true);
                        break;

                    case CLEAR_TRACK_CIRCUIT:
                        crust_window_set_occupation(remoteID, false);
                        break;

                    case UPDATE_BLOCK:
                        crust_window_update_berth(remoteID, headcode);
                        break;

                    default:

                        break;
                }
                readPointer = 0;
            }
            else if(readPointer > CRUST_MAX_MESSAGE_LENGTH)
            {
                crust_window_print("Oversized message...", *mode);
                sleep(5);
                crust_window_stop();
            }
            else
            {
                readPointer++;
            }
        }

        char inputCharacter = getch();

        if(*mode != HOME)
        {
            if(inputCharacter == '\e')
            {
                inputPointer = 0;
                memset(inputBuffer, '_', CRUST_HEADCODE_LENGTH * 2);
                crust_window_enter_mode(mode, HOME);
            }
            else if(inputCharacter == '\x7f' && inputPointer)
            {
                inputPointer--;
                inputBuffer[inputPointer] = '_';
            }
            else if(inputCharacter == '\r' || inputCharacter == '\n')
            {
                if(inputPointer % 4)
                {
                    inputPointer = ((inputPointer / 4) + 1) * 4;
                }
                if(inputPointer == 8 || (inputPointer == 4 && *mode == MANUAL_CLEAR))
                {
                    crust_window_process_input(inputBuffer, pollList[1].fd, *mode);
                    memset(inputBuffer, '_', CRUST_HEADCODE_LENGTH * 2);
                    inputPointer = 0;
                    crust_window_enter_mode(mode, HOME);
                }
            }
            else if((inputCharacter >= 'A' && inputCharacter <= 'Z' && inputPointer > 3)
                || (inputCharacter >= '0' && inputCharacter <= '9'))
            {
                inputBuffer[inputPointer] = inputCharacter;
                inputPointer++;
            }
            else if(inputCharacter >= 'a' && inputCharacter <= 'z' && inputPointer > 3)
            {
                inputBuffer[inputPointer] = inputCharacter - 32;
                inputPointer++;
            }

            if(inputPointer > 8)
            {
                inputPointer = 8;
            }
            if(inputPointer > 4 && *mode == MANUAL_CLEAR)
            {
                inputPointer = 4;
            }
        }
        else
        {
            switch(inputCharacter)
            {
                case 'q':
                case 'Q':
                    crust_window_stop();
                    break;

                case 'c':
                case 'C':
                    crust_window_enter_mode(mode, MANUAL_CLEAR);
                    break;

                case 'i':
                case 'I':
                    crust_window_enter_mode(mode, MANUAL_INTERPOSE);
                    break;

                case '\e':
                    crust_window_enter_mode(mode, HOME);
                    break;
            }
        }
        // Handle data from the keyboard


        crust_window_refresh_screen(*mode, inputBuffer, inputPointer);
    }
}

_Noreturn void crust_window_run()
{
    struct pollfd pollList[2];

    // Compile regexes
    crust_window_compile_regexs();

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
    CRUST_WINDOW_MODE currentWindowMode = LOG;

    if(crustOptionWindowEnterLog)
    {
        windowStartingMode = LOG;
    }
    else
    {
        windowStartingMode = CRUST_WINDOW_DEFAULT_MODE;
    }

    crust_window_enter_mode(&currentWindowMode, windowStartingMode);

    crust_window_loop(state, pollList, &currentWindowMode);
}