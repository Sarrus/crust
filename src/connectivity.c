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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "connectivity.h"
#include "terminal.h"
#include "config.h"

CRUST_CONNECTIVITY connectivity = {.pollList = NULL, .connectionListLength = 0, .connectionList = NULL};

void crust_connection_init(CRUST_CONNECTION * connection)
{
    connection->readFunction = NULL;
    connection->openFunction = NULL;
    connection->closeFunction = NULL;
    connection->readBuffer = NULL;
    connection->readTo = 0;
    connection->writeBuffer = NULL;
    connection->didConnect = false;
    connection->didClose = false;
    connection->customIdentifier = 0;
}

void crust_connectivity_extend()
{
    connectivity.connectionListLength++;

    connectivity.connectionList = realloc(connectivity.connectionList, sizeof(CRUST_CONNECTION *) * connectivity.connectionListLength);
    if(connectivity.connectionList == NULL)
    {
        crust_terminal_print("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    connectivity.connectionList[connectivity.connectionListLength - 1] = malloc(sizeof (CRUST_CONNECTION));
    if(connectivity.connectionList[connectivity.connectionListLength - 1] == NULL)
    {
        crust_terminal_print("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    crust_connection_init(connectivity.connectionList[connectivity.connectionListLength - 1]);

    connectivity.pollList = realloc(connectivity.pollList, sizeof(struct pollfd) * connectivity.connectionListLength);
    if(connectivity.pollList == NULL)
    {
        crust_terminal_print("Memory allocation error");
        exit(EXIT_FAILURE);
    }
    connectivity.pollList[connectivity.connectionListLength - 1].fd = 0;
    connectivity.pollList[connectivity.connectionListLength - 1].events = 0;
    connectivity.pollList[connectivity.connectionListLength - 1].revents = 0;
}

CRUST_CONNECTION * crust_connection_read_keyboard_open(void (*readFunction)(CRUST_CONNECTION *))
{
    crust_connectivity_extend();
    CRUST_CONNECTION * connection = connectivity.connectionList[connectivity.connectionListLength - 1];
    struct pollfd * pollListEntry = &connectivity.pollList[connectivity.connectionListLength - 1];

    connection->type = CONNECTION_TYPE_KEYBOARD;
    connection->readFunction = readFunction;
    connection->didConnect = true;
    pollListEntry->fd = STDIN_FILENO;
    pollListEntry->events = POLLRDNORM;

    return connection;
}

CRUST_CONNECTION * crust_connection_read_write_open(void (*readFunction)(CRUST_CONNECTION *),
                                                    void (*openFunction)(CRUST_CONNECTION *),
                                                    void (*closeFunction)(CRUST_CONNECTION *),
                                                    in_addr_t address,
                                                    in_port_t port)
{
    // Prepare memory to hold the connection
    crust_connectivity_extend();
    CRUST_CONNECTION * connection = connectivity.connectionList[connectivity.connectionListLength - 1];
    struct pollfd * pollListEntry = &connectivity.pollList[connectivity.connectionListLength - 1];
    connection->type = CONNECTION_TYPE_READ_WRITE;
    connection->readFunction = readFunction;
    connection->openFunction = openFunction;
    connection->closeFunction = closeFunction;

    // Set up the socket and enable keep-alives
    int yes = 1;
    int tcpKeepAliveInterval = CRUST_TCP_KEEPALIVE_INTERVAL;
    int tcpKeepAliveCount = CRUST_TCP_MAX_FAILED_KEEPALIVES;
    pollListEntry->fd = socket(AF_INET, SOCK_STREAM, 0);
    if(pollListEntry->fd == -1
       || setsockopt(pollListEntry->fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&yes, sizeof(yes))
       || setsockopt(pollListEntry->fd, IPPROTO_TCP,
#ifdef MACOS
                     TCP_KEEPALIVE,
#else
                     TCP_KEEPIDLE,
#endif
                     (void*)&tcpKeepAliveInterval, sizeof(tcpKeepAliveInterval))
       || setsockopt(pollListEntry->fd, IPPROTO_TCP, TCP_KEEPINTVL, (void*)&tcpKeepAliveInterval, sizeof(tcpKeepAliveInterval))
       || setsockopt(pollListEntry->fd, IPPROTO_TCP, TCP_KEEPCNT, (void*)&tcpKeepAliveCount, sizeof(tcpKeepAliveCount)))
    {
        crust_terminal_print("Unable to create socket");
        exit(EXIT_FAILURE);
    }

    // Make it non-blocking
    int flags = fcntl(pollListEntry->fd, F_GETFL);
    if(flags == -1)
    {
        crust_terminal_print("Unable to create socket");
        exit(EXIT_FAILURE);
    }
    flags |= O_NONBLOCK;
    if(fcntl(pollListEntry->fd, F_SETFL, flags))
    {
        crust_terminal_print("Unable to create socket");
        exit(EXIT_FAILURE);
    }

    // Start establishing the connection
    struct sockaddr_in serverAddress;
    memset(&serverAddress, '\0', sizeof(struct sockaddr_in));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = address;
    serverAddress.sin_port = htons(port);
    if(connect(pollListEntry->fd, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in))
            && errno != EINPROGRESS)
    {
        crust_terminal_print("Error connecting to CRUST server.");
        exit(EXIT_FAILURE);
    }

    pollListEntry->events = POLLHUP | POLLWRNORM;

    return connection;
}

#ifdef GPIO
CRUST_CONNECTION * crust_connection_gpio_open(void (*readFunction)(CRUST_CONNECTION *), struct gpiod_line * gpioLine)
{
    crust_connectivity_extend();
    CRUST_CONNECTION * connection = connectivity.connectionList[connectivity.connectionListLength - 1];
    struct pollfd * pollListEntry = &connectivity.pollList[connectivity.connectionListLength - 1];

    connection->type = CONNECTION_TYPE_GPIO_LINE;
    connection->readFunction = readFunction;
    pollListEntry->fd = gpiod_line_event_get_fd(gpioLine);
    if(pollListEntry->fd < 0)
    {
        crust_terminal_print("Failed to obtain file descriptor for a GPIO line");
        exit(EXIT_FAILURE);
    }
    pollListEntry->events = POLLRDNORM;

    return connection;
}
#endif

void crust_connection_write(CRUST_CONNECTION * connection, char * data)
{
    size_t existingDataSize = 0;
    size_t newDataSize = strlen(data);
    if(connection->writeBuffer != NULL)
    {
        existingDataSize = strlen(connection->writeBuffer);
    }

    connection->writeBuffer = realloc(connection->writeBuffer, existingDataSize + newDataSize + 1);
    connection->writeBuffer[existingDataSize] = '\0'; // Make sure the write buffer is null terminated
    strncat(connection->writeBuffer, data, newDataSize + 1);
}

void crust_connectivity_execute(int timeout)
{
    char localReadBuffer[CRUST_MAX_MESSAGE_LENGTH] = "";

    for(int i = 0; i < connectivity.connectionListLength; i++)
    {
        // Find connections with writes waiting
        if(connectivity.connectionList[i]->didConnect
                && !connectivity.connectionList[i]->didClose
                && connectivity.connectionList[i]->writeBuffer != NULL)
        {
            // Start write polling
            connectivity.pollList[i].events |= POLLWRNORM;
        }
    }

    poll(connectivity.pollList, connectivity.connectionListLength, timeout);

    for(int i = 0; i < connectivity.connectionListLength; i++)
    {
        // Handle hangups
        if(connectivity.pollList[i].revents & POLLHUP)
        {
            connectivity.connectionList[i]->didClose = true;
            if(connectivity.connectionList[i]->closeFunction != NULL)
            {
                connectivity.connectionList[i]->closeFunction(connectivity.connectionList[i]);
            }
            connectivity.pollList[i].revents = 0; // Ignore any other events if we had a hangup
            connectivity.pollList[i].events = 0; // Stop polling
        }

        // Handle new outbound connections opening
        if(connectivity.pollList[i].revents & POLLWRNORM && connectivity.connectionList[i]->didConnect == false)
        {
            connectivity.connectionList[i]->didConnect = true;
            if(connectivity.connectionList[i]->openFunction != NULL)
            {
                connectivity.connectionList[i]->openFunction(connectivity.connectionList[i]);
            }
            connectivity.pollList[i].events &= ~POLLWRNORM; // Stop write polling
            connectivity.pollList[i].revents &= ~POLLWRNORM; // Clear the write flag
            connectivity.pollList[i].events |= POLLRDNORM; // Start read polling
        }

        // Handle reads
        if(connectivity.pollList[i].revents & POLLRDNORM)
        {
            if(connectivity.connectionList[i]->type == CONNECTION_TYPE_KEYBOARD)
            {
                // Run the read function to show that keyboard data is available (don't actually read it, let ncurses do that)
                connectivity.connectionList[i]->readFunction(connectivity.connectionList[i]);
            }
#ifdef GPIO
            else if(connectivity.connectionList[i]->type == CONNECTION_TYPE_GPIO_LINE)
            {
                connectivity.connectionList[i]->readFunction(connectivity.connectionList[i]);
            }
#endif
            else
            {
                size_t bytesRead = read(connectivity.pollList[i].fd, localReadBuffer, CRUST_MAX_MESSAGE_LENGTH - 1);
                if(bytesRead == 0) //The connection is closing
                {
                    shutdown(connectivity.pollList[i].fd, SHUT_RDWR);
                }
                else
                {
                    localReadBuffer[bytesRead] = '\0';
                    size_t connectivityReadBufferLength;
                    if(connectivity.connectionList[i]->readBuffer == NULL)
                    {
                        connectivityReadBufferLength = 0;
                    }
                    else
                    {
                        connectivityReadBufferLength = strlen(connectivity.connectionList[i]->readBuffer);
                    }

                    size_t newConnectivityReadBufferLength = connectivityReadBufferLength + bytesRead + 1;

                    connectivity.connectionList[i]->readBuffer = realloc(
                            connectivity.connectionList[i]->readBuffer,
                            newConnectivityReadBufferLength);

                    strncat(connectivity.connectionList[i]->readBuffer, localReadBuffer, bytesRead + 1);

                    // Tell the program that there is data to read
                    connectivity.connectionList[i]->readFunction(connectivity.connectionList[i]);

                    // Calculate how much has been left in the read buffer
                    size_t bytesLeft = strlen(&connectivity.connectionList[i]->readBuffer[connectivity.connectionList[i]->readTo]);
                    if(!bytesLeft)
                    {
                        free(connectivity.connectionList[i]->readBuffer);
                        connectivity.connectionList[i]->readBuffer = NULL;
                    }
                    else
                    {
                        char * newReadBuffer = malloc(bytesLeft + 1);
                        strncpy(newReadBuffer, &connectivity.connectionList[i]->readBuffer[connectivity.connectionList[i]->readTo], bytesLeft + 1);
                        free(connectivity.connectionList[i]->readBuffer);
                        connectivity.connectionList[i]->readBuffer = newReadBuffer;
                        connectivity.connectionList[i]->readTo = 0;
                    }
                }
            }
        }

        // Handle writes
        if(connectivity.pollList[i].revents & POLLWRNORM)
        {
            size_t bytesToWrite = strlen(connectivity.connectionList[i]->writeBuffer);
            size_t bytesWritten = write(connectivity.pollList[i].fd,
                                        connectivity.connectionList[i]->writeBuffer,
                                        bytesToWrite);
            if(bytesToWrite == bytesWritten)
            {
                free(connectivity.connectionList[i]->writeBuffer);
                connectivity.connectionList[i]->writeBuffer = NULL;
                connectivity.pollList[i].events &= ~POLLWRNORM; // Stop write polling
            }
            else
            {
                size_t bytesLeft = bytesToWrite - bytesWritten;
                char * newWriteBuffer = malloc(bytesLeft + 1);
                strncpy(newWriteBuffer, &connectivity.connectionList[i]->writeBuffer[bytesWritten], bytesLeft + 1);
                free(connectivity.connectionList[i]->writeBuffer);
                connectivity.connectionList[i]->writeBuffer = newWriteBuffer;
            }
        }
    }
}