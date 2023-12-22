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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "state.h"
#include "terminal.h"

#define CRUST_INDEX_SIZE_INCREMENT 100

// Each type of link has an inversion. For example, if downMain of block A points to block B then upMain of block B must
// point to block A.
const CRUST_LINK_TYPE crustLinkInversions[] = {
        [upMain] = downMain,
        [upBranching] = downBranching,
        [downMain] = upMain,
        [downBranching] = upBranching
};

void crust_index_regrow(void ** index, unsigned int * indexLength, const unsigned int * indexPointer, size_t entrySize)
{
    if(*indexPointer >= *indexLength)
    {
        *indexLength += CRUST_INDEX_SIZE_INCREMENT;
        *index = realloc(*index, *indexLength * entrySize);
        if(*index == NULL)
        {
            crust_terminal_print("Failed to grow an index.");
            exit(EXIT_FAILURE);
        }
    }
}

/*
 * Adds a block to the block index, enabling CRUST to locate it by its block ID which is allocated at the same time.
 * All blocks that form part of the live layout must be in the index.
 */
int crust_block_index_add(CRUST_BLOCK * block, CRUST_STATE * state)
{
    bool generateName = false;
    CRUST_IDENTIFIER potentialName;
    if(block->blockName == NULL)
    {
        generateName = true;
        potentialName = state->blockIndexPointer;
        asprintf(&block->blockName, "%i", potentialName);
    }

    for(;;)
    {
        int i = 0;
        while(i < state->blockIndexPointer)
        {
            if(!strcmp(block->blockName, state->blockIndex[i]->blockName))
            {
                if(generateName)
                {
                    potentialName++;
                    asprintf(&block->blockName, "%i", potentialName);
                    break;
                }
                else
                {
                    return 1;
                }
            }
            i++;
        }
        if(i == state->blockIndexPointer)
        {
            break;
        }
    }

    // Resize the index if we are running out of space.
    crust_index_regrow((void **) &state->blockIndex, &state->blockIndexLength, &state->blockIndexPointer, sizeof(CRUST_BLOCK *));

    // Add the block to the index.
    state->blockIndex[state->blockIndexPointer] = block;
    block->blockId = state->blockIndexPointer;
    state->blockIndexPointer++;

    return 0;
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

    (*block)->trackCircuit = NULL;
    (*block)->blockName = NULL;
    (*block)->berth = false;

    for(int i = 0; i < CRUST_HEADCODE_LENGTH; i++)
    {
        (*block)->headcode[i] = CRUST_EMPTY_BERTH_CHARACTER;
    }

    (*block)->headcode[CRUST_HEADCODE_LENGTH] = '\0';
}

void crust_track_circuit_index_add(CRUST_TRACK_CIRCUIT * trackCircuit, CRUST_STATE * state)
{
    crust_index_regrow((void **) &state->trackCircuitIndex, &state->trackCircuitIndexLength, &state->trackCircuitIndexPointer, sizeof(CRUST_TRACK_CIRCUIT *));

    state->trackCircuitIndex[state->trackCircuitIndexPointer] = trackCircuit;
    trackCircuit->trackCircuitId = state->trackCircuitIndexPointer;
    state->trackCircuitIndexPointer++;
}

void crust_track_circuit_init(CRUST_TRACK_CIRCUIT ** trackCircuit, CRUST_STATE * state)
{
    *trackCircuit = malloc(sizeof(CRUST_TRACK_CIRCUIT));
    (*trackCircuit)->blocks = NULL;
    (*trackCircuit)->numBlocks = 0;
    (*trackCircuit)->occupied = true; // Track circuits always start out occupied
}

/*
 * Takes a CRUST block with one or more links set (up and down main and branching)
 * and attempts to insert them into the CRUST layout. Returns 0 on success or:
 * 1: The block could not be inserted because it's name was not unique
 * 2: The block could not be inserted because a link already exists to it's target
 * 3: The block could not be inserted because it contains no links
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
        return 3;
    }

    if(crust_block_index_add(block, state))
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

    return 0;
}

/*
 * Takes a CRUST track circuit containing one or more blocks and attempts to insert it, returns 0 on success or:
 * 1: The track circuit references no blocks
 * 2: One or more of the referenced blocks are part of a different track circuit
 */
int crust_track_circuit_insert(CRUST_TRACK_CIRCUIT * trackCircuit, CRUST_STATE * state)
{
    // Check that at least one block is referenced
    if(!trackCircuit->numBlocks)
    {
        return 1;
    }

    // Check that all the referenced blocks are not already part of a track circuit
    for(u_int32_t i = 0; i < trackCircuit->numBlocks; i++)
    {
        if(trackCircuit->blocks[i]->trackCircuit != NULL)
        {
            return 2;
        }
    }

    // Update the referenced blocks with the new track circuit
    for(u_int32_t i = 0; i < trackCircuit->numBlocks; i++)
    {
        trackCircuit->blocks[i]->trackCircuit = trackCircuit;
    }
    // Add the track circuit to the index
    crust_track_circuit_index_add(trackCircuit, state);

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
    (*state)->trackCircuitIndex = NULL;
    (*state)->trackCircuitIndexLength = 0;
    (*state)->trackCircuitIndexPointer = 0;
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

bool crust_track_circuit_get(unsigned int trackCircuitId, CRUST_TRACK_CIRCUIT ** trackCircuit, CRUST_STATE * state)
{
    if(trackCircuitId < state->trackCircuitIndexPointer)
    {
        *trackCircuit = state->trackCircuitIndex[trackCircuitId];
        return true;
    }
    else
    {
        return false;
    }
}

// Sets the occupation state of a track circuit. Returns true if the occupation has changed, false otherwise.
bool crust_track_circuit_set_occupation(CRUST_TRACK_CIRCUIT * trackCircuit, bool occupied, CRUST_STATE * state)
{
    if(trackCircuit->occupied == occupied)
    {
        return false;
    }

    trackCircuit->occupied = occupied;
    return true;
}

void crust_enable_berth(CRUST_BLOCK * block)
{
    block->berth = true;
}

bool crust_interpose(CRUST_BLOCK * block, char * headcode)
{
    if(!block->berth)
    {
        return false;
    }

    for(int i = 0; i < CRUST_HEADCODE_LENGTH; i++)
    {
        block->headcode[i] = headcode[i];
    }
    block->headcode[CRUST_HEADCODE_LENGTH] = '\0';

    return true;
}