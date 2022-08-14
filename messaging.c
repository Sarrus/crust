//
// Created by Michael Bell on 04/08/2022.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "messaging.h"
#include "terminal.h"

#define CRUST_PRINT_BUFFER_SIZE_INCREMENT 65536

/*
 * Takes a pointer to a CRUST message and the length of the message and returns the detected opcode. If there is an input
 * to go with the operation, fills operationInput. See CRUST_MIXED_OPERATION_INPUT for details. If the opcode is not
 * recognised or there is an error, returns NO_OPERATION.
 */
CRUST_OPCODE crust_interpret_message(const char * message, unsigned int length, CRUST_MIXED_OPERATION_INPUT * operationInput)
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

size_t crust_print_block(CRUST_BLOCK * block, char * printBuffer) // printBuffer must point to CRUST_MAX_MESSAGE_LENGTH bytes
{
    char partBuffer[CRUST_MAX_MESSAGE_LENGTH];
    sprintf(printBuffer,"BL%i;", block->blockId);
    if(block->links[upMain] != NULL)
    {
        sprintf(partBuffer, "UM%i;", block->links[upMain]->blockId);
        strcat(printBuffer, partBuffer);
    }
    if(block->links[upBranching] != NULL)
    {
        sprintf(partBuffer, "UB%i;", block->links[upBranching]->blockId);
        strcat(printBuffer, partBuffer);
    }
    if(block->links[downMain] != NULL)
    {
        sprintf(partBuffer, "DM%i;", block->links[downMain]->blockId);
        strcat(printBuffer, partBuffer);
    }
    if(block->links[downBranching] != NULL)
    {
        sprintf(partBuffer, "DB%i;", block->links[downBranching]->blockId);
        strcat(printBuffer, partBuffer);
    }
    strcat(printBuffer, "\r\n");
    return strlen(printBuffer);
}

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
            if(dynamicPrintBuffer == NULL)
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