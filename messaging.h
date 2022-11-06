//
// Created by Michael Bell on 04/08/2022.
//

#ifndef CRUST_MESSAGING_H
#define CRUST_MESSAGING_H

#include <time.h>
#include "state.h"

#define CRUST_OPCODE enum crustOpcode
#define CRUST_INPUT_BUFFER struct crustInputBuffer
#define CRUST_MIXED_OPERATION_INPUT union crustMixedOperationInput
#define CRUST_MAX_MESSAGE_LENGTH 256

enum crustOpcode {
    NO_OPERATION,
    RESEND_STATE,
#ifdef TESTING
    RESEND_LIPSUM,
#endif
    INSERT_BLOCK
};

struct crustInputBuffer {
    char buffer[CRUST_MAX_MESSAGE_LENGTH];
    unsigned int writePointer;
};

union crustMixedOperationInput
{
    CRUST_BLOCK * block;
};

CRUST_OPCODE crust_interpret_message(char * message, unsigned int length, CRUST_MIXED_OPERATION_INPUT * operationInput, CRUST_STATE * state);
unsigned long crust_print_state(CRUST_STATE * state, char ** dynamicPrintBuffer);

#endif //CRUST_MESSAGING_H