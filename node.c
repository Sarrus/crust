#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <gpiod.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include "node.h"
#include "terminal.h"
#include "options.h"

#define GPIO_CHIP struct gpiod_chip

#define CRUST_GPIO_PIN_MAP struct crustGPIOPinMap
struct crustGPIOPinMap {
    unsigned int pinID;
    CRUST_IDENTIFIER trackCircuitID;
};

GPIO_CHIP * gpioChip;

void crust_generate_pin_map_and_poll_list(char * mapText, int * listLength, CRUST_GPIO_PIN_MAP ** pinMap, struct pollfd ** pollList)
{
    *listLength = 0;
    *pinMap = NULL;

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

_Noreturn void crust_node_loop()
{
    for(;;)
    {
        sleep(1);
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

    // Get the lines as objects
    // Reserve and set to input gpiod_line_request_input
    // Request events gpiod_line_request_bulk_both_edges_events
    // Setup a poll of the lines gpiod_line_event_get_fd gpiod_line_event_read_fd

    crust_node_loop();
}