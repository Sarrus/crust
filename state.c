#include <stdlib.h>
#include "state.h"

void crust_block_init(CRUST_BLOCK ** block)
{
    static unsigned int idAutoIncrement = 0;

    *block = malloc(sizeof(CRUST_BLOCK));

    (*block)->blockId = idAutoIncrement;
    idAutoIncrement++;

    (*block)->upMain = NULL;
    (*block)->upBranching = NULL;
    (*block)->downMain = NULL;
    (*block)->downBranching = NULL;
}

void crust_state_init(CRUST_STATE ** state)
{
    *state = malloc(sizeof(CRUST_STATE));
    crust_block_init(&(*state)->initialBlock);
}