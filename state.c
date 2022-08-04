#include <stdlib.h>
#include "state.h"
#include "terminal.h"

#define CRUST_BLOCK_INDEX_SIZE_INCREMENT 100

void crust_block_index_add(CRUST_BLOCK * block, CRUST_STATE * state)
{
    if(state->blockIndexPointer >= state->blockIndexLength)
    {
        state->blockIndexLength += CRUST_BLOCK_INDEX_SIZE_INCREMENT;
        state->blockIndex = realloc(state->blockIndex, state->blockIndexLength);
        if(state->blockIndex == NULL)
        {
            crust_terminal_print("Failed to grow block index.");
            exit(EXIT_FAILURE);
        }
    }

    state->blockIndex[state->blockIndexPointer] = block;
    block->blockId = state->blockIndexPointer;
    state->blockIndexPointer++;
}

void crust_block_init(CRUST_BLOCK ** block, CRUST_STATE * state)
{
    *block = malloc(sizeof(CRUST_BLOCK));

    (*block)->upMain = NULL;
    (*block)->upBranching = NULL;
    (*block)->downMain = NULL;
    (*block)->downBranching = NULL;

    crust_block_index_add(*block, state);
}

void crust_state_init(CRUST_STATE ** state)
{
    *state = malloc(sizeof(CRUST_STATE));
    crust_block_init(&(*state)->initialBlock, *state);
    (*state)->blockIndex = NULL;
    (*state)->blockIndexLength = 0;
    (*state)->blockIndexPointer = 0;
}