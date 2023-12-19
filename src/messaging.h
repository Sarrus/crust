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
    OCCUPY_TRACK_CIRCUIT,
    ENABLE_BERTH
    INTERPOSE
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
