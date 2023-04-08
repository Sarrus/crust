#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <gpiod.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include "node.h"
#include "terminal.h"
#include "options.h"

#define GPIO_CHIP struct gpiod_chip

#define CRUST_GPIO_PIN_MAP struct crustGPIOPinMap
struct crustGPIOPinMap {
    unsigned int pinID;
    CRUST_IDENTIFIER trackCircuitID;
    struct gpiod_line * gpioLine;
};

GPIO_CHIP * gpioChip;

void crust_generate_pin_map_and_poll_list(char * mapText, int * listLength, CRUST_GPIO_PIN_MAP ** pinMap, struct pollfd ** pollList)
{
    *listLength = 0;
    *pinMap = NULL;
    *pollList = NULL;

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

        (*listLength)++;

        *pinMap = realloc(*pinMap, (sizeof(CRUST_GPIO_PIN_MAP) * *listLength));
        pinMap[0][*listLength - 1].pinID = pinNumber;
        pinMap[0][*listLength - 1].trackCircuitID = trackCircuitNumber;
    }

    *pollList = malloc(sizeof(struct pollfd) * *listLength);
}

_Noreturn void crust_node_stop()
{
    crust_terminal_print_verbose("Closing the GPIO connection...");
    gpiod_chip_close(gpioChip);

    exit(EXIT_SUCCESS);
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

_Noreturn void crust_node_loop(int listLength, CRUST_GPIO_PIN_MAP * pinMap, struct pollfd * pollList)
{
    for(;;)
    {
        struct gpiod_line_event event;
        poll(pollList, listLength, -1);
        for(int i = 0; i < listLength; i++)
        {
            if(pollList[i].revents)
            {
                gpiod_line_event_read_fd(pollList[i].fd, &event);
                printf("Track circuit %i ", pinMap[i].trackCircuitID);
                if(event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE)
                {
                    printf("LOW\r\n");
                }
                else if(event.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
                {
                    printf("HIGH\r\n");
                }
            }
        }
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

    int listLength;
    CRUST_GPIO_PIN_MAP * pinMap;
    struct pollfd * pollList;

    crust_generate_pin_map_and_poll_list(crustOptionPinMapString, &listLength, &pinMap, &pollList);

    for(int i = 0; i < listLength; i++)
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

        pollList[i].fd = gpiod_line_event_get_fd(pinMap[i].gpioLine);
        if(pollList[i].fd < 0)
        {
            crust_terminal_print("Failed to obtain file descriptor for a GPIO line");
            exit(EXIT_FAILURE);
        }

        pollList[i].events = POLLIN;
    }

    int socketFD = socket(AF_INET, SOCK_STREAM, 0);
    if(socketFD == -1)
    {
        crust_terminal_print("Unable to create socket");
        exit(EXIT_FAILURE);
    }

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

    // Create a socket
    // Configure the socket address
    // Connect the socket
    // Retry if the connection fails
    // Reconnect on hangup

    crust_node_loop(listLength, pinMap, pollList);
}