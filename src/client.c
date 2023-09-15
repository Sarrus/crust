//
// Created by Michael Bell on 14/09/2023.
//

#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include "client.h"
#include "terminal.h"
#include "options.h"

int crust_client_connect()
{
    int socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if(socketFD == -1)
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