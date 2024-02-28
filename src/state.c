/******************************************************************************
 * Consolidated, Realtime Updates on Status of Trains (CRUST)
 * Copyright (C) 2022-2024 Michael R. Bell <michael@black-dragon.io>
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

    (*block)->berthDirection = CRUST_DEFAULT_DIRECTION;
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
    (*trackCircuit)->upEdgeBlocks = NULL;
    (*trackCircuit)->numUpEdgeBlocks = 0;
    (*trackCircuit)->downEdgeBlocks = NULL;
    (*trackCircuit)->numDownEdgeBlocks = 0;
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
    if(state->circuitsInserted)
    {
        return 4;
    }

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

    // Check that the referenced blocks are all connected together and find the
    // edge blocks
    for(u_int32_t blockInCircuit = 0; blockInCircuit < trackCircuit->numBlocks; blockInCircuit++)
    {
        bool linkedToCircuit = false;
        bool upEdgeBlock = false;
        bool downEdgeBlock = false;
        for(int link = 0; link < CRUST_MAX_LINKS; link++)
        {
            if(trackCircuit->blocks[blockInCircuit]->links[link] != NULL)
            {
                bool linkMatchFound = false;
                for(u_int32_t otherBlockInCircuit = 0; otherBlockInCircuit < trackCircuit->numBlocks; otherBlockInCircuit++)
                {
                    if(trackCircuit->blocks[blockInCircuit]->links[link] == trackCircuit->blocks[otherBlockInCircuit])
                    {
                        linkedToCircuit = true;
                        linkMatchFound = true;
                    }
                }
                if(!linkMatchFound)
                {
                    if(!upEdgeBlock
                    && (link == upMain
                            || link == upBranching))
                    {
                        (trackCircuit->numUpEdgeBlocks)++;
                        trackCircuit->upEdgeBlocks = realloc(trackCircuit->upEdgeBlocks, sizeof(CRUST_BLOCK *) * trackCircuit->numUpEdgeBlocks);
                        if(trackCircuit->upEdgeBlocks == NULL)
                        {
                            crust_terminal_print("Memory allocation error.");
                            exit(EXIT_FAILURE);
                        }
                        trackCircuit->upEdgeBlocks[trackCircuit->numUpEdgeBlocks - 1] = trackCircuit->blocks[blockInCircuit];
                        upEdgeBlock = true;
                    }
                    else if(!downEdgeBlock
                    && (link == downMain
                            || link == downBranching))
                    {
                        (trackCircuit->numDownEdgeBlocks)++;
                        trackCircuit->downEdgeBlocks = realloc(trackCircuit->downEdgeBlocks, sizeof(CRUST_BLOCK *) * trackCircuit->numDownEdgeBlocks);
                        if(trackCircuit->downEdgeBlocks == NULL)
                        {
                            crust_terminal_print("Memory allocation error.");
                            exit(EXIT_FAILURE);
                        }
                        trackCircuit->downEdgeBlocks[trackCircuit->numDownEdgeBlocks - 1] = trackCircuit->blocks[blockInCircuit];
                        downEdgeBlock = true;
                    }
                }
            }
        }
        if(trackCircuit->numBlocks != 1 && !linkedToCircuit)
        {
            free(trackCircuit->upEdgeBlocks);
            trackCircuit->upEdgeBlocks = NULL;
            trackCircuit->numUpEdgeBlocks = 0;
            free(trackCircuit->downEdgeBlocks);
            trackCircuit->downEdgeBlocks = NULL;
            trackCircuit->numDownEdgeBlocks = 0;
            return 3;
        }
    }

    // Update the referenced blocks with the new track circuit
    for(u_int32_t i = 0; i < trackCircuit->numBlocks; i++)
    {
        trackCircuit->blocks[i]->trackCircuit = trackCircuit;
    }
    // Add the track circuit to the index
    crust_track_circuit_index_add(trackCircuit, state);

    // Record that we have started inserting circuits
    state->circuitsInserted = true;

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
    (*state)->circuitsInserted = false;
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

bool crust_enable_berth(CRUST_BLOCK * block, CRUST_DIRECTION direction)
{
    if(block->berth)
    {
        return false;
    }
    block->berth = true;
    block->berthDirection = direction;
    return true;
}

bool crust_interpose(CRUST_BLOCK * block, const char * headcode)
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

size_t crust_headcode_auto_advance(CRUST_TRACK_CIRCUIT * occupiedTrackCircuit, CRUST_BLOCK *** affectedBlocks, CRUST_STATE * state)
{
    CRUST_BLOCK * rearBlock = NULL;
    CRUST_BLOCK * advancedBlock = NULL;
    // Find the interconnected track circuits
    // Examine the occupied ones
    // If only one of the occupied circuits contains a headcode oriented the right way then pull it
    for(int i = 0; i < occupiedTrackCircuit->numUpEdgeBlocks; i++)
    {
        if(occupiedTrackCircuit->upEdgeBlocks[i]->links[upMain] != NULL
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upMain]->trackCircuit != NULL
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upMain]->trackCircuit->occupied == true
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upMain]->berth == true
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upMain]->berthDirection == DOWN
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upMain]->headcode[0] != '_')
        {
            if(rearBlock != NULL)
            {
                // Two headcodes found
                return 0;
            }
            rearBlock = occupiedTrackCircuit->upEdgeBlocks[i]->links[upMain];
        }

        if(occupiedTrackCircuit->upEdgeBlocks[i]->links[upBranching] != NULL
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upBranching]->trackCircuit != NULL
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upBranching]->trackCircuit->occupied == true
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upBranching]->berth == true
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upBranching]->berthDirection == DOWN
            && occupiedTrackCircuit->upEdgeBlocks[i]->links[upBranching]->headcode[0] != '_')
        {
            if(rearBlock != NULL)
            {
                // Two headcodes found
                return 0;
            }
            rearBlock = occupiedTrackCircuit->upEdgeBlocks[i]->links[upBranching];
        }
    }

    for(int i = 0; i < occupiedTrackCircuit->numDownEdgeBlocks; i++)
    {
        if(occupiedTrackCircuit->downEdgeBlocks[i]->links[downMain] != NULL
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downMain]->trackCircuit != NULL
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downMain]->trackCircuit->occupied == true
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downMain]->berth == true
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downMain]->berthDirection == UP
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downMain]->headcode[0] != '_')
        {
            if(rearBlock != NULL)
            {
                // Two headcodes found
                return 0;
            }
            rearBlock = occupiedTrackCircuit->downEdgeBlocks[i]->links[downMain];
        }

        if(occupiedTrackCircuit->downEdgeBlocks[i]->links[downBranching] != NULL
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downBranching]->trackCircuit != NULL
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downBranching]->trackCircuit->occupied == true
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downBranching]->berth == true
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downBranching]->berthDirection == UP
           && occupiedTrackCircuit->downEdgeBlocks[i]->links[downBranching]->headcode[0] != '_')
        {
            if(rearBlock != NULL)
            {
                // Two headcodes found
                return 0;
            }
            rearBlock = occupiedTrackCircuit->downEdgeBlocks[i]->links[downBranching];
        }
    }

    if(rearBlock == NULL)
    {
        return 0;
    }
    else if(rearBlock->berthDirection == UP)
    {
        for(int i = 0; i < occupiedTrackCircuit->numUpEdgeBlocks; i++)
        {
            if(occupiedTrackCircuit->upEdgeBlocks[i]->berth == true
                && occupiedTrackCircuit->upEdgeBlocks[i]->berthDirection == UP)
            {
                if(occupiedTrackCircuit->upEdgeBlocks[i]->headcode[0] == '_')
                {
                    advancedBlock = occupiedTrackCircuit->upEdgeBlocks[i];
                    for(int j = 0; j < CRUST_HEADCODE_LENGTH; j++)
                    {
                        advancedBlock->headcode[j] = rearBlock->headcode[j];
                        rearBlock->headcode[j] = '_';
                    }

                    *affectedBlocks = malloc(sizeof(CRUST_BLOCK *) * 2);
                    (*affectedBlocks)[0] = rearBlock;
                    (*affectedBlocks)[1] = advancedBlock;
                    return 2;
                }
            }
        }
    }
    else if(rearBlock->berthDirection == DOWN)
    {
        for(int i = 0; i < occupiedTrackCircuit->numDownEdgeBlocks; i++)
        {
            if(occupiedTrackCircuit->downEdgeBlocks[i]->berth == true
               && occupiedTrackCircuit->downEdgeBlocks[i]->berthDirection == DOWN)
            {
                if(occupiedTrackCircuit->downEdgeBlocks[i]->headcode[0] == '_')
                {
                    advancedBlock = occupiedTrackCircuit->downEdgeBlocks[i];
                    for(int j = 0; j < CRUST_HEADCODE_LENGTH; j++)
                    {
                        advancedBlock->headcode[j] = rearBlock->headcode[j];
                        rearBlock->headcode[j] = '_';
                    }

                    *affectedBlocks = malloc(sizeof(CRUST_BLOCK *) * 2);
                    (*affectedBlocks)[0] = rearBlock;
                    (*affectedBlocks)[1] = advancedBlock;
                    return 2;
                }
            }
        }
    }

    return 0;
}