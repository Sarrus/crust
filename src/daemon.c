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
#ifdef SYSTEMD
#include <systemd/sd-daemon.h>
#endif
#ifdef TESTING
#include "tools/lipsum.h"
#endif

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
    bool listening; // Indicates that the user wants state change updates
};

_Noreturn void crust_daemon_stop()
{
#ifdef SYSTEMD
    sd_notify(0, "STOPPING=1\n"
                 "STATUS=CRUST Daemon shutting down...");
#endif
    crust_terminal_print_verbose("Closing the CRUST socket...");
    close(socketFp);

    exit(EXIT_SUCCESS);
}

void crust_daemon_handle_signal(int signal)
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
 * The buffer pollList contains a read buffer for each connection. The read buffer is a chunk of memory that contains an
 * operation for CRUST to process. The user fills this buffer by writing to their connection. When they send an LF CRUST
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

void crust_close_connection(struct pollfd * pollList, CRUST_BUFFER_LIST_ENTRY * bufferList, int listEntryID)
{
    shutdown(pollList[listEntryID].fd, SHUT_RDWR);
    close(pollList[listEntryID].fd);
    pollList[listEntryID].fd = -(pollList[listEntryID].fd);
    pollList[listEntryID].revents = 0;
    bufferList[listEntryID].listening = false;
}

void crust_write_queue_insert(struct pollfd * pollList, CRUST_BUFFER_LIST_ENTRY * bufferList, int listEntryID,
                                CRUST_WRITE * writeToInsert)
{
    // Check that the write queue isn't full
    if((bufferList[listEntryID].writeQueueArrival + 1) % CRUST_MAX_WRITE_QUEUE_LENGTH == bufferList[listEntryID].writeQueueService)
    {
        crust_terminal_print_verbose("Write queue filled, terminating connection");
        crust_close_connection(pollList, bufferList, listEntryID);
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

void crust_write_to_listeners(struct pollfd * pollList, CRUST_BUFFER_LIST_ENTRY * bufferList, int listLength,
                              CRUST_WRITE * write)
{
    for(int i = 1; i < listLength; i++)
    {
        if(bufferList[i].listening)
        {
            crust_write_queue_insert(pollList, bufferList, i, write);
        }
    }

    if(!(write->targets))
    {
        free(write);
    }
}

/*
 * Takes a pointer to a CRUST message and the length of the message and returns the detected opcode. If there is an input
 * to go with the operation, fills operationInput. See CRUST_MIXED_OPERATION_INPUT for details. If the opcode is not
 * recognised or there is an error, returns NO_OPERATION.
 */
CRUST_OPCODE crust_daemon_interpret_message(char * message, CRUST_MIXED_OPERATION_INPUT * operationInput, CRUST_STATE * state)
{
    // Return NOP if the message is too short
    if(strlen(message) < 2)
    {
        return NO_OPERATION;
    }

    switch(message[0])
    {
        case 'C':
            switch(message[1])
            {
                case 'C':
                    if(crust_interpret_identifier(&message[2], &operationInput->identifier))
                    {
                        crust_terminal_print_verbose("Invalid identifier");
                        return NO_OPERATION;
                    }
                    return CLEAR_TRACK_CIRCUIT;

                default:
                    return NO_OPERATION;
            }

        case 'E':
            switch(message[1])
            {
                case 'U':
                    if(crust_interpret_identifier(&message[2], &operationInput->identifier))
                    {
                        crust_terminal_print_verbose("Invalid identifier");
                        return NO_OPERATION;
                    }
                    return ENABLE_BERTH_UP;

                case 'D':
                    if(crust_interpret_identifier(&message[2], &operationInput->identifier))
                    {
                        crust_terminal_print_verbose("Invalid identifier");
                        return NO_OPERATION;
                    }
                    return ENABLE_BERTH_DOWN;

                default:
                    return NO_OPERATION;
            }

        case 'I':
            switch(message[1])
            {
                case 'B':
                    // Initialise a block and try to fill it.
                    crust_block_init(&operationInput->block, state);
                    if(crust_interpret_block(&message[2], operationInput->block, state))
                    {
                        crust_terminal_print_verbose("Invalid block description message");
                        free(operationInput->block);
                        return NO_OPERATION;
                    }
                    return INSERT_BLOCK;

                case 'C':
                    crust_track_circuit_init(&operationInput->trackCircuit, state);
                    if(crust_interpret_track_circuit(&message[2], operationInput->trackCircuit, state))
                    {
                        crust_terminal_print_verbose("Invalid circuit member list");
                        free(operationInput->trackCircuit);
                        return NO_OPERATION;
                    }
                    return INSERT_TRACK_CIRCUIT;

                case 'P':
                    operationInput->interposeInstruction = malloc(sizeof(CRUST_INTERPOSE_INSTRUCTION));
                    if(crust_interpret_interpose_instruction(&message[2], operationInput->interposeInstruction))
                    {
                        crust_terminal_print_verbose("Invalid interpose instruction");
                        free(operationInput->interposeInstruction);
                        return NO_OPERATION;
                    }
                    return INTERPOSE;


                default:
                    return NO_OPERATION;
            }

        case 'O':
            switch(message[1])
            {
                case 'C':
                    if(crust_interpret_identifier(&message[2], &operationInput->identifier))
                    {
                        crust_terminal_print_verbose("Invalid identifier");
                        return NO_OPERATION;
                    }
                    return OCCUPY_TRACK_CIRCUIT;
            }

        case 'R':
            switch(message[1])
            {
                case 'S':
                    return RESEND_STATE;

#ifdef TESTING
                    case 'L':
                    return RESEND_LIPSUM;
#endif

                default:
                    return NO_OPERATION;
            }

        case 'S':
            switch(message[1])
            {
                case 'L':
                    return START_LISTENING;

                default:
                    return NO_OPERATION;
            }

        default:
            return NO_OPERATION;
    }
}

void crust_daemon_process_opcode(CRUST_OPCODE opcode, CRUST_MIXED_OPERATION_INPUT * operationInput, CRUST_STATE * state,
                                struct pollfd * pollList, CRUST_BUFFER_LIST_ENTRY * bufferList, int listPosition, int listLength)
{
    CRUST_WRITE * write;
    CRUST_TRACK_CIRCUIT * identifiedTrackCircuit;
    CRUST_BLOCK * identifiedBlock;
    CRUST_BLOCK ** affectedBlocks = NULL;
    size_t affectedBlockCount = 0;

    // Process the user's operation
    switch(opcode)
    {
        // Attempt to insert a block and generate the appropriate response
        case INSERT_BLOCK:
            crust_terminal_print_verbose("OPCODE: Insert Block");
            switch(crust_block_insert(operationInput->block, state))
            {
                case 0:
                    crust_terminal_print_verbose("Block inserted successfully");
                    write = malloc(sizeof(CRUST_WRITE));
                    write->writeBuffer = malloc(CRUST_MAX_MESSAGE_LENGTH);
                    write->targets = 0;
                    write->bufferLength = crust_print_block(operationInput->block, &write->writeBuffer);
                    crust_write_to_listeners(pollList, bufferList, listLength, write);
                    break;

                case 1:
                    crust_terminal_print_verbose("Failed to insert block - name is not unique");
                    if(operationInput->block->blockName != NULL)
                    {
                        free(operationInput->block->blockName);
                    }
                    free(operationInput->block);
                    break;

                case 2:
                    crust_terminal_print_verbose("Failed to insert block - conflicting link(s)");
                    if(operationInput->block->blockName != NULL)
                    {
                        free(operationInput->block->blockName);
                    }
                    free(operationInput->block);
                    break;

                case 3:
                    crust_terminal_print_verbose("Failed to insert block - no links");
                    if(operationInput->block->blockName != NULL)
                    {
                        free(operationInput->block->blockName);
                    }
                    free(operationInput->block);
                    break;

                case 4:
                    crust_terminal_print_verbose("Cannot insert more blocks after track circuits have been inserted");
                    if(operationInput->block->blockName != NULL)
                    {
                        free(operationInput->block->blockName);
                    }
                    free(operationInput->block);
                    break;
            }
            break;

        case INSERT_TRACK_CIRCUIT:
            crust_terminal_print_verbose("OPCODE: Insert track circuit");
            switch(crust_track_circuit_insert(operationInput->trackCircuit, state))
            {
                case 0:
                    crust_terminal_print_verbose("Track circuit inserted successfully.");
                    write = malloc(sizeof(CRUST_WRITE));
                    write->targets = 0;
                    write->bufferLength = crust_print_track_circuit(operationInput->trackCircuit, &write->writeBuffer);
                    crust_write_to_listeners(pollList, bufferList, listLength, write);
                    break;

                case 1:
                    crust_terminal_print_verbose("Failed to insert track circuit - no blocks");
                    free(operationInput->trackCircuit->blocks);
                    free(operationInput->trackCircuit);
                    break;

                case 2:
                    crust_terminal_print_verbose("Failed to insert track circuit - blocks already part of a different track circuit");
                    free(operationInput->trackCircuit->blocks);
                    free(operationInput->trackCircuit);
                    break;

                case 3:
                    crust_terminal_print_verbose("Failed to insert track circuit - not all blocks are connected together");
                    free(operationInput->trackCircuit->blocks);
                    free(operationInput->trackCircuit);
            }
            break;

            // Resend the entire state to the user
        case RESEND_STATE:
            crust_terminal_print_verbose("OPCODE: Resend State");
            write = malloc(sizeof(CRUST_WRITE));
            write->bufferLength = crust_print_state(state, &write->writeBuffer);
            write->targets = 0;
            crust_write_queue_insert(pollList, bufferList, listPosition, write);
            break;

#ifdef TESTING
        case RESEND_LIPSUM:
            crust_terminal_print_verbose("OPCODE: Resend Lipsum");
            write = malloc(sizeof(CRUST_WRITE));
            write->bufferLength = strlen(lipsum);
            write->writeBuffer = malloc(write->bufferLength);
            strcpy(write->writeBuffer, lipsum);
            write->targets = 0;
            crust_write_queue_insert(pollList, bufferList, listPosition, write);
            break;
#endif
            // Send the state then send updates as it changes.
        case START_LISTENING:
            crust_terminal_print_verbose("OPCODE: Start Listening");
            write = malloc(sizeof(CRUST_WRITE));
            write->bufferLength = crust_print_state(state, &write->writeBuffer);
            write->targets = 0;
            crust_write_queue_insert(pollList, bufferList, listPosition, write);
            bufferList[listPosition].listening = true;
            break;

        case CLEAR_TRACK_CIRCUIT:
            crust_terminal_print_verbose("OPCODE: Clear Track Circuit");
            if(crust_track_circuit_get(operationInput->identifier, &identifiedTrackCircuit, state)
               && crust_track_circuit_set_occupation(identifiedTrackCircuit, false, state))
            {
                write = malloc(sizeof(CRUST_WRITE));
                write->targets = 0;
                write->bufferLength = crust_print_track_circuit(identifiedTrackCircuit, &write->writeBuffer);
                crust_write_to_listeners(pollList, bufferList, listLength, write);
            }
            break;

        case OCCUPY_TRACK_CIRCUIT:
            crust_terminal_print_verbose("OPCODE: Occupy Track Circuit");
            if(crust_track_circuit_get(operationInput->identifier, &identifiedTrackCircuit, state)
               && crust_track_circuit_set_occupation(identifiedTrackCircuit, true, state))
            {
                write = malloc(sizeof(CRUST_WRITE));
                write->targets = 0;
                write->bufferLength = crust_print_track_circuit(identifiedTrackCircuit, &write->writeBuffer);
                crust_write_to_listeners(pollList, bufferList, listLength, write);
                affectedBlockCount = crust_headcode_auto_advance(identifiedTrackCircuit, &affectedBlocks, state);
                for(int i = 0; i < affectedBlockCount; i++)
                {
                    write = malloc(sizeof(CRUST_WRITE));
                    write->targets = 0;
                    write->bufferLength = crust_print_block(affectedBlocks[i], &write->writeBuffer);
                    crust_write_to_listeners(pollList, bufferList, listLength, write);
                }
                free(affectedBlocks);
                affectedBlocks = NULL;
                affectedBlockCount = 0;
            }
            break;

        case ENABLE_BERTH_UP:
            crust_terminal_print_verbose("OPCODE: Enable Berth UP");
            if(crust_block_get(operationInput->identifier, &identifiedBlock, state)
                && crust_enable_berth(identifiedBlock, UP, state))
            {
                write = malloc(sizeof(CRUST_WRITE));
                write->writeBuffer = malloc(CRUST_MAX_MESSAGE_LENGTH);
                write->targets = 0;
                write->bufferLength = crust_print_block(identifiedBlock, &write->writeBuffer);
                crust_write_to_listeners(pollList, bufferList, listLength, write);
            }
            break;

        case ENABLE_BERTH_DOWN:
            crust_terminal_print_verbose("OPCODE: Enable Berth DOWN");
            if(crust_block_get(operationInput->identifier, &identifiedBlock, state)
               && crust_enable_berth(identifiedBlock, DOWN, state))
            {
                write = malloc(sizeof(CRUST_WRITE));
                write->writeBuffer = malloc(CRUST_MAX_MESSAGE_LENGTH);
                write->targets = 0;
                write->bufferLength = crust_print_block(identifiedBlock, &write->writeBuffer);
                crust_write_to_listeners(pollList, bufferList, listLength, write);
            }
            break;

        case INTERPOSE:
            crust_terminal_print_verbose("OPCODE: Interpose");
            if(!crust_block_get(operationInput->interposeInstruction->blockID, &identifiedBlock, state))
            {
                crust_terminal_print_verbose("Invalid block");
                break;
            }
            if(!crust_interpose(identifiedBlock, operationInput->interposeInstruction->headcode))
            {
                crust_terminal_print_verbose("Block is not a berth");
                break;
            }

            write = malloc(sizeof(CRUST_WRITE));
            write->writeBuffer = malloc(CRUST_MAX_MESSAGE_LENGTH);
            write->targets = 0;
            write->bufferLength = crust_print_block(identifiedBlock, &write->writeBuffer);
            crust_write_to_listeners(pollList, bufferList, listLength, write);
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
}

_Noreturn void crust_daemon_loop(CRUST_STATE * state)
{
    // TODO: Limit the size of the poll list
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
                crust_close_connection(pollList, bufferList, i);
            }
            // Receive some data if the socket is ready to be read
            else if(pollList[i].revents & (POLLRDBAND | POLLRDNORM))
            {
                // Calculate how many bytes we can read
                unsigned int bufferSpaceRemaining = CRUST_MAX_MESSAGE_LENGTH - bufferList[i].inputBuffer.writePointer - 1; // -1 to ensure there is always space for a null terminator

                // Proceed if we have space to buffer the bytes
                if(bufferSpaceRemaining)
                {
                    // Read one byte at a time
                    size_t readBytes = read(pollList[i].fd, &bufferList[i].inputBuffer.buffer[bufferList[i].inputBuffer.writePointer], 1);

                    // If we get 0 bytes (EOF), it means that the other end is closing the connection, we should do the same
                    if(!readBytes)
                    {
                        shutdown(pollList[i].fd, SHUT_RDWR);
                    }

                    // Set the write pointer to the start of the remaining free space
                    bufferList[i].inputBuffer.writePointer += readBytes;

                    // If there are bytes in the buffer and the user has sent a LF at the end then process the buffer
                    if(bufferList[i].inputBuffer.writePointer > 1
                        && bufferList[i].inputBuffer.buffer[bufferList[i].inputBuffer.writePointer - 1] == '\n')
                    {
                        // Terminate the buffer
                        bufferList[i].inputBuffer.buffer[bufferList[i].inputBuffer.writePointer -1 ] = '\0';

                        // Interpret the message from the user, splitting it into an opcode and optionally some input
                        CRUST_MIXED_OPERATION_INPUT operationInput;
                        CRUST_OPCODE opcode = crust_daemon_interpret_message(bufferList[i].inputBuffer.buffer,
                                                                             &operationInput,
                                                                             state);

                        crust_daemon_process_opcode(opcode, &operationInput, state, pollList, bufferList, i, pollListLength);

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

                    // If no bytes can be written then stop and let the connection get re-polled
                    if(bytesWritten < 1)
                    {
                        break;
                    }

                    bufferList[i].currentWritePositionPointer += bytesWritten;

                    // If the write succeeds completely, drop it from the queue and try the next entry
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
#ifdef SYSTEMD
    sd_notify(0, "STATUS=CRUST Daemon starting up...");
#endif

    if(crustOptionSetGroup)
    {
        crust_terminal_print_verbose("Attempting to set process GID...");
        if(setgid(crustOptionTargetGroup))
        {
            crust_terminal_print("Unable to set process GID, continuing with default");
        }
    }

    crust_terminal_print_verbose("Creating CRUST socket...");

    // Request a socket from the kernel
    socketFp = socket(PF_INET, SOCK_STREAM, 0);
    if(socketFp == -1)
    {
        crust_terminal_print("Failed to create CRUST socket.");
        exit(EXIT_FAILURE);
    }

    // Declare a structure to hold the socket address
    struct sockaddr_in address;

    // Empty the structure
    memset(&address, '\0', sizeof(struct sockaddr_in));

    // Fill the structure
    address.sin_family = AF_INET;
    address.sin_port = htons(crustOptionPort);
    address.sin_addr.s_addr = crustOptionIPAddress;
#ifdef MACOS
    address.sin_len = sizeof(struct sockaddr_in);
#endif

    // Allow the socket to re-use an address from a previous instance of CRUST
    int yes = 1;
    if(setsockopt(socketFp, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof(yes)))
    {
        crust_terminal_print("Failed to enable address reuse on the socket.");
        exit(EXIT_FAILURE);
    }

    // Bind to the interface
    if(bind(socketFp, (struct sockaddr *) &address, sizeof(address)) == -1)
    {
        if(errno == EACCES)
        {
            crust_terminal_print("Unable to bind to interface - permission denied. ");
            exit(EXIT_FAILURE);
        }
        crust_terminal_print("Failed to bind to interface.");
        exit(EXIT_FAILURE);
    }

    if(crustOptionSetUser)
    {
        crust_terminal_print_verbose("Setting process UID...");
        if(setuid(crustOptionTargetUser))
        {
            crust_terminal_print("Unable to set process UID");
            exit(EXIT_FAILURE);
        }
    }

    // Register the signal handlers
    signal(SIGINT, crust_daemon_handle_signal);
    signal(SIGTERM, crust_daemon_handle_signal);

    // Make the socket non-blocking
    if(fcntl(socketFp, F_SETFD, fcntl(socketFp, F_GETFD) | O_NONBLOCK) == -1)
    {
        crust_terminal_print("Unable to make the CRUST socket non-blocking.");
        exit(EXIT_FAILURE);
    }

    crust_terminal_print_verbose("Building initial state...");

    CRUST_STATE * state;
    crust_state_init(&state);

    // Start accepting connections
    if(listen(socketFp, CRUST_SOCKET_QUEUE_LIMIT))
    {
        crust_terminal_print("Failed to enable listening on the CRUST socket.");
        exit(EXIT_FAILURE);
    }

#ifdef SYSTEMD
    sd_notify(0, "READY=1\n"
                 "STATUS=CRUST Daemon running");
#endif

    crust_daemon_loop(state);
}