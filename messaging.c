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

/*
 * Takes a text based description of a block and a pointer to an empty block then fills the empty block based on the description.
 * The block may then be inserted into the CRUST state with crust_block_insert().
 */
void crust_interpret_block(char * message, CRUST_BLOCK * block, CRUST_STATE * state)
{
    unsigned long long readValue;
    char * conversionStopPoint;
    CRUST_BLOCK * linkBlock;
    char * segment;

    // Go through each ; delimited part of the message
    while((segment = strsep(&message, CRUST_DELIMITERS)) != NULL)
    {
        // Check if the part begins with a link designation.
        for(int i = 0; i < CRUST_MAX_LINKS; i++)
        {
            if(!strncmp(crustLinkDesignations[i], segment, CRUST_OPCODE_LENGTH))
            {
                // Try and convert the characters after the designation into an unsigned long long
                errno = 0;
                conversionStopPoint = &segment[2];
                readValue = strtoull(&segment[2], &conversionStopPoint, 10);
                // Check that:
                if(!errno // There was no error
                    && *conversionStopPoint == '\0' // There was no data after the number
                    && readValue <= UINT32_MAX // The number isn't larger than tha maximum size of a block index
                    && crust_block_get(readValue, &linkBlock, state)) // The referenced block exists
                {
                    // Link to the existing block
                    block->links[i] = linkBlock;
                    break;
                }
                // TODO: report to the user if they have referenced a non-existing block
            }
        }
    }
}

void crust_interpret_track_circuit(char * message, CRUST_TRACK_CIRCUIT * trackCircuit, CRUST_STATE * state)
{
    unsigned long long readValue;
    char * conversionStopPoint;
    CRUST_BLOCK * memberBlock;
    char * segment;

    // Go through each ; delimited part of the message
    while((segment = strsep(&message, CRUST_DELIMITERS)) != NULL)
    {
        errno = 0;
        conversionStopPoint = segment;
        readValue = strtoull(segment, &conversionStopPoint, 10);
        // Check that:
        if(!errno // There was no error
           && *conversionStopPoint == '\0' // There was no data after the number
           && readValue <= UINT32_MAX // The number isn't larger than tha maximum size of an index
           && crust_block_get(readValue, &memberBlock, state)) // The referenced block exists
        {
            trackCircuit->blocks = realloc(trackCircuit->blocks, sizeof(CRUST_BLOCK *) * (trackCircuit->numBlocks + 1));
            trackCircuit->blocks[trackCircuit->numBlocks] = memberBlock;
            (trackCircuit->numBlocks)++;
        }
        // TODO: report to the user if they have referenced a non-existing block
    }
}

/*
 * Takes a pointer to a CRUST message and the length of the message and returns the detected opcode. If there is an input
 * to go with the operation, fills operationInput. See CRUST_MIXED_OPERATION_INPUT for details. If the opcode is not
 * recognised or there is an error, returns NO_OPERATION.
 */
CRUST_OPCODE crust_interpret_message(char * message, unsigned int length, CRUST_MIXED_OPERATION_INPUT * operationInput, CRUST_STATE * state)
{
    // Return NOP if the message is too short
    if(length < 2)
    {
        return NO_OPERATION;
    }

    switch(message[0])
    {
        case 'I':
            switch(message[1])
            {
                case 'B':
                    // Initialise a block and try to fill it.
                    crust_block_init(&operationInput->block, state);
                    crust_interpret_block(message, operationInput->block, state);
                    return INSERT_BLOCK;

                case 'C':
                    crust_track_circuit_init(&operationInput->trackCircuit, state);
                    crust_interpret_track_circuit(message, operationInput->trackCircuit, state);
                    return INSERT_TRACK_CIRCUIT;

                default:
                    return NO_OPERATION;
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
    sprintf(printBuffer,"BL%i;", block->blockId);
    for(int i = 0; i < CRUST_MAX_LINKS; i++)
    {
        if(block->links[i] != NULL)
        {
            sprintf(partBuffer, "%s%i;", crustLinkDesignations[i], block->links[i]->blockId);
            strcat(printBuffer, partBuffer);
        }
    }
    // Current max length 58 bytes
    strcat(printBuffer, "\r\n");
    return strlen(printBuffer);
}

/*
 * Creates a buffer containing the entire state as text, ready to be sent to listeners. A pointer to the text is placed
 * in dynamicPrintBuffer and the length of the text is returned.
 */
unsigned long crust_print_state(CRUST_STATE * state, char ** dynamicPrintBuffer)
{
    *dynamicPrintBuffer = NULL;
    unsigned long printBufferSize = 0;
    unsigned long printPointer = 0;
    char lineBuffer[CRUST_MAX_MESSAGE_LENGTH];

    // Look up every block in the index one by one
    CRUST_BLOCK * blockToPrint;
    unsigned int blockToPrintId = 0;
    while(crust_block_get(blockToPrintId, &blockToPrint, state))
    {
        // If there is less free space in the buffer than the maximum size of a message then resize the buffer
        if((printBufferSize - printPointer) < CRUST_MAX_MESSAGE_LENGTH)
        {
            printBufferSize += CRUST_PRINT_BUFFER_SIZE_INCREMENT;
            *dynamicPrintBuffer = realloc(*dynamicPrintBuffer, printBufferSize);
            if(*dynamicPrintBuffer == NULL)
            {
                crust_terminal_print("Memory allocation failure when resizing print buffer");
                exit(EXIT_FAILURE);
            }
        }

        // Fill the line buffer with the details of the block, then add the line buffer to the end of the print buffer.
        size_t linePrintLength = crust_print_block(blockToPrint, lineBuffer);
        memcpy(*dynamicPrintBuffer + printPointer, lineBuffer, linePrintLength);
        printPointer += linePrintLength;
        blockToPrintId++;
    }

    return printPointer;
}