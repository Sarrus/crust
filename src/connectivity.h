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

#ifndef CRUST_CONNECTIVITY_H
#define CRUST_CONNECTIVITY_H

#include <stddef.h>
#include <poll.h>
#include <stdbool.h>
#ifdef GPIO
#include <gpiod.h>
#endif

enum crustConnectionType {
    CONNECTION_TYPE_UNDEFINED,
    CONNECTION_TYPE_READ_WRITE,
    CONNECTION_TYPE_SOCKET,
#ifdef GPIO
    CONNECTION_TYPE_GPIO_LINE,
#endif
    CONNECTION_TYPE_KEYBOARD
};

#define CRUST_CONNECTION struct crustConnection
struct crustConnection{
    enum crustConnectionType type;
    void (*readFunction)(CRUST_CONNECTION *);  // Called when data is ready to be read
    void (*openFunction)(CRUST_CONNECTION *);  // Called when a connection is received on a socket
    void (*closeFunction)(CRUST_CONNECTION *); // Called when the connection is closed
    char * readBuffer;
    size_t readTo; // Used by the receiving code to indicate how far it has read
    char * writeBuffer;
    bool didConnect;
    bool didClose;
    long long customIdentifier;
    CRUST_CONNECTION * parentSocket;
};

#define CRUST_CONNECTIVITY struct crustConnectivity
struct crustConnectivity {
    CRUST_CONNECTION ** connectionList;
    size_t connectionListLength;
    struct pollfd * pollList;
};

void crust_connection_write(CRUST_CONNECTION * connection, char * data);
void crust_connectivity_execute(int timeout);
CRUST_CONNECTION * crust_connection_read_write_open(void (*readFunction)(CRUST_CONNECTION *),
                                                    void (*openFunction)(CRUST_CONNECTION *),
                                                    void (*closeFunction)(CRUST_CONNECTION *),
                                                    in_addr_t address,
                                                    in_port_t port);

CRUST_CONNECTION * crust_connection_read_keyboard_open(void (*readFunction)(CRUST_CONNECTION *));
CRUST_CONNECTION * crust_connection_socket_open(void (*readFunction)(CRUST_CONNECTION *),
                                                void (*openFunction)(CRUST_CONNECTION *),
                                                void (*closeFunction)(CRUST_CONNECTION *),
                                                in_addr_t address,
                                                in_port_t port);

#ifdef GPIO
CRUST_CONNECTION * crust_connection_gpio_open(void (*readFunction)(CRUST_CONNECTION *), struct gpiod_line * gpioLine);
#endif

#endif //CRUST_CONNECTIVITY_H
