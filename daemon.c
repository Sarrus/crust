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
#include "messaging.h"

#define CRUST_WRITE struct crustWrite
#define CRUST_BUFFER_LIST_ENTRY struct crustBufferListEntry
#define CRUST_MAX_WRITE_QUEUE_LENGTH 5

static int socketFp;

struct crustWrite {
    char * writeBuffer;
    unsigned long bufferLength;
    unsigned int targets;
};

struct crustBufferListEntry {
    CRUST_INPUT_BUFFER inputBuffer; // Where input from the user goes
    CRUST_WRITE * writeQueue[CRUST_MAX_WRITE_QUEUE_LENGTH]; // A queue of CRUST_WRITES to be executed
    unsigned int writeQueueArrival; // Points to the next available place in the queue (back of the queue)
    unsigned int writeQueueService; // Points to the next write to be executed / being executed (front of the queue)
    unsigned long currentWritePositionPointer; // Our position in the current CRUST_WRITE
};

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

/*
 * Regenerates a buffer and poll pollList. Both should always be the same length as each entry on the poll pollList has a matching
 * entry on the buffer pollList.
 *
 * The poll pollList is a pollList of connections to the CRUST socket. It is formatted to be passed to poll(). Each connection can
 * be readable which means that the user is trying to send us some data or writable which means that the user is ready to
 * receive some data from us.
 *
 * CRUST watches all connections for readability and processes instructions as they come in.
 *
 * CRUST only watches connections for writeability when it has some data to write. When there is no data to write, CRUST
 * stops polling the connection for writeability.
 *
 * The buffer pollList contains a read buffer for each connection. The read buffer is a chunk of memory that contains an operation
 * for CRUST to process. The user fills this buffer by writing to their connection. When they send either a CR or an LF, CRUST
 * executes the operation and resets the buffer.
 *
 * The buffer pollList also contains a queue of CRUST_WRITEs for the connection. While there are writes in the queue, CRUST will
 * send them to the connection whenever it is ready to be written to.
 */
void crust_poll_list_and_buffer_list_regen(struct pollfd ** pollList, int * listLength, int * listPointer,
                                           CRUST_BUFFER_LIST_ENTRY **bufferList)
{
    crust_terminal_print_verbose("Regenerating poll pollList...");

    // On each regen we leave space for 100 new connections
    int newListLength = 100;

    // Take our 100 new spaces and then add an additional one for each existing connection. (We don't count disconnected
    // sockets which have a negative fd)
    for(int i = 0; i < *listPointer; i++)
    {
        if((*pollList)[i].fd > 0)
        {
            newListLength++;
        }
    }

    // Allocate the memory for the two new lists
    struct pollfd * newPollList = malloc(sizeof(struct pollfd) * newListLength);
    CRUST_BUFFER_LIST_ENTRY * newBufferList = malloc(sizeof(CRUST_BUFFER_LIST_ENTRY) * newListLength);

    // Empty the two new lists
    memset(newPollList, '\0', sizeof(struct pollfd) * newListLength);
    memset(newBufferList, '\0', sizeof(CRUST_BUFFER_LIST_ENTRY) * newListLength);

    // Add the CRUST socket to the top of the pollList and set it to read. poll() will mark it as readable when a new connection
    // is available.
    newPollList[0].fd = socketFp;
    newPollList[0].events = POLLRDBAND | POLLRDNORM;

    // Point to the next free entry
    int newListPointer = 1;

    // Copy each active entry from both of the old lists to both of the new lists.
    for(int i = 1; i < *listPointer; i++)
    {
        if((*pollList)[i].fd > 0)
        {
            newPollList[newListPointer].fd = (*pollList)[i].fd;
            newPollList[newListPointer].events = (*pollList)[i].events;
            newBufferList[newListPointer].inputBuffer.writePointer = (*bufferList)[i].inputBuffer.writePointer;
            newBufferList[newListPointer].writeQueueArrival = (*bufferList)[i].writeQueueArrival;
            newBufferList[newListPointer].writeQueueService = (*bufferList)[i].writeQueueService;
            newBufferList[newListPointer].currentWritePositionPointer = (*bufferList)[i].currentWritePositionPointer;
            for(int j = 0; j < CRUST_MAX_MESSAGE_LENGTH; j++)
            {
                newBufferList[newListPointer].inputBuffer.buffer[j] = (*bufferList)[i].inputBuffer.buffer[j];
            }
            for(int j = 0; j < CRUST_MAX_WRITE_QUEUE_LENGTH; j++)
            {
                newBufferList[newListPointer].writeQueue[j] = (*bufferList)[i].writeQueue[j];
            }
            newListPointer++;
        }
    }

    // If we are replacing an existing pair of lists then free their associated memory
    if(*pollList)
    {
        free(*pollList);
    }
    if(*bufferList)
    {
        free(*bufferList);
    }

    // Point to the new lists
    *pollList = newPollList;
    *listLength = newListLength;
    *listPointer = newListPointer;
    *bufferList = newBufferList;
}

void crust_write_queue_insert(struct pollfd * pollList, CRUST_BUFFER_LIST_ENTRY * bufferList, int listEntryID,
                                CRUST_WRITE * writeToInsert)
{
    // Check that the write queue isn't full
    if((bufferList[listEntryID].writeQueueArrival + 1) % CRUST_MAX_WRITE_QUEUE_LENGTH == bufferList[listEntryID].writeQueueService)
    {
        crust_terminal_print_verbose("Write queue filled, terminating connection");
        close(pollList[listEntryID].fd);
        pollList[listEntryID].fd = -(pollList[listEntryID].fd);
        // TODO: If there are no other connections waiting for this CRUST_WRITE, free() the write.
        return;
    }

    // Add the new entry to the queue
    bufferList[listEntryID].writeQueue[bufferList[listEntryID].writeQueueArrival] = writeToInsert;

    // Update queue pointers
    (bufferList[listEntryID].writeQueueArrival)++;
    bufferList[listEntryID].writeQueueArrival %= CRUST_MAX_WRITE_QUEUE_LENGTH;

    // Update count of targets
    (writeToInsert->targets)++;

    // Enable listening for writes
    pollList[listEntryID].events |= (POLLWRBAND | POLLWRNORM);
}

_Noreturn void crust_daemon_loop(CRUST_STATE * state)
{
    // Create a poll list and a buffer list
    struct pollfd * pollList = NULL;
    int pollListLength = 0;
    int pollListPointer = 0;
    CRUST_BUFFER_LIST_ENTRY * bufferList = NULL;
    crust_poll_list_and_buffer_list_regen(&pollList, &pollListLength, &pollListPointer, &bufferList);

    for(;;)
    {
        // Poll. This will block until a socket becomes readable and / or a new connection is made.
        int pollResult = poll(&pollList[0], pollListPointer, -1);
        if(pollResult == -1)
        {
            crust_terminal_print("Error occurred while polling connections.");
            exit(EXIT_FAILURE);
        }

        // Go through all the connections (except the socket itself)
        for(int i = 1; i < pollListPointer; i++)
        {
            // deactivate the connection in the poll list if it hangs up
            if(pollList[i].revents & POLLHUP)
            {
                crust_terminal_print_verbose("Connection terminated.");
                pollList[i].fd = -(pollList[i].fd);
            }
            // Send some data if the socket is ready to be read
            else if(pollList[i].revents & (POLLRDBAND | POLLRDNORM))
            {
                // Calculate how many bytes we can read
                unsigned int bufferSpaceRemaining = CRUST_MAX_MESSAGE_LENGTH - bufferList[i].inputBuffer.writePointer;

                // Proceed if we have space to buffer the bytes
                if(bufferSpaceRemaining)
                {
                    // Read bytes up to the maximum we can accept
                    size_t readBytes = read(pollList[i].fd, &bufferList[i].inputBuffer.buffer[bufferList[i].inputBuffer.writePointer], bufferSpaceRemaining);

                    // Set the write pointer to the start of the remaining free space
                    bufferList[i].inputBuffer.writePointer += readBytes;

                    // If there are bytes in the buffer and the user has sent a CR or LF at the end then process the buffer
                    if(bufferList[i].inputBuffer.writePointer > 0
                        && (bufferList[i].inputBuffer.buffer[bufferList[i].inputBuffer.writePointer - 1] == '\r'
                            || bufferList[i].inputBuffer.buffer[bufferList[i].inputBuffer.writePointer - 1] == '\n'))
                    {
                        // Interpret the message from the user, splitting it into an opcode and optionally some input
                        CRUST_MIXED_OPERATION_INPUT operationInput;
                        CRUST_OPCODE opcode = crust_interpret_message(bufferList[i].inputBuffer.buffer,
                                                                      bufferList[i].inputBuffer.writePointer,
                                                                      &operationInput,
                                                                      state);

                        // Process the user's operation
                        switch(opcode)
                        {
                            // Attempt to insert a block and generate the appropriate response
                            case INSERT_BLOCK:
                                crust_terminal_print_verbose("OPCODE: Insert Block");
                                switch(crust_block_insert(operationInput.block, state))
                                {
                                    case 0:
                                        crust_terminal_print_verbose("Block inserted successfully");
                                        break;

                                    case 1:
                                        crust_terminal_print_verbose("Failed to insert block - no links");
                                        free(operationInput.block);
                                        break;

                                    case 2:
                                        crust_terminal_print_verbose("Failed to insert block - conflicting link(s)");
                                        free(operationInput.block);
                                        break;
                                }
                                break;

                            // Resend the entire state to the user
                            case RESEND_STATE:
                                crust_terminal_print_verbose("OPCODE: Resend State");
                                CRUST_WRITE * newWrite = malloc(sizeof(CRUST_WRITE));
                                newWrite->bufferLength = crust_print_state(state, &newWrite->writeBuffer);
                                newWrite->targets = 0;
                                crust_write_queue_insert(pollList, bufferList, i, newWrite);
                                break;

                            // Do nothing
                            case NO_OPERATION:
                                crust_terminal_print_verbose("OPCODE: No Operation");
                                break;

                            // Report that the opcode was unrecognised.
                            default:
                                crust_terminal_print_verbose("Unrecognised OPCODE");
                                break;
                        }

                        // Put the write pointer back to the beginning (clear the buffer)
                        bufferList[i].inputBuffer.writePointer = 0;
                    }
                }
                else
                {
                    //TODO: Deal with a full buffer
                }
            }
            else if(pollList[i].revents & (POLLWRBAND | POLLWRNORM))
            {
                // Go through the write queue in order
                for(;;)
                {
                    // Attempt a write
                    ssize_t bytesWritten = write(pollList[i].fd,
                  bufferList[i].writeQueue[bufferList[i].writeQueueService]->writeBuffer + bufferList[i].currentWritePositionPointer,
                bufferList[i].writeQueue[bufferList[i].writeQueueService]->bufferLength - bufferList[i].currentWritePositionPointer);
                    crust_terminal_print_verbose("write");
                    // If no bytes can be written then stop and let the connection get re-polled
                    if(!bytesWritten)
                    {
                        break;
                    }

                    bufferList[i].currentWritePositionPointer += bytesWritten;

                    // If the write suceedes completely, drop it from the queue and try the next entry
                    if(bufferList[i].currentWritePositionPointer == bufferList[i].writeQueue[bufferList[i].writeQueueService]->bufferLength)
                    {
                        (bufferList[i].writeQueue[bufferList[i].writeQueueService]->targets)--;
                        if(!bufferList[i].writeQueue[bufferList[i].writeQueueService]->targets)
                        {
                            free(bufferList[i].writeQueue[bufferList[i].writeQueueService]);
                        }
                        bufferList[i].currentWritePositionPointer = 0;
                        (bufferList[i].writeQueueService)++;
                        bufferList[i].writeQueueService %= CRUST_MAX_WRITE_QUEUE_LENGTH;
                    }
                    // If all writes complete, unflag the connection for writing.
                    if(bufferList[i].writeQueueArrival == bufferList[i].writeQueueService)
                    {
                        pollList[i].events ^= (POLLWRBAND | POLLWRNORM);
                        break;
                    }
                }
            }
        }

        // pollList[0] always contains the CRUST socket. This will show up as readable when a new connection is available
        if(pollList[0].revents & (POLLRDBAND | POLLRDNORM))
        {
            // Accept the new connection and store it's fp
            int newSocketFp = accept(socketFp, NULL, 0);
            if(newSocketFp == -1)
            {
                crust_terminal_print("Failed to accept a socket connection.");
                exit(EXIT_FAILURE);
            }

            // Set the connection to non-blocking
            if(fcntl(newSocketFp, F_SETFL, fcntl(newSocketFp, F_GETFL) | O_NONBLOCK) == -1)
            {
                crust_terminal_print("Unable to make the new socket non-blocking.");
                exit(EXIT_FAILURE);
            }

            // The poll list needs to be regenerated if we run out of space.
            if(pollListLength <= pollListPointer)
            {
                crust_poll_list_and_buffer_list_regen(&pollList, &pollListLength, &pollListPointer, &bufferList);
            }

            // Add the new connection to the poll list and request an alert if it becomes readable. (We will also be
            // informed if the other end hangs up.)
            pollList[pollListPointer].fd = newSocketFp;
            pollList[pollListPointer].events = POLLRDBAND | POLLRDNORM;
            pollListPointer++;
            crust_terminal_print_verbose("New connection accepted.");
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
    unsigned long addrLength =
#ifdef MACOS
            sizeof(socketAddress->sa_len) +
#endif
            sizeof(socketAddress->sa_family) +
            strlen(crustOptionSocketPath);

    // Allocate the memory
    socketAddress = malloc(addrLength);

    // Empty the structure
    memset(socketAddress, '\0', addrLength);

    // Fill the structure
#ifdef MACOS
    socketAddress->sa_len = addrLength;
#endif
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

    crust_daemon_loop(state);
}