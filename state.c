#include <stdlib.h>
#include "state.h"
#include "terminal.h"

#define CRUST_BLOCK_INDEX_SIZE_INCREMENT 100

const CRUST_LINK_TYPE crustLinkInversions[] = {
        [upMain] = downMain,
        [upBranching] = downBranching,
        [downMain] = upMain,
        [downBranching] = upBranching
};

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

    for(int i = 0; i < CRUST_MAX_LINKS; i++)
    {
        (*block)->links[i] = NULL;
    }

    crust_block_index_add(*block, state);
}

/* Takes a CRUST block with one or more links set (up and down main and branching)
 * and attempts to insert them into the CRUST layout. Returns 0 on success or:
 * 1: The block could not be inserted because it contains no links
 * 2: The block could not be inserted because a link already exists to its target */
int crust_block_insert(CRUST_BLOCK * block, CRUST_STATE * state)
{
    unsigned int linkCount = 0;
    for(int i = 0; i < CRUST_MAX_LINKS; i++)
    {
        if(block->links[i] != NULL)
        {
            linkCount++;
            if(block->links[i]->links[crustLinkInversions[i]] != NULL)
            {
                return 2;
            }
        }
    }

    if(!linkCount)
    {
        return 1;
    }

    for(int i = 0; i < CRUST_MAX_LINKS; i++)
    {
        block->links[i]->links[crustLinkInversions[i]] = block;
    }

    return 0;
}

void crust_state_init(CRUST_STATE ** state)
{
    *state = malloc(sizeof(CRUST_STATE));
    crust_block_init(&(*state)->initialBlock, *state);
    (*state)->blockIndex = NULL;
    (*state)->blockIndexLength = 0;
    (*state)->blockIndexPointer = 0;
}