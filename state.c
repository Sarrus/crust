#include <stdlib.h>
#include "state.h"
#include "terminal.h"

#define CRUST_BLOCK_INDEX_SIZE_INCREMENT 100

// Each type of link has an inversion. For example, if downMain of block A points to block B then upMain of block B must
// point to block A.
const CRUST_LINK_TYPE crustLinkInversions[] = {
        [upMain] = downMain,
        [upBranching] = downBranching,
        [downMain] = upMain,
        [downBranching] = upBranching
};

/*
 * Adds a block to the block index, enabling CRUST to locate it by its block ID which is allocated at the same time.
 * All blocks that form part of the live layout must be in the index.
 */
void crust_block_index_add(CRUST_BLOCK * block, CRUST_STATE * state)
{
    // Resize the index if we are running out of space.
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

    // Add the block to the index.
    state->blockIndex[state->blockIndexPointer] = block;
    block->blockId = state->blockIndexPointer;
    state->blockIndexPointer++;
}

/*
 * Allocates the memory for a new block and initialises it.
 */
void crust_block_init(CRUST_BLOCK ** block, CRUST_STATE * state)
{
    *block = malloc(sizeof(CRUST_BLOCK));

    for(int i = 0; i < CRUST_MAX_LINKS; i++)
    {
        (*block)->links[i] = NULL;
    }
}

/*
 * Takes a CRUST block with one or more links set (up and down main and branching)
 * and attempts to insert them into the CRUST layout. Returns 0 on success or:
 * 1: The block could not be inserted because it contains no links
 * 2: The block could not be inserted because a link already exists to it's target
 * */
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
        if(block->links[i] != NULL)
        {
            block->links[i]->links[crustLinkInversions[i]] = block;
        }
    }
    crust_block_index_add(block, state);
    return 0;
}

/*
 * Initialises a new crust state and creates block 0. Block 0 is created with no links. All other blocks in the state must
 * have at least one link.
 */
void crust_state_init(CRUST_STATE ** state)
{
    *state = malloc(sizeof(CRUST_STATE));
    (*state)->blockIndex = NULL;
    (*state)->blockIndexLength = 0;
    (*state)->blockIndexPointer = 0;
    crust_block_init(&(*state)->initialBlock, *state);
    crust_block_index_add((*state)->initialBlock, *state);
}

/*
 * Fills 'block' with an address of the block identified by blockId and returns true if the block exists, otherwise
 * returns false
 */
bool crust_block_get(unsigned int blockId, CRUST_BLOCK ** block, CRUST_STATE * state)
{
    if(blockId < state->blockIndexPointer)
    {
        *block = state->blockIndex[blockId];
        return true;
    }
    else
    {
        return false;
    }
}