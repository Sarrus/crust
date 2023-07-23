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
#include <stdbool.h>
#include <time.h>
#include "node.h"
#include "terminal.h"
#include "options.h"
#ifdef SYSTEMD
#include <systemd/sd-daemon.h>
#endif

/*
 * This is the time that a GPIO line has to stay in it's state before the appropriate track circuit is updated. This allows
 * for some flickering of the circuit when it changes state and ignores brief spikes / troughs in voltage. Max 1000
 * */
#define CRUST_NODE_SETTLE_TIME 100 //ms

#define GPIO_CHIP struct gpiod_chip

#define CRUST_GPIO_PIN_MAP struct crustGPIOPinMap
struct crustGPIOPinMap {
    unsigned int pinID;
    CRUST_IDENTIFIER trackCircuitID;
    struct gpiod_line * gpioLine;
    bool lastOccupationRead; // The last occupation state read on the line (true = occupied, false = clear)
    bool lastOccupationSent; // The last occupation state sent to the server
    struct timespec lastReadAt; // The time the last read occurred
};

GPIO_CHIP * gpioChip;

void crust_generate_pin_map_and_poll_list(char * mapText, int * listLength, CRUST_GPIO_PIN_MAP ** pinMap, struct pollfd ** pollList)
{
    *listLength = 1; // Start at length 1 to leave an empty space for the socket
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
#ifdef SYSTEMD
    sd_notify(0, "STOPPING=1\n"
                 "STATUS=CRUST Node shutting down...");
#endif
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
    struct timespec now;
    int pollTimeout = 1; // Set the first poll to time out straight away
    // pollList[0].fd is the socket connected to the server
    for(;;)
    {
        struct gpiod_line_event event;
        poll(pollList, listLength, pollTimeout);
        pollTimeout = -1; // Disable the timeout (then re-enable it below if required)
        if(pollList[0].revents & POLLHUP)
        {
            crust_terminal_print_verbose("CRUST server disconnected.");
            crust_node_stop();
        }
        if((pollList[0].revents & POLLRDNORM) && !read(pollList[0].fd, NULL, 1))
        {
            crust_terminal_print_verbose("CRUST server closing connection.");
            shutdown(pollList[0].fd, SHUT_RDWR);
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        for(int i = 1; i < listLength; i++)
        {
            if(pollList[i].revents)
            {
                gpiod_line_event_read_fd(pollList[i].fd, &event);
                if(event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE)
                {
                    pinMap[i].lastOccupationRead = false;
                }
                else if(event.event_type == GPIOD_LINE_EVENT_RISING_EDGE)
                {
                    pinMap[i].lastOccupationRead = true;
                }

                pinMap[i].lastReadAt.tv_sec = now.tv_sec;
                pinMap[i].lastReadAt.tv_nsec = now.tv_nsec;
                pollTimeout = CRUST_NODE_SETTLE_TIME; // Set a poll timeout
            }
            else if(pinMap[i].lastOccupationRead != pinMap[i].lastOccupationSent)
            {
                // Calculate the time since the last read
                struct timespec difference;
                difference.tv_sec = now.tv_sec - pinMap[i].lastReadAt.tv_sec;
                difference.tv_nsec = now.tv_nsec - pinMap[i].lastReadAt.tv_nsec;
                if(difference.tv_nsec < 0)
                {
                    difference.tv_nsec += (long)1e9;
                    difference.tv_sec--;
                }

                if(difference.tv_sec || (difference.tv_nsec > (CRUST_NODE_SETTLE_TIME * 1000)))
                {
                    if(pinMap[i].lastOccupationRead)
                    {
                        dprintf(pollList[0].fd, "OC;%i;\r\n", pinMap[i].trackCircuitID);
                    }
                    else
                    {
                        dprintf(pollList[0].fd, "CC;%i;\r\n", pinMap[i].trackCircuitID);
                    }
                    pinMap[i].lastOccupationSent = pinMap[i].lastOccupationRead;
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

    for(int i = 1; i < listLength; i++)
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
        pinMap[i].lastOccupationRead = gpiod_line_get_value(pinMap[i].gpioLine);
        pinMap[i].lastOccupationSent = !pinMap[i].lastOccupationRead;

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

    // Put the server socket in entry 0 on the poll list
    pollList[0].fd = socketFD;
    pollList[0].events = POLLRDNORM;

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

    // Create a socket
    // Configure the socket address
    // Connect the socket
    // Retry if the connection fails
    // Reconnect on hangup

#ifdef SYSTEMD
    sd_notify(0, "READY=1\n"
                 "STATUS=CRUST Node running");
#endif

    crust_node_loop(listLength, pinMap, pollList);
}