//
// Created by Michael Bell on 04/08/2022.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "messaging.h"
#include "terminal.h"

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
        // Check that:
        if(!errno // There was no error
            && conversionStopPoint != message // some numerals were read
            && readValue <= UINT32_MAX // The number isn't larger than tha maximum size of a block index
            && crust_block_get(readValue, &linkBlock, state)) // The referenced block exists
        {
            // Link to the existing block
            block->links[linkType] = linkBlock;
        }
        else
        {
            return 1;
        }

        if(*conversionStopPoint == '\0')
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

/*
 * Takes a pointer to a CRUST message and the length of the message and returns the detected opcode. If there is an input
 * to go with the operation, fills operationInput. See CRUST_MIXED_OPERATION_INPUT for details. If the opcode is not
 * recognised or there is an error, returns NO_OPERATION.
 */
CRUST_OPCODE crust_interpret_message(char * message, CRUST_MIXED_OPERATION_INPUT * operationInput, CRUST_STATE * state)
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
            break;

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
/*
 * Fills a buffer CRUST_MAX_MESSAGE_LENGTH bytes long with the properties of the specified block.
 * The buffer must already be allocated.
 */
size_t crust_print_block(CRUST_BLOCK * block, char * printBuffer) // printBuffer must point to
{
    char partBuffer[CRUST_MAX_MESSAGE_LENGTH];
    sprintf(printBuffer,"BL%i", block->blockId);
    for(int i = 0; i < CRUST_MAX_LINKS; i++)
    {
        if(block->links[i] != NULL)
        {
            sprintf(partBuffer, "%s%i", crustLinkDesignations[i], block->links[i]->blockId);
            strcat(printBuffer, partBuffer);
        }
    }
    // Current max length 58 bytes
    strcat(printBuffer, "\n");
    return strlen(printBuffer);
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
        crust_print_block(blockToPrint, lineBuffer);
        crust_dynamic_print_buffer_cat(&dynamicBuffer, lineBuffer);
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