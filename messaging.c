//
// Created by Michael Bell on 04/08/2022.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
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

const char * crustLinkDesignations[] = {
        [upMain] = "UM",
        [upBranching] = "UB",
        [downMain] = "DM",
        [downBranching] = "DB"
};

void crust_interpret_block(char * message, CRUST_BLOCK * block, CRUST_STATE * state)
{
    unsigned long long readValue;
    char * conversionStopPoint;
    CRUST_BLOCK * linkBlock;
    char * segment;

    while((segment = strsep(&message, CRUST_DELIMITERS)) != NULL)
    {
        for(int i = 0; i < CRUST_MAX_LINKS; i++)
        {
            if(!strncmp(crustLinkDesignations[i], segment, CRUST_OPCODE_LENGTH))
            {
                errno = 0;
                conversionStopPoint = &segment[2];
                readValue = strtoull(&segment[2], &conversionStopPoint, 10);
                if(!errno
                    && *conversionStopPoint == '\0'
                    && readValue <= UINT32_MAX
                    && crust_block_get(readValue, &linkBlock, state))
                {
                    block->links[i] = linkBlock;
                    break;
                }
            }
        }
    }
}

/*
 * Takes a pointer to a CRUST message and the length of the message and returns the detected opcode. If there is an input
 * to go with the operation, fills operationInput. See CRUST_MIXED_OPERATION_INPUT for details. If the opcode is not
 * recognised or there is an error, returns NO_OPERATION.
 */
CRUST_OPCODE crust_interpret_message(char * message, unsigned int length, CRUST_MIXED_OPERATION_INPUT * operationInput, CRUST_STATE * state)
{
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
                    crust_block_init(&operationInput->block, state);
                    crust_interpret_block(message, operationInput->block, state);
                    return INSERT_BLOCK;

                default:
                    return NO_OPERATION;
            }

        case 'R':
            switch(message[1])
            {
                case 'S':
                    return RESEND_STATE;

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

    CRUST_BLOCK * blockToPrint;
    unsigned int blockToPrintId = 0;
    while(crust_block_get(blockToPrintId, &blockToPrint, state))
    {
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

        size_t linePrintLength = crust_print_block(blockToPrint, lineBuffer);
        memcpy(*dynamicPrintBuffer + printPointer, lineBuffer, linePrintLength);
        printPointer += linePrintLength;
        blockToPrintId++;
    }

    return printPointer;
}