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
#include "config.h"
#include "messaging.h"
#include "connectivity.h"
#ifdef SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#define CRUST_WRITE struct crustWrite
#define CRUST_BUFFER_LIST_ENTRY struct crustBufferListEntry
#define CRUST_MAX_WRITE_QUEUE_LENGTH 5

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

CRUST_SESSION ** daemonSessionList = NULL;
size_t daemonSessionListLength = 0;

CRUST_CONNECTION * daemonSocket;

CRUST_STATE * state;

_Noreturn void crust_daemon_stop()
{
#ifdef SYSTEMD
    sd_notify(0, "STOPPING=1\n"
                 "STATUS=CRUST Daemon shutting down...");
#endif
//    crust_terminal_print_verbose("Closing the CRUST socket...");
//    close(socketFp);

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

void crust_daemon_session_init(CRUST_SESSION * session)
{
    session->connection = NULL;
    session->listening = false;
    session->closed = false;
    session->ownsCircuits = false;
}

void crust_daemon_session_list_extend()
{
    daemonSessionListLength++;
    daemonSessionList = realloc(daemonSessionList, sizeof(CRUST_SESSION *) * daemonSessionListLength);
    if(daemonSessionList == NULL)
    {
        crust_terminal_print("Memory allocation error.");
        exit(EXIT_FAILURE);
    }
    daemonSessionList[daemonSessionListLength - 1] = malloc(sizeof(CRUST_SESSION));
    crust_daemon_session_init(daemonSessionList[daemonSessionListLength - 1]);
}

void crust_write_to_listeners(char * message)
{
    for(int i = 0; i < daemonSessionListLength; i++)
    {
        if(daemonSessionList[i]->listening && !(daemonSessionList[i]->closed))
        {
            crust_connection_write(daemonSessionList[i]->connection, message);
        }
    }
}

/*
 * Takes a pointer to a CRUST message and the length of the message and returns the detected opcode. If there is an input
 * to go with the operation, fills operationInput. See CRUST_MIXED_OPERATION_INPUT for details. If the opcode is not
 * recognised or there is an error, returns NO_OPERATION.
 */
CRUST_OPCODE crust_daemon_interpret_message(char * message, CRUST_MIXED_OPERATION_INPUT * operationInput)
{
    // Return NOP if the message is too short
    if(strlen(message) < 2)
    {
        return NO_OPERATION;
    }

    switch(message[0])
    {
        case 'B':
            switch(message[1])
            {
                case 'S':
                    operationInput->manualStepInstruction = malloc(sizeof(CRUST_BERTH_STEP_INSTRUCTION));
                    if(crust_interpret_berth_step_instruction(&message[2], operationInput->manualStepInstruction))
                    {
                        crust_terminal_print_verbose("Invalid manual step instruction");
                        free(operationInput->manualStepInstruction);
                        return NO_OPERATION;
                    }
                    return BERTH_STEP;
            }

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

void crust_daemon_process_opcode(CRUST_OPCODE opcode, CRUST_MIXED_OPERATION_INPUT * operationInput, CRUST_SESSION * session)
{
    CRUST_WRITE * write;
    CRUST_TRACK_CIRCUIT * identifiedTrackCircuit;
    CRUST_BLOCK * sourceBlock;
    CRUST_BLOCK * targetBlock;
    CRUST_BLOCK ** affectedBlocks = NULL;
    size_t affectedBlockCount = 0;
    char * writeBuffer;

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
                    crust_print_block(operationInput->block, &writeBuffer);
                    crust_write_to_listeners(writeBuffer);
                    free(writeBuffer);
                    writeBuffer = NULL;
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
                    crust_print_track_circuit(operationInput->trackCircuit, &writeBuffer);
                    crust_write_to_listeners(writeBuffer);
                    free(writeBuffer);
                    writeBuffer = NULL;
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
            if(session == NULL) break;
            crust_terminal_print_verbose("OPCODE: Resend State");
            crust_print_state(state, &writeBuffer);
            crust_connection_write(session->connection, writeBuffer);
            free(writeBuffer);
            writeBuffer = NULL;
            break;

            // Send the state then send updates as it changes.
        case START_LISTENING:
            if(session == NULL) break;
            crust_terminal_print_verbose("OPCODE: Start Listening");
            crust_print_state(state, &writeBuffer);
            crust_connection_write(session->connection, writeBuffer);
            free(writeBuffer);
            writeBuffer = NULL;
            session->listening = true;
            break;

        case CLEAR_TRACK_CIRCUIT:
            if(session == NULL) break;
            crust_terminal_print_verbose("OPCODE: Clear Track Circuit");
            if(crust_track_circuit_get(operationInput->identifier, &identifiedTrackCircuit, state)
               && crust_track_circuit_set_occupation(identifiedTrackCircuit, false, state, session))
            {
                crust_print_track_circuit(identifiedTrackCircuit, &writeBuffer);
                crust_write_to_listeners(writeBuffer);
                free(writeBuffer);
                writeBuffer = NULL;
            }
            break;

        case OCCUPY_TRACK_CIRCUIT:
            if(session == NULL) break;
            crust_terminal_print_verbose("OPCODE: Occupy Track Circuit");
            if(crust_track_circuit_get(operationInput->identifier, &identifiedTrackCircuit, state)
               && crust_track_circuit_set_occupation(identifiedTrackCircuit, true, state, session))
            {
                crust_print_track_circuit(identifiedTrackCircuit, &writeBuffer);
                crust_write_to_listeners(writeBuffer);
                free(writeBuffer);
                writeBuffer = NULL;
                affectedBlockCount = crust_headcode_auto_advance(identifiedTrackCircuit, &affectedBlocks, state);
                for(int i = 0; i < affectedBlockCount; i++)
                {
                    crust_print_block(affectedBlocks[i], &writeBuffer);
                    crust_write_to_listeners(writeBuffer);
                    free(writeBuffer);
                    writeBuffer = NULL;
                }
                free(affectedBlocks);
                affectedBlocks = NULL;
                affectedBlockCount = 0;
            }
            break;

        case ENABLE_BERTH_UP:
            crust_terminal_print_verbose("OPCODE: Enable Berth UP");
            if(crust_block_get(operationInput->identifier, &targetBlock, state)
                && crust_enable_berth(targetBlock, UP, state))
            {
                crust_print_block(targetBlock, &writeBuffer);
                crust_write_to_listeners(writeBuffer);
                free(writeBuffer);
                writeBuffer = NULL;
            }
            break;

        case ENABLE_BERTH_DOWN:
            crust_terminal_print_verbose("OPCODE: Enable Berth DOWN");
            if(crust_block_get(operationInput->identifier, &targetBlock, state)
               && crust_enable_berth(targetBlock, DOWN, state))
            {
                crust_print_block(targetBlock, &writeBuffer);
                crust_write_to_listeners(writeBuffer);
                free(writeBuffer);
                writeBuffer = NULL;
            }
            break;

        case INTERPOSE:
            crust_terminal_print_verbose("OPCODE: Interpose");
            if(!crust_block_get(operationInput->interposeInstruction->blockID, &targetBlock, state))
            {
                crust_terminal_print_verbose("Invalid block");
                break;
            }
            if(!crust_interpose(targetBlock, operationInput->interposeInstruction->headcode))
            {
                crust_terminal_print_verbose("Block is not a berth");
                break;
            }

            crust_print_block(targetBlock, &writeBuffer);
            crust_write_to_listeners(writeBuffer);
            free(writeBuffer);
            writeBuffer = NULL;
            break;

        case BERTH_STEP:
            crust_terminal_print_verbose("OPCODE: Berth Step");
            if(!crust_block_get(operationInput->manualStepInstruction->sourceBlockID, &sourceBlock, state))
            {
                crust_terminal_print_verbose("Invalid source block");
            }
            if(!crust_block_get(operationInput->manualStepInstruction->destinationBlockID, &targetBlock, state))
            {
                crust_terminal_print_verbose("Invalid destination block");
            }
            if(!crust_headcode_advance(sourceBlock, targetBlock))
            {
                crust_terminal_print_verbose("Failed to step headcode");
            }

            crust_print_block(sourceBlock, &writeBuffer);
            crust_write_to_listeners(writeBuffer);
            free(writeBuffer);
            writeBuffer = NULL;

            crust_print_block(targetBlock, &writeBuffer);
            crust_write_to_listeners(writeBuffer);
            free(writeBuffer);
            writeBuffer = NULL;
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

void crust_daemon_read_config()
{
    char line[CRUST_MAX_MESSAGE_LENGTH];

    FILE * configFile = fopen(crustOptionDaemonConfigFilePath, "r");
    if(configFile == NULL)
    {
        crust_terminal_print("Failed to open config file.");
        exit(EXIT_FAILURE);
    }
    while(fgets(line, CRUST_MAX_MESSAGE_LENGTH, configFile) != NULL)
    {
        for(int i = 0; i < CRUST_MAX_MESSAGE_LENGTH; i++)
        {
            if(line[i] == '\r' || line[i] == '\n')
            {
                line[i] = '\0';
                break;
            }
        }

        CRUST_MIXED_OPERATION_INPUT operationInput;
        CRUST_OPCODE opcode = crust_daemon_interpret_message(line, &operationInput);
        if(opcode == NO_OPERATION)
        {
            crust_terminal_print("Invalid initial config.");
            exit(EXIT_FAILURE);
        }
        crust_daemon_process_opcode(opcode, &operationInput, NULL);
    }
}

void crust_daemon_handle_socket_connection(CRUST_CONNECTION * connection)
{
    crust_terminal_print_verbose("New client connection accepted.");
    crust_daemon_session_list_extend();
    daemonSessionList[daemonSessionListLength - 1]->connection = connection;
    connection->customIdentifier = (long long)daemonSessionListLength - 1;
}

void crust_daemon_handle_read(CRUST_CONNECTION * connection)
{
    size_t readBufferLength = strlen(connection->readBuffer);
    char * instructionStart = connection->readBuffer;
    for(size_t i = 0; i < readBufferLength; i++)
    {
        if(connection->readBuffer[i] == '\n')
        {
            connection->readBuffer[i] = '\0';
            CRUST_MIXED_OPERATION_INPUT operationInput;
            CRUST_OPCODE opcode = crust_daemon_interpret_message(instructionStart, &operationInput);
            crust_daemon_process_opcode(opcode, &operationInput, daemonSessionList[connection->customIdentifier]);
            connection->readTo = i;
            instructionStart = &connection->readBuffer[i + 1];
        }
    }
}

void crust_daemon_handle_close(CRUST_CONNECTION * connection)
{
    char * writeBuffer;

    crust_terminal_print_verbose("Client connection closed.");
    CRUST_SESSION * session = daemonSessionList[connection->customIdentifier];
    session->closed = true;
    session->connection = NULL;
    if(session->ownsCircuits)
    {
        for(unsigned int i = 0; i < state->trackCircuitIndexPointer; i++)
        {
            if(state->trackCircuitIndex[i]->owningSession == session)
            {
                state->trackCircuitIndex[i]->owningSession = NULL;
                crust_print_track_circuit(state->trackCircuitIndex[i], &writeBuffer);
                crust_write_to_listeners(writeBuffer);
                free(writeBuffer);
            }
        }
    }
}

_Noreturn void crust_daemon_loop()
{
    for(;;)
    {
        crust_connectivity_execute(-1);
    }
}

// Starts the CRUST daemon, exiting when the daemon finishes.
_Noreturn void crust_daemon_run()
{
    char statusText[CRUST_MAX_MESSAGE_LENGTH];

    crust_terminal_print_verbose("CRUST daemon starting...");
#ifdef SYSTEMD
    sd_notify(0, "STATUS=CRUST Daemon starting up...");
#endif
    // Set the connection limit
    struct rlimit connectionRLimit;

    if(crustOptionConnectionLimit)
    {
        connectionRLimit.rlim_cur = connectionRLimit.rlim_max = crustOptionConnectionLimit;
    }
    else
    {
        getrlimit(RLIMIT_NOFILE, &connectionRLimit);
        snprintf(statusText, CRUST_MAX_MESSAGE_LENGTH - 1, "System defined connection limit: %lu (unprivileged maximum: %lu)",
                 connectionRLimit.rlim_cur,
                 connectionRLimit.rlim_max);
        crustOptionConnectionLimit = connectionRLimit.rlim_max = connectionRLimit.rlim_cur;
        crust_terminal_print_verbose(statusText);
    }

    if(setrlimit(RLIMIT_NOFILE, &connectionRLimit) == -1)
    {
        crust_terminal_print("Failed to set connection limit.");
        if(getuid())
        {
            crust_terminal_print("Try starting the daemon as root.");
        }
        exit(EXIT_FAILURE);
    }

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

    crust_terminal_print_verbose("Building initial state...");


    crust_state_init(&state);

    if(crustOptionDaemonConfigFilePath[0] != '\0')
    {
        crust_terminal_print_verbose("Reading config...");
        crust_daemon_read_config();
    }

    crust_terminal_print_verbose("Creating CRUST socket...");
    daemonSocket = crust_connection_socket_open(crust_daemon_handle_read,
                                                crust_daemon_handle_socket_connection,
                                                crust_daemon_handle_close,
                                                crustOptionIPAddress,
                                                crustOptionPort);

#ifdef SYSTEMD
    sd_notify(0, "READY=1\n"
                 "STATUS=CRUST Daemon running");
#endif

    crust_daemon_loop();
}