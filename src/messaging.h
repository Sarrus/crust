//
// Created by Michael Bell on 04/08/2022.
//

#ifndef CRUST_MESSAGING_H
#define CRUST_MESSAGING_H

#include <time.h>
#include "state.h"

#define CRUST_OPCODE enum crustOpcode
#define CRUST_INPUT_BUFFER struct crustInputBuffer
#define CRUST_DYNAMIC_PRINT_BUFFER struct crustDynamicPrintBuffer
#define CRUST_MIXED_OPERATION_INPUT union crustMixedOperationInput
#define CRUST_MAX_MESSAGE_LENGTH 256

enum crustOpcode {
    NO_OPERATION,
    RESEND_STATE,
#ifdef TESTING
    RESEND_LIPSUM,
#endif
    INSERT_BLOCK,
    INSERT_TRACK_CIRCUIT,
    START_LISTENING,
    CLEAR_TRACK_CIRCUIT,
    OCCUPY_TRACK_CIRCUIT
};

struct crustInputBuffer {
    char buffer[CRUST_MAX_MESSAGE_LENGTH];
    unsigned int writePointer;
};

struct crustDynamicPrintBuffer {
    char * buffer;
    unsigned long size;
    unsigned long pointer;
};

union crustMixedOperationInput
{
    CRUST_BLOCK * block;
    CRUST_TRACK_CIRCUIT * trackCircuit;
    CRUST_IDENTIFIER identifier;
};

int crust_interpret_identifier(char * message, CRUST_IDENTIFIER * identifier);
int crust_interpret_block(char * message, CRUST_BLOCK * block, CRUST_STATE * state);
int crust_interpret_track_circuit(char * message, CRUST_TRACK_CIRCUIT * trackCircuit, CRUST_STATE * state);
size_t crust_print_block(CRUST_BLOCK * block, char ** outBuffer);
size_t crust_print_track_circuit(CRUST_TRACK_CIRCUIT * trackCircuit, char ** outBuffer);
unsigned long crust_print_state(CRUST_STATE * state, char ** outBuffer);

#endif //CRUST_MESSAGING_H
