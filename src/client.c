/******************************************************************************
 * Consolidated, Realtime Updates on Status of Trains (CRUST)
 * Copyright (C) 2022-2026 Michael R. Bell <michael@black-dragon.io>
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
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include "client.h"
#include "terminal.h"
#include "config.h"

int crust_client_connect()
{
    int yes = 1;
    int tcpKeepAliveInterval = CRUST_TCP_KEEPALIVE_INTERVAL;
    int tcpKeepAliveCount = CRUST_TCP_MAX_FAILED_KEEPALIVES;
    int socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if(socketFD == -1
        || setsockopt(socketFD, SOL_SOCKET, SO_KEEPALIVE, (void*)&yes, sizeof(yes))
        || setsockopt(socketFD, IPPROTO_TCP,
#ifdef MACOS
                TCP_KEEPALIVE,
#else
                TCP_KEEPIDLE,
#endif
                         (void*)&tcpKeepAliveInterval, sizeof(tcpKeepAliveInterval))
        || setsockopt(socketFD, IPPROTO_TCP, TCP_KEEPINTVL, (void*)&tcpKeepAliveInterval, sizeof(tcpKeepAliveInterval))
        || setsockopt(socketFD, IPPROTO_TCP, TCP_KEEPCNT, (void*)&tcpKeepAliveCount, sizeof(tcpKeepAliveCount)))
    {
        crust_terminal_print("Unable to create socket");
        exit(EXIT_FAILURE);
    }

    crust_terminal_print_verbose("Connecting to CRUST server...");
    struct sockaddr_in serverAddress;
    memset(&serverAddress, '\0', sizeof(struct sockaddr_in));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = crustOptionIPAddress;
    serverAddress.sin_port = htons(crustOptionPort);
    if(connect(socketFD, (struct sockaddr *)&serverAddress, sizeof(struct sockaddr_in)))
    {
        crust_terminal_print("Error connecting to CRUST server.");
        exit(EXIT_FAILURE);
    }
    crust_terminal_print_verbose("Connected to CRUST server.");

    return socketFD;
}