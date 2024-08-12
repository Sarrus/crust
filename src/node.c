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

#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <gpiod.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include "node.h"
#include "terminal.h"
#include "config.h"
#include "connectivity.h"
#ifdef SYSTEMD
#include <systemd/sd-daemon.h>
#endif

/*
 * This is the time that a GPIO line has to stay in it's state before the appropriate track circuit is updated. This allows
 * for some flickering of the circuit when it changes state and ignores brief spikes / troughs in voltage. Max 1000
 * */
#define CRUST_NODE_SETTLE_TIME 100 //ms
#define CRUST_NODE_RECONNECTION_WAIT_TIME 10 //s

#define GPIO_CHIP struct gpiod_chip

#define CRUST_GPIO_PIN_MAP struct crustGPIOPinMap
struct crustGPIOPinMap {
    unsigned int pinID;
    CRUST_IDENTIFIER trackCircuitID;
    struct gpiod_line * gpioLine;
    bool lastOccupationRead; // The last occupation state read on the line (true = occupied, false = clear)
    bool lastOccupationSent; // The last occupation state sent to the server
    struct timespec lastReadAt; // The time the last read occurred
    CRUST_CONNECTION * connection;
};

GPIO_CHIP * gpioChip;
CRUST_CONNECTION * nodeServerConnection;
int pinMapLength = 0;
CRUST_GPIO_PIN_MAP * pinMap = NULL;
int circuitOccupiedEvent = GPIOD_LINE_EVENT_RISING_EDGE;
int circuitClearEvent = GPIOD_LINE_EVENT_FALLING_EDGE;

void crust_generate_pin_map(char * mapText)
{
    char * next, * current, * subNext;
    next = mapText;
    while((current = subNext = strsep(&next, ",")) != NULL)
    {
        if((current = strsep(&subNext, ":")) == NULL
           || *current == '\0'
           || subNext == NULL
           || *subNext == '\0')
        {
            crust_terminal_print("Invalid track circuit GPIO map");
            exit(EXIT_FAILURE);
        }

        errno = 0;
        unsigned long pinNumber = strtoul(current, NULL, 10);
        if(errno
           || pinNumber > UINT_MAX)
        {
            crust_terminal_print("Invalid track circuit GPIO map");
            exit(EXIT_FAILURE);
        }
        unsigned long trackCircuitNumber = strtoul(subNext, NULL, 10);
        if(errno
           || trackCircuitNumber > UINT_MAX)
        {
            crust_terminal_print("Invalid track circuit GPIO map");
            exit(EXIT_FAILURE);
        }

        pinMapLength++;

        pinMap = realloc(pinMap, (sizeof(CRUST_GPIO_PIN_MAP) * pinMapLength));
        pinMap[pinMapLength - 1].pinID = pinNumber;
        pinMap[pinMapLength - 1].trackCircuitID = trackCircuitNumber;
        pinMap[pinMapLength - 1].gpioLine = NULL;
        pinMap[pinMapLength - 1].lastOccupationRead = false;
        pinMap[pinMapLength - 1].lastOccupationSent = false;
        pinMap[pinMapLength - 1].connection = NULL;
        pinMap[pinMapLength - 1].lastReadAt.tv_nsec = 0;
        pinMap[pinMapLength - 1].lastReadAt.tv_sec = 0;
    }
}

_Noreturn void crust_node_stop()
{
#ifdef SYSTEMD
    sd_notify(0, "STOPPING=1\n"
                 "STATUS=CRUST Node shutting down...");
#endif
    crust_terminal_print_verbose("Closing the GPIO connection...");
    gpiod_chip_close(gpioChip);

    exit(EXIT_SUCCESS);
}

void crust_node_receive_read(CRUST_CONNECTION * connection)
{
    struct gpiod_line_event event;

    if(connection != nodeServerConnection)
    {
        CRUST_GPIO_PIN_MAP * pin = &pinMap[connection->customIdentifier];
        gpiod_line_event_read(pin->gpioLine, &event);
        if(event.event_type == circuitOccupiedEvent)
        {
            pin->lastOccupationRead = true;
        }
        else if(event.event_type == circuitClearEvent)
        {
            pin->lastOccupationRead = false;
        }
        clock_gettime(CLOCK_MONOTONIC, &pin->lastReadAt);
    }
}

void crust_node_receive_open(CRUST_CONNECTION * connection)
{
    if(connection == nodeServerConnection)
    {
        crust_terminal_print_verbose("Connected.");

        // Trigger a resend of all circuits
        for(int i = 0; i < pinMapLength; i++)
        {
            int pinValue = gpiod_line_get_value(pinMap[i].gpioLine);
            pinMap[i].lastOccupationRead = pinValue ^ crustOptionInvertPinLogic;
            pinMap[i].lastOccupationSent = !pinValue ^ crustOptionInvertPinLogic;
            pinMap[i].lastReadAt.tv_sec = 0;
            pinMap[i].lastReadAt.tv_nsec = 0;
        }
    }
}

void crust_node_receive_close(CRUST_CONNECTION * connection)
{
    if(connection != nodeServerConnection)
    {
        return;
    }
    crust_terminal_print_verbose("Connection lost / failed.");
    sleep(CRUST_NODE_RECONNECTION_WAIT_TIME);
    crust_terminal_print_verbose("Reconnecting...");
    nodeServerConnection = crust_connection_read_write_open(crust_node_receive_read,
                                                  crust_node_receive_open,
                                                  crust_node_receive_close,
                                                  crustOptionIPAddress,
                                                  crustOptionPort);
}

void crust_node_handle_signal(int signal)
{
    switch(signal)
    {
        case SIGINT:
            crust_terminal_print_verbose("Received SIGINT, shutting down...");
            crust_node_stop();
        case SIGTERM:
            crust_terminal_print_verbose("Received SIGTERM, shutting down...");
            crust_node_stop();
        default:
            crust_terminal_print("Received an unexpected signal, exiting.");
            exit(EXIT_FAILURE);
    }
}

_Noreturn void crust_node_loop()
{
    struct timespec now;
    char messageBuffer[CRUST_MAX_MESSAGE_LENGTH];

    for(;;)
    {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long connectivityExecuteTimeout = 0;

        if(nodeServerConnection->didConnect)
        {
            for (int i = 1; i < pinMapLength; i++)
            {
                if (pinMap[i].lastOccupationRead != pinMap[i].lastOccupationSent)
                {
                    // Calculate the time since the last read
                    struct timespec difference;
                    difference.tv_sec = now.tv_sec - pinMap[i].lastReadAt.tv_sec;
                    difference.tv_nsec = now.tv_nsec - pinMap[i].lastReadAt.tv_nsec;
                    if (difference.tv_nsec < 0)
                    {
                        difference.tv_nsec += (long) 1e9;
                        difference.tv_sec--;
                    }

                    if (difference.tv_sec || (difference.tv_nsec > (CRUST_NODE_SETTLE_TIME * 1000)))
                    {
                        if (pinMap[i].lastOccupationRead)
                        {
                            sprintf(messageBuffer, "OC%i\n", pinMap[i].trackCircuitID);
                        }
                        else
                        {
                            sprintf(messageBuffer, "CC%i\n", pinMap[i].trackCircuitID);
                        }
                        crust_connection_write(nodeServerConnection, messageBuffer);
                        pinMap[i].lastOccupationSent = pinMap[i].lastOccupationRead;
                    }
                    else if (!difference.tv_sec)
                    {
                        long differenceInMsec = difference.tv_nsec / 1000;
                        if (!connectivityExecuteTimeout || differenceInMsec < connectivityExecuteTimeout)
                        {
                            connectivityExecuteTimeout = differenceInMsec;
                        }
                    }
                }
            }
        }

        crust_connectivity_execute(connectivityExecuteTimeout);
    }
}

_Noreturn void crust_node_run()
{
    crust_terminal_print_verbose("CRUST node starting...");

    crust_terminal_print_verbose("Binding to GPIO chip...");

    gpioChip = NULL;
    gpioChip = gpiod_chip_open(crustOptionGPIOPath);

    if(gpioChip == NULL)
    {
        crust_terminal_print("Unable to open GPIO chip");
        exit(EXIT_FAILURE);
    }

    // Registering signal handlers
    signal(SIGINT, crust_node_handle_signal);
    signal(SIGTERM, crust_node_handle_signal);

    if(crustOptionSetGroup)
    {
        crust_terminal_print_verbose("Attempting to set process GID...");
        if(setgid(crustOptionTargetGroup))
        {
            crust_terminal_print("Unable to set process GID, continuing with default");
        }
    }

    if(crustOptionSetUser)
    {
        crust_terminal_print_verbose("Changing the owner of the GPIO device...");
        if(chown(crustOptionGPIOPath, crustOptionTargetUser, 0))
        {
            crust_terminal_print("Failed to change the owner of the GPIO device");
            exit(EXIT_FAILURE);
        }

        crust_terminal_print_verbose("Setting process UID...");
        if(setuid(crustOptionTargetUser))
        {
            crust_terminal_print("Unable to set process UID");
            exit(EXIT_FAILURE);
        }
    }

    crust_terminal_print_verbose("Changing the permission bits on the GPIO device...");
    if(chmod(crustOptionGPIOPath, S_IRUSR | S_IWUSR))
    {
        crust_terminal_print("Unable to set the permission bits on the GPIO device, continuing");
    }

    crust_generate_pin_map(crustOptionPinMapString);

    // If the user has requested inverted pin logic, reverse the event types
    if(crustOptionInvertPinLogic)
    {
        circuitOccupiedEvent = GPIOD_LINE_EVENT_FALLING_EDGE;
        circuitClearEvent = GPIOD_LINE_EVENT_RISING_EDGE;
    }

    for(int i = 0; i < pinMapLength; i++)
    {
        pinMap[i].gpioLine = gpiod_chip_get_line(gpioChip, pinMap[i].pinID);
        if(pinMap[i].gpioLine == NULL)
        {
            crust_terminal_print("Failed to open a GPIO line");
            exit(EXIT_FAILURE);
        }

        if(gpiod_line_request_both_edges_events(pinMap[i].gpioLine, NULL))
        {
            crust_terminal_print("Failed to register for events on a GPIO line");
            exit(EXIT_FAILURE);
        }

        // Set to immediately trigger a state update when the loop starts
        pinMap[i].lastReadAt.tv_nsec = 0;
        pinMap[i].lastReadAt.tv_sec = 0;
        if(crustOptionInvertPinLogic)
        {
            pinMap[i].lastOccupationRead = !gpiod_line_get_value(pinMap[i].gpioLine);
        }
        else
        {
            pinMap[i].lastOccupationRead = gpiod_line_get_value(pinMap[i].gpioLine);
        }
        pinMap[i].lastOccupationSent = !pinMap[i].lastOccupationRead;

        pinMap[i].connection = crust_connection_gpio_open(crust_node_receive_read, pinMap[i].gpioLine);

        pinMap[i].connection->customIdentifier = i;
    }

    nodeServerConnection = crust_connection_read_write_open(crust_node_receive_read,
                                                        crust_node_receive_open,
                                                        crust_node_receive_close,
                                                        crustOptionIPAddress,
                                                        crustOptionPort);

#ifdef SYSTEMD
    sd_notify(0, "READY=1\n"
                 "STATUS=CRUST Node running");
#endif

    crust_node_loop();
}