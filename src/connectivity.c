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
    connection->writeBuffer = NULL;
    connection->didConnect = false;
    connection->didClose = false;
}

void crust_connection_destroy(CRUST_CONNECTION * connection)
{
    free(connection->readBuffer);
    free(connection->writeBuffer);
    free(connection);
}

void crust_connectivity_extend()
{
    connectivity.connectionListLength++;

    connectivity.connectionList = realloc(connectivity.connectionList, sizeof(CRUST_CONNECTION) * connectivity.connectionListLength);
    if(connectivity.connectionList == NULL)
    {
        crust_terminal_print("Memory allocation error");
        exit(EXIT_FAILURE);
    }

    connectivity.pollList = realloc(connectivity.pollList, sizeof(struct pollfd) * connectivity.connectionListLength);
    if(connectivity.pollList == NULL)
    {
        crust_terminal_print("Memory allocation error");
        exit(EXIT_FAILURE);
    }
}

CRUST_CONNECTION * crust_connection_read_keyboard(void (*readFunction)(CRUST_CONNECTION *))
{
    crust_connectivity_extend();
    CRUST_CONNECTION * connection = &connectivity.connectionList[connectivity.connectionListLength - 1];
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
    CRUST_CONNECTION * connection = &connectivity.connectionList[connectivity.connectionListLength - 1];
    struct pollfd * pollListEntry = &connectivity.pollList[connectivity.connectionListLength - 1];
    crust_connection_init(connection);
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
       || setsockopt(pollListEntry->fd, IPPROTO_TCP, TCP_KEEPALIVE, (void*)&tcpKeepAliveInterval, sizeof(tcpKeepAliveInterval))
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

void crust_connectivity_execute(int timeout)
{
    poll(connectivity.pollList, connectivity.connectionListLength, timeout);

    for(int i = 0; i < connectivity.connectionListLength; i++)
    {
        if(connectivity.pollList[i].revents & POLLHUP)
        {
            connectivity.connectionList[i].didClose = true;
            if(connectivity.connectionList[i].closeFunction != NULL)
            {
                connectivity.connectionList[i].closeFunction(&connectivity.connectionList[i]);
            }
            connectivity.pollList[i].revents = 0; // Ignore any other events if we had a hangup
            connectivity.pollList[i].events = 0; // Stop polling
        }

        if(connectivity.pollList[i].revents & POLLWRNORM && connectivity.connectionList[i].didConnect == false)
        {
            connectivity.connectionList[i].didConnect = true;
            if(connectivity.connectionList[i].openFunction != NULL)
            {
                connectivity.connectionList[i].openFunction(&connectivity.connectionList[i]);
            }
            connectivity.pollList[i].events &= ~POLLWRNORM; // Stop write polling
            connectivity.pollList[i].events |= POLLRDNORM; // Start read polling
        }

        if(connectivity.pollList[i].revents & POLLRDNORM)
        {
            if(read(connectivity.pollList[i].fd, NULL, 0) == 0)
            {
                shutdown(connectivity.pollList[i].fd, SHUT_RDWR);
            }
        }
    }
}