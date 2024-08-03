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
#include "connectivity.h"

enum crustWindowMode {
    LOG,
    WELCOME,
    HOME,
    CONNECTING,
    DISCONNECTED,
    MANUAL_INTERPOSE,
    MANUAL_CLEAR
};

#define CRUST_WINDOW_MODE enum crustWindowMode
#define CRUST_WINDOW_DEFAULT_MODE WELCOME

CRUST_WINDOW_MODE currentWindowMode = LOG;

int reconnectWaitTimer = -1;
#define RECONNECTION_WAIT_TIME 10

int keyboardInputPointer = 0;
static char keyboardInputBuffer[10] = "________\0\0";

CRUST_CONNECTION * serverConnection = NULL;

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

void crust_window_process_input(char * receivedInputBuffer)
{
    char headcode[CRUST_HEADCODE_LENGTH + 1];
    unsigned long berth;
    char writeBuffer[CRUST_MAX_MESSAGE_LENGTH];

    if(serverConnection == NULL)
    {
        return;
    }

    switch(currentWindowMode)
    {
        case MANUAL_INTERPOSE:
            for(int i = 0; i < CRUST_HEADCODE_LENGTH; i++)
            {
                headcode[i] = receivedInputBuffer[i + 4];
            }
            headcode[CRUST_HEADCODE_LENGTH] = '\0';
            receivedInputBuffer[4] = '\0';
            berth = strtoul(receivedInputBuffer, NULL, 10);
            snprintf(writeBuffer, CRUST_MAX_MESSAGE_LENGTH, "IP%li/%s\n", berth, headcode);
            crust_connection_write(serverConnection, writeBuffer);
            break;

        case MANUAL_CLEAR:
            receivedInputBuffer[4] = '\0';
            berth = strtoul(receivedInputBuffer, NULL, 10);
            snprintf(writeBuffer, CRUST_MAX_MESSAGE_LENGTH, "IP%li/____\n", berth);
            crust_connection_write(serverConnection, writeBuffer);
            break;
    }
}

void crust_window_refresh_screen()
{
    bool flasher = time(NULL) % 2;

    clear();
    for(int i = 0; i < lineMapLength; i++)
    {
        move(lineMap[i].yPos, lineMap[i].xPos);

        if(currentWindowMode != HOME && flasher && lineMap[i].berthNumber > -1)
        {
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            attron(A_BOLD);
            addch(lineMap[i].berthNumberCharacter);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
        }
        if(lineMap[i].berthNumber > -1 && (lineMap[i].showBerth || currentWindowMode == MANUAL_INTERPOSE))
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

    switch(currentWindowMode)
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
            addch(keyboardInputBuffer[0]);
            addch(keyboardInputBuffer[1]);
            addch(keyboardInputBuffer[2]);
            addch(keyboardInputBuffer[3]);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            addstr(" (enter) then ");
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_HEADCODE));
            attron(A_BOLD);
            addstr("HEADCODE: ");
            addch(keyboardInputBuffer[4]);
            addch(keyboardInputBuffer[5]);
            addch(keyboardInputBuffer[6]);
            addch(keyboardInputBuffer[7]);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_HEADCODE));
            addstr(" (enter). Esc to cancel.");
            if(keyboardInputPointer < 4)
            {
                move(LINES - 1, 29 + keyboardInputPointer);
            }
            else
            {
                move(LINES - 1, 53 + keyboardInputPointer);
            }

            break;

        case MANUAL_CLEAR:
            addstr("MANUAL CLEAR type ");
            attron(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            attron(A_BOLD);
            addstr("BERTH: ");
            addch(keyboardInputBuffer[0]);
            addch(keyboardInputBuffer[1]);
            addch(keyboardInputBuffer[2]);
            addch(keyboardInputBuffer[3]);
            attroff(A_BOLD);
            attroff(COLOR_PAIR(CRUST_COLOUR_PAIR_BERTH_NUMBER));
            addstr(" (enter). Esc to cancel.");
            move(LINES - 1, 25 + keyboardInputPointer);
            break;

        case CONNECTING:
            clear();
            move(LINES / 2, 0);
            addstr("Establishing connection to the server...");
            refresh();
            break;

        case DISCONNECTED:
            clear();
            move(LINES / 2, 0);
            addstr("Server disconnected, attempting to reconnect...");
            refresh();
            break;
    }



    refresh();
}

void crust_window_enter_mode(CRUST_WINDOW_MODE targetMode)
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
            init_pair(CRUST_COLOUR_PAIR_DEFAULT, COLOR_WHITE, COLOR_BLACK);
            init_color(CRUST_COLOUR_GREY, 800, 800, 800);
            init_pair(CRUST_COLOUR_PAIR_DEFAULT, CRUST_COLOUR_GREY, COLOR_BLACK); //Overset above for terminals that support it
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

            currentWindowMode = CONNECTING;

            break;

        case DISCONNECTED:
        case HOME:
        case MANUAL_INTERPOSE:
        case MANUAL_CLEAR:
            currentWindowMode = targetMode;
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

void crust_window_handle_keyboard()
{
    char inputCharacter = getch();

    if(currentWindowMode == MANUAL_INTERPOSE || currentWindowMode == MANUAL_CLEAR)
    {
        if(inputCharacter == '\e')
        {
            keyboardInputPointer = 0;
            memset(keyboardInputBuffer, '_', CRUST_HEADCODE_LENGTH * 2);
            crust_window_enter_mode(HOME);
        }
        else if(inputCharacter == '\x7f' && keyboardInputPointer)
        {
            keyboardInputPointer--;
            keyboardInputBuffer[keyboardInputPointer] = '_';
        }
        else if(inputCharacter == '\r' || inputCharacter == '\n')
        {
            if(keyboardInputPointer % 4)
            {
                keyboardInputPointer = ((keyboardInputPointer / 4) + 1) * 4;
            }
            if(keyboardInputPointer == 8 || (keyboardInputPointer == 4 && currentWindowMode == MANUAL_CLEAR))
            {
                crust_window_process_input(keyboardInputBuffer);
                memset(keyboardInputBuffer, '_', CRUST_HEADCODE_LENGTH * 2);
                keyboardInputPointer = 0;
                crust_window_enter_mode(HOME);
            }
        }
        else if((inputCharacter >= 'A' && inputCharacter <= 'Z' && keyboardInputPointer > 3)
            || (inputCharacter >= '0' && inputCharacter <= '9'))
        {
            keyboardInputBuffer[keyboardInputPointer] = inputCharacter;
            keyboardInputPointer++;
        }
        else if(inputCharacter >= 'a' && inputCharacter <= 'z' && keyboardInputPointer > 3)
        {
            keyboardInputBuffer[keyboardInputPointer] = inputCharacter - 32;
            keyboardInputPointer++;
        }

        if(keyboardInputPointer > 8)
        {
            keyboardInputPointer = 8;
        }
        if(keyboardInputPointer > 4 && currentWindowMode == MANUAL_CLEAR)
        {
            keyboardInputPointer = 4;
        }
    }
    else if(currentWindowMode == HOME)
    {
        switch(inputCharacter)
        {
            case 'q':
            case 'Q':
                crust_window_stop();
                break;

            case 'c':
            case 'C':
                crust_window_enter_mode(MANUAL_CLEAR);
                break;

            case 'i':
            case 'I':
                crust_window_enter_mode(MANUAL_INTERPOSE);
                break;

            case '\e':
                crust_window_enter_mode(HOME);
                break;
        }
    }
    // Handle data from the keyboard

}

void crust_window_handle_update(CRUST_CONNECTION * connection)
{
    size_t readBufferLength = strlen(connection->readBuffer);
    size_t commandStart = 0;
    CRUST_IDENTIFIER remoteID;
    char * headcode = NULL;
    for(int i = 0; i < readBufferLength; i++)
    {
        if(connection->readBuffer[i] == '\n')
        {
            connection->readBuffer[i] = '\0';
            switch(crust_window_interpret_message(&connection->readBuffer[commandStart], &remoteID, &headcode))
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

            connection->readBuffer[i] = '\n';
            commandStart = i + 1;
        }
    }
    connection->readTo = commandStart;
}

void crust_window_receive_read(CRUST_CONNECTION * connection)
{
    if(connection->type == CONNECTION_TYPE_KEYBOARD)
    {
        crust_window_handle_keyboard();
    }
    else if(connection->type == CONNECTION_TYPE_READ_WRITE)
    {
        crust_window_handle_update(connection);
    }
}

void crust_window_receive_open(CRUST_CONNECTION * connection)
{
    crust_connection_write(connection, "SL\n");
    serverConnection = connection;
    crust_window_enter_mode(HOME);
}

void crust_window_receive_close(CRUST_CONNECTION * connection)
{
    serverConnection = NULL;

    if(connection->didConnect)
    {
        crust_connection_read_write_open(crust_window_receive_read,
                                         crust_window_receive_open,
                                         crust_window_receive_close,
                                         crustOptionIPAddress,
                                         crustOptionPort);
        crust_window_enter_mode(DISCONNECTED);
    }
    else
    {
        reconnectWaitTimer = RECONNECTION_WAIT_TIME;
    }
}

_Noreturn void crust_window_loop()
{
    for(;;)
    {
        crust_connectivity_execute(1000);

        if(reconnectWaitTimer > 0)
        {
            reconnectWaitTimer--;
        }
        else if(reconnectWaitTimer == 0)
        {
            crust_connection_read_write_open(crust_window_receive_read,
                                             crust_window_receive_open,
                                             crust_window_receive_close,
                                             crustOptionIPAddress,
                                             crustOptionPort);
            reconnectWaitTimer = -1;
        }


        crust_window_refresh_screen();
    }
}

_Noreturn void crust_window_run()
{
    // Compile regexes
    crust_window_compile_regexs();

    // Register the signal handlers
    signal(SIGINT, crust_window_handle_signal);
    signal(SIGTERM, crust_window_handle_signal);

    // Load the layout file
    crust_window_load_layout();

    crust_connection_read_write_open(crust_window_receive_read,
                                     crust_window_receive_open,
                                     crust_window_receive_close,
                                     crustOptionIPAddress,
                                     crustOptionPort);
    crust_connection_read_keyboard_open(crust_window_receive_read);

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

    crust_window_loop();
}