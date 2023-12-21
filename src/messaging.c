/******************************************************************************
 * Consolidated, Realtime Updates on Status of Trains (CRUST)
 * Copyright (C) 2022-2023 Michael R. Bell <michael@black-dragon.io>
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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "messaging.h"
#include "terminal.h"
#include "options.h"

#define CRUST_PRINT_BUFFER_SIZE_INCREMENT 65536
#define CRUST_DELIMITERS ";"
#define CRUST_OPCODE_LENGTH 2

/*
 * Note: when adding print functions, take care to avoid buffer overruns by calculating the maximum possible length of the
 * message you are printing. Make sure you know the maximum possible length of each variable you are including in the
 * message.
 */

// Each of the four types of link a block can have has a two letter designation.
const char * crustLinkDesignations[] = {
        [upMain] = "UM",
        [upBranching] = "UB",
        [downMain] = "DM",
        [downBranching] = "DB"
};

void crust_dynamic_print_buffer_init(CRUST_DYNAMIC_PRINT_BUFFER ** dynamicPrintBuffer)
{
    *dynamicPrintBuffer = malloc(sizeof(CRUST_DYNAMIC_PRINT_BUFFER));
    (*dynamicPrintBuffer)->pointer = 0;
    (*dynamicPrintBuffer)->size = CRUST_PRINT_BUFFER_SIZE_INCREMENT;
    (*dynamicPrintBuffer)->buffer = malloc(sizeof(char) * (*dynamicPrintBuffer)->size);
}

void crust_dynamic_print_buffer_cat(CRUST_DYNAMIC_PRINT_BUFFER ** dst, char * src)
{
    size_t srcLength = strlen(src) + 1; // Include the null terminator in the length
    // If there is less free space in the buffer than the length of the message then resize the buffer
    if(((*dst)->size - (*dst)->pointer) < srcLength)
    {
        (*dst)->size += CRUST_PRINT_BUFFER_SIZE_INCREMENT;
        (*dst)->buffer = realloc((*dst)->buffer, (*dst)->size);
        if((*dst)->buffer == NULL)
        {
            crust_terminal_print("Memory allocation failure when resizing print buffer");
            exit(EXIT_FAILURE);
        }
    }

    memcpy((*dst)->buffer + (*dst)->pointer, src, srcLength);
    (*dst)->pointer += srcLength - 1; // Overwrite the null terminator
}

/*
 * Takes a text based description of a block and a pointer to an empty block then fills the empty block based on the description.
 * The block may then be inserted into the CRUST state with crust_block_insert(). Returns 0 if the description is valid, otherwise
 * returns 1
 */
int crust_interpret_block(char * message, CRUST_BLOCK * block, CRUST_STATE * state)
{
    bool linkSeen[4] = {false, false, false, false};

    while(1)
    {
        CRUST_LINK_TYPE linkType;

        if(strlen(message) < 3)
        {
            return 1;
        }

        // Figure out the link type the user has specified
        switch(message[0])
        {
            case 'U':
                switch(message[1])
                {
                    case 'M':
                        linkType = upMain;
                        break;

                    case 'B':
                        linkType = upBranching;
                        break;

                    default:
                        return 1;
                }
                break;

            case 'D':
                switch(message[1])
                {
                    case 'M':
                        linkType = downMain;
                    break;

                    case 'B':
                        linkType = downBranching;
                    break;

                    default:
                        return 1;
                }
                break;

            default:
                return 1;
        }

        if(linkSeen[linkType])
        {
            // same link specified twice
            return 1;
        }

        linkSeen[linkType] = true;

        // Skip past the link designation characters
        message = &message[2];

        char * conversionStopPoint = "";
        errno = 0;
        CRUST_BLOCK * linkBlock;
        unsigned long long readValue = strtoull(message, &conversionStopPoint, 10);
        if(errno // There was an error
            || conversionStopPoint == message // no numerals were read
            || readValue > UINT32_MAX) // The number is larger than tha maximum size of a block index
        {
            // Reject the block because it's invalid
            return 1;
        }
        else if(crust_block_get(readValue, &linkBlock, state)) // The referenced block exists
        {
            // Link to the existing block
            block->links[linkType] = linkBlock;
        }
        else if(crustOptionRunMode == CRUST_RUN_MODE_DAEMON)
        {
            // Reject the block if we are in daemon mode. (Links that go nowhere are acceptable in other modes.)
            return 1;
        }

        if(*conversionStopPoint == ':')
        {
            conversionStopPoint++;
            if(*conversionStopPoint == '\0')
            {
                return 1;
            }
            block->blockName = malloc(strlen(conversionStopPoint) + 1); // +1 to include the null byte
            strcpy(block->blockName, conversionStopPoint);
            return 0;
        }
        else if(*conversionStopPoint == '\0')
        {
            return 0;
        }

        // Skip to the next designation
        message = conversionStopPoint;
    }
}

int crust_interpret_track_circuit(char * message, CRUST_TRACK_CIRCUIT * trackCircuit, CRUST_STATE * state)
{
    while(1)
    {
        char *conversionStopPoint = "";
        errno = 0;
        CRUST_BLOCK *memberBlock;
        unsigned long long readValue = strtoull(message, &conversionStopPoint, 10);
        if (!errno // There was no error
            && conversionStopPoint != message // some numerals were read
            && readValue <= UINT32_MAX // The number isn't larger than tha maximum size of a block index
            && crust_block_get(readValue, &memberBlock, state)) // The referenced block exists
        {
            trackCircuit->blocks = realloc(trackCircuit->blocks, sizeof(CRUST_BLOCK *) * (trackCircuit->numBlocks + 1));
            if (trackCircuit->blocks == NULL) {
                crust_terminal_print("Memory allocation error");
                exit(EXIT_FAILURE);
            }
            trackCircuit->blocks[trackCircuit->numBlocks] = memberBlock;
            (trackCircuit->numBlocks)++;
        }
        else
        {
            return 1;
        }

        if(*conversionStopPoint == '\0')
        {
            return 0;
        }

        if(*conversionStopPoint != '/')
        {
            return 1;
        }

        message = &conversionStopPoint[1];
    }
}

int crust_interpret_identifier(char * message, CRUST_IDENTIFIER * identifier)
{
    errno = 0;
    char * conversionStopPoint = "";
    unsigned long long readValue = strtoull(message, &conversionStopPoint, 10);
    if(!errno // There was no error
       && conversionStopPoint != message // Some numerals were read
       && readValue <= UINT32_MAX // The value was less than the maximum
       && *conversionStopPoint == '\0' ) // We read to the end
    {
        *identifier = readValue;
        return 0;
    }
    else
    {
        return 1;
    }
}

int crust_interpret_interpose_instruction(char * message, CRUST_INTERPOSE_INSTRUCTION * interposeInstruction)
{
    errno = 0;
    char * conversionStopPoint = "";
    unsigned long long readValue = strtoull(message, &conversionStopPoint, 10);
    if(!errno // There was no error
       && conversionStopPoint != message // Some numerals were read
       && readValue <= UINT32_MAX // The value was less than the maximum
       && *conversionStopPoint == '/' ) // The end was a '/'
    {
        interposeInstruction->blockID = readValue;
    }
    else
    {
        return 1;
    }

    size_t headcodeLength = strlen(&conversionStopPoint[1]);
    if(headcodeLength != CRUST_HEADCODE_LENGTH)
    {
        return 2;
    }

    for(int i = 0; i < CRUST_HEADCODE_LENGTH; i++)
    {
        int readPos = i + 1;
        if(
                (conversionStopPoint[readPos] < 'A'
                || conversionStopPoint[readPos] > 'Z')
                && conversionStopPoint[readPos] != '_'
                && conversionStopPoint[readPos] != '*'
                )
        {
            return 3;
        }

        interposeInstruction->headcode[i] = conversionStopPoint[readPos];
    }

    return 0;
}

size_t crust_print_block(CRUST_BLOCK * block, char ** outBuffer)
{
    CRUST_DYNAMIC_PRINT_BUFFER * dynamicBuffer;
    crust_dynamic_print_buffer_init(&dynamicBuffer);

    char partBuffer[CRUST_MAX_MESSAGE_LENGTH];
    char * dynamicPartBuffer;

    sprintf(partBuffer,"BL%i", block->blockId);
    crust_dynamic_print_buffer_cat(&dynamicBuffer, partBuffer);
    for(int i = 0; i < CRUST_MAX_LINKS; i++)
    {
        if(block->links[i] != NULL)
        {
            sprintf(partBuffer, "%s%i", crustLinkDesignations[i], block->links[i]->blockId);
            crust_dynamic_print_buffer_cat(&dynamicBuffer, partBuffer);
        }
    }

    if(block->berth)
    {
        crust_dynamic_print_buffer_cat(&dynamicBuffer, "/");
        crust_dynamic_print_buffer_cat(&dynamicBuffer, block->headcode);
    }

    asprintf(&dynamicPartBuffer, ":%s\n", block->blockName);
    crust_dynamic_print_buffer_cat(&dynamicBuffer, dynamicPartBuffer);
    free(dynamicPartBuffer);

    *outBuffer = dynamicBuffer->buffer;
    size_t finalLength = dynamicBuffer->pointer;
    free(dynamicBuffer);
    return finalLength;
}

size_t crust_print_track_circuit(CRUST_TRACK_CIRCUIT * trackCircuit, char ** outBuffer)
{
    CRUST_DYNAMIC_PRINT_BUFFER * dynamicBuffer;
    crust_dynamic_print_buffer_init(&dynamicBuffer);

    char chunkBuffer[CRUST_MAX_MESSAGE_LENGTH];

    sprintf(chunkBuffer, "TC%i:", trackCircuit->trackCircuitId);
    crust_dynamic_print_buffer_cat(&dynamicBuffer, chunkBuffer);

    for(u_int32_t i = 0; i < trackCircuit->numBlocks; i++)
    {
        if(i)
        {
            crust_dynamic_print_buffer_cat(&dynamicBuffer, "/");
        }
        sprintf(chunkBuffer, "%i", trackCircuit->blocks[i]->blockId);
        crust_dynamic_print_buffer_cat(&dynamicBuffer, chunkBuffer);
    }

    if(trackCircuit->occupied)
    {
        crust_dynamic_print_buffer_cat(&dynamicBuffer, "OC\r\n");
    }
    else
    {
        crust_dynamic_print_buffer_cat(&dynamicBuffer, "CL\r\n");
    }

    *outBuffer = dynamicBuffer->buffer;
    size_t finalLength = dynamicBuffer->pointer;
    free(dynamicBuffer);
    return finalLength;
}

/*
 * Creates a buffer containing the entire state as text, ready to be sent to listeners. A pointer to the text is placed
 * in outBuffer and the length of the text is returned.
 */
unsigned long crust_print_state(CRUST_STATE * state, char ** outBuffer)
{
    CRUST_DYNAMIC_PRINT_BUFFER * dynamicBuffer;
    crust_dynamic_print_buffer_init(&dynamicBuffer);

    char lineBuffer[CRUST_MAX_MESSAGE_LENGTH];
    char * subDynamicBuffer;

    // Look up every block in the index one by one
    CRUST_BLOCK * blockToPrint;
    unsigned int blockToPrintId = 0;
    while(crust_block_get(blockToPrintId, &blockToPrint, state))
    {
        // Fill the line buffer with the details of the block, then add the line buffer to the end of the print buffer.
        crust_print_block(blockToPrint, &subDynamicBuffer);
        crust_dynamic_print_buffer_cat(&dynamicBuffer, subDynamicBuffer);
        free(subDynamicBuffer);
        blockToPrintId++;
    }

    // Do the same with track circuits
    CRUST_TRACK_CIRCUIT * trackCircuitToPrint;
    unsigned int trackCircuitToPrintId = 0;
    while(crust_track_circuit_get(trackCircuitToPrintId, &trackCircuitToPrint, state))
    {
        crust_print_track_circuit(trackCircuitToPrint, &subDynamicBuffer);
        crust_dynamic_print_buffer_cat(&dynamicBuffer, subDynamicBuffer);
        free(subDynamicBuffer);
        trackCircuitToPrintId++;
    }

    *outBuffer = dynamicBuffer->buffer;
    size_t finalLength = dynamicBuffer->pointer;
    free(dynamicBuffer);
    return finalLength;
}