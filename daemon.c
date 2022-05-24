#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <poll.h>
#include <stdio.h>
#include <fcntl.h>
#include "daemon.h"
#include "terminal.h"
#include "state.h"
#include "options.h"

static int socketFp;

_Noreturn void crust_daemon_stop()
{
    crust_terminal_print_verbose("Closing the CRUST socket...");
    close(socketFp);

    crust_terminal_print_verbose("Removing the CRUST socket from the VFS...");
    unlink(crustOptionSocketPath);

    exit(EXIT_SUCCESS);
}

void crust_handle_signal(int signal)
{
    switch(signal)
    {
        case SIGINT:
            crust_terminal_print_verbose("Received SIGINT, shutting down...");
            crust_daemon_stop();
        case SIGTERM:
            crust_terminal_print_verbose("Received SIGTERM, shutting down...");
            crust_daemon_stop();
        default:
            crust_terminal_print("Received an unexpected signal, exiting.");
            exit(EXIT_FAILURE);
    }
}

void crust_poll_list_regen(struct pollfd ** list, int * listLength, int * listPointer)
{
    crust_terminal_print_verbose("Regenerating poll list...");

    int newListLength = 100;

    for(int i = 0; i < *listPointer; i++)
    {
        if((*list)[i].fd > 0)
        {
            newListLength++;
        }
    }

    struct pollfd * newList = malloc(sizeof(struct pollfd) * newListLength);

    memset(newList, '\0', sizeof(struct pollfd) * newListLength);

    newList[0].fd = socketFp;
    newList[0].events = POLLRDBAND | POLLRDNORM;

    int newListPointer = 1;

    for(int i = 1; i < *listPointer; i++)
    {
        if((*list)[i].fd > 0)
        {
            newList[newListPointer].fd = (*list)[i].fd;
            newList[newListPointer].events = (*list)[i].events;
            newListPointer++;
        }
    }

    if(*list)
    {
        free(*list);
    }
    *list = newList;
    *listLength = newListLength;
    *listPointer = newListPointer;
}

_Noreturn void crust_daemon_loop()
{
    struct pollfd * pollList = NULL;
    int pollListLength = 0;
    int pollListPointer = 0;

    crust_poll_list_regen(&pollList, &pollListLength, &pollListPointer);

    for(;;)
    {
        int pollResult = poll(&pollList[0], pollListPointer, -1);
        if(pollResult == -1)
        {
            crust_terminal_print("Error occurred while polling connections.");
            exit(EXIT_FAILURE);
        }

        // pollList[0] always contains the CRUST socket. This will show up as readable when a new connection is available
        if(pollList[0].revents & (POLLRDBAND | POLLRDNORM))
        {
            int newSocketFp = accept(socketFp, NULL, 0);
            if(newSocketFp == -1)
            {
                crust_terminal_print("Failed to accept a socket connection.");
                exit(EXIT_FAILURE);
            }

            if(fcntl(newSocketFp, F_SETFL, fcntl(newSocketFp, F_GETFL) | O_NONBLOCK) == -1)
            {
                crust_terminal_print("Unable to make the new socket non-blocking.");
                exit(EXIT_FAILURE);
            }

            // The poll list needs to be regenerated if we run out of space.
            if(pollListLength <= pollListPointer)
            {
                crust_poll_list_regen(&pollList, &pollListLength, &pollListPointer);
            }

            // Add the new connection to the poll list and request an alert if it becomes writable. (We will also be
            // informed if the other end hangs up.)
            pollList[pollListPointer].fd = newSocketFp;
            pollList[pollListPointer].events = POLLRDBAND | POLLRDNORM;
            pollListPointer++;
            crust_terminal_print_verbose("New connection accepted.");
        }

        for(int i = 1; i < pollListPointer; i++)
        {
            if(pollList[i].revents & POLLHUP)
            {
                crust_terminal_print_verbose("Connection terminated.");
                pollList[i].fd = -(pollList[i].fd);
            }
            else if(pollList[i].revents & (POLLRDBAND | POLLRDNORM))
            {
                char readBuffer[16];
                size_t readBytes;
                while((readBytes = read(pollList[i].fd, &readBuffer, 16)) != -1)
                {
                    write(STDOUT_FILENO, &readBuffer, readBytes);
                }
            }
        }
    }
}

// Starts the CRUST daemon, exiting when the daemon finishes.
_Noreturn void crust_daemon_run()
{
    crust_terminal_print_verbose("CRUST daemon starting...");
    crust_terminal_print_verbose("Building initial state...");

    CRUST_STATE * state;
    crust_state_init(&state);

    crust_terminal_print_verbose("Removing previous CRUST socket...");
    if((unlink(crustOptionSocketPath) == -1) && (errno != ENOENT))
    {
        crust_terminal_print("Unable to remove previous CRUST socket or socket address invalid");
        exit(EXIT_FAILURE);
    }

    crust_terminal_print_verbose("Creating CRUST socket...");

    // Request a socket from the kernel
    socketFp = socket(PF_LOCAL, SOCK_STREAM, 0);
    if(socketFp == -1)
    {
        crust_terminal_print("Failed to create CRUST socket.");
        exit(EXIT_FAILURE);
    }

    // Declare a structure to hold the socket address
    struct sockaddr * socketAddress;

    // Measure the socket address length
    unsigned long addrLength = sizeof(socketAddress->sa_len) + sizeof(socketAddress->sa_family) + strlen(crustOptionSocketPath);

    // Allocate the memory
    socketAddress = malloc(addrLength);

    // Empty the structure
    memset(socketAddress, '\0', addrLength);

    // Fill the structure
    socketAddress->sa_len = addrLength;
    socketAddress->sa_family = AF_LOCAL;
    strcpy(socketAddress->sa_data, crustOptionSocketPath);

    // Set the umask
    mode_t lastUmask = umask(CRUST_DEFAULT_SOCKET_UMASK);

    // Bind to the VFS
    if(bind(socketFp, socketAddress, addrLength) == -1)
    {
        if(errno == ENOENT)
        {
            crust_terminal_print("Unable to create CRUST socket - no such file or directory. Please ensure that the "
                                 "specified directory exists (CRUST will create the socket itself).");
            exit(EXIT_FAILURE);
        }

        if(errno == EACCES)
        {
            crust_terminal_print("Unable to create CRUST socket - permission denied. Please ensure that the process "
                                 "can write to the socket directory.");
            exit(EXIT_FAILURE);
        }

        crust_terminal_print("Failed to bind CRUST socket to the VFS.");
        exit(EXIT_FAILURE);
    }

    // Free the memory used for the address
    free(socketAddress);

    // Pop the previous umask
    umask(lastUmask);

    // Register the signal handlers
    signal(SIGINT, crust_handle_signal);
    signal(SIGTERM, crust_handle_signal);

    // Make the socket non-blocking
    if(fcntl(socketFp, F_SETFD, fcntl(socketFp, F_GETFD) | O_NONBLOCK) == -1)
    {
        crust_terminal_print("Unable to make the CRUST socket non-blocking.");
        exit(EXIT_FAILURE);
    }

    // Start accepting connections
    if(listen(socketFp, CRUST_SOCKET_QUEUE_LIMIT))
    {
        crust_terminal_print("Failed to enable listening on the CRUST socket.");
        exit(EXIT_FAILURE);
    }

    crust_daemon_loop();
}