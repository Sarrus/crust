#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <poll.h>
#include <stdio.h>
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

//TODO: Make this dynamic
#define FP_STACK_SIZE 4096
_Noreturn void crust_daemon_loop()
{
    struct pollfd pollList[FP_STACK_SIZE];
    pollList[0].fd = socketFp;
    pollList[0].events = POLLRDBAND | POLLRDNORM;
    for(;;)
    {
        if(poll(&pollList[0], 1, -1) == -1)
        {
            crust_terminal_print("Error occurred while polling connections.");
            exit(EXIT_FAILURE);
        }
        crust_terminal_print_verbose("Polled");
        if(accept(socketFp, NULL, 0) == -1)
        {
            crust_terminal_print("Failed to accept a socket connection.");
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

    // Start accepting connections
    if(listen(socketFp, CRUST_SOCKET_QUEUE_LIMIT))
    {
        crust_terminal_print("Failed to enable listening on the CRUST socket.");
        exit(EXIT_FAILURE);
    }

    crust_daemon_loop();
}