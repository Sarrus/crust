//
// Created by Michael Bell on 04/08/2022.
//

#include "messaging.h"

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