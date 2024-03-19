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
#include <stdint.h>
#include "state.h"
#include "terminal.h"

#define CRUST_INDEX_SIZE_INCREMENT 100
#define CRUST_BLOCK_WALK_DEPTH_LIMIT 10

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

    (*block)->rearBerths = NULL;
    (*block)->numRearBerths = 0;
}

void crust_path_init(CRUST_PATH ** path)
{
    *path = malloc(sizeof(CRUST_PATH));
    if(*path == NULL)
    {
        crust_terminal_print("Memory allocation error");
        exit(EXIT_FAILURE);
    }
    (*path)->linkedBlocks = NULL;
    (*path)->numLinkedBlocks = 0;
}

void crust_path_destroy(CRUST_PATH * path)
{
    if(path == NULL)
    {
        return;
    }
    free(path->linkedBlocks);
    free(path);
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

void crust_remap_berths_block_walk(CRUST_BLOCK * block,
                                   CRUST_DIRECTION direction,
                                   CRUST_IDENTIFIER * numBlocks,
                                   CRUST_BLOCK *** foundBlocks,
                                   CRUST_PATH *** pathsToFoundBlocks,
                                   CRUST_IDENTIFIER depth)
{
    static CRUST_BLOCK * path[CRUST_BLOCK_WALK_DEPTH_LIMIT];
    path[depth] = block;
    depth++;

    // If we've hit the depth limit do nothing and return
    if(depth > CRUST_BLOCK_WALK_DEPTH_LIMIT)
    {
        return;
    }

    // If the block is a berth and we are not at the starting block then add it to the list and return
    if(depth > 1 && block->berth && block->berthDirection == direction)
    {
        // Check that the block is not already recorded (if there is a loop this will happen)
        for(CRUST_IDENTIFIER i = 0; i < *numBlocks; i++)
        {
            if((*foundBlocks)[i] == block)
            {
                return;
            }
        }
        (*numBlocks)++;
        *foundBlocks = realloc(*foundBlocks, sizeof(CRUST_BLOCK *) * *numBlocks);
        if(*foundBlocks == NULL)
        {
            crust_terminal_print("Memory allocation error");
            exit(EXIT_FAILURE);
        }
        (*foundBlocks)[(*numBlocks) - 1] = block;

        *pathsToFoundBlocks = realloc(*pathsToFoundBlocks, sizeof (CRUST_PATH *) * *numBlocks);
        crust_path_init(&(*pathsToFoundBlocks)[(*numBlocks) - 1]);
        (*pathsToFoundBlocks)[(*numBlocks) - 1]->numLinkedBlocks = depth;
        (*pathsToFoundBlocks)[(*numBlocks) - 1]->linkedBlocks = malloc(sizeof(CRUST_BLOCK *) * depth);
        if((*pathsToFoundBlocks)[(*numBlocks) - 1]->linkedBlocks == NULL)
        {
            crust_terminal_print("Memory allocation error");
            exit(EXIT_FAILURE);
        }
        for(CRUST_IDENTIFIER i = 0; i < depth; i++)
        {
            (*pathsToFoundBlocks)[(*numBlocks) - 1]->linkedBlocks[i] = path[i];
        }
        return;
    }

    // To calculate the UP rear berths you have to search DOWN
    switch(direction)
    {
        case DOWN:
            if(block->links[upMain] != NULL)
            {
                crust_remap_berths_block_walk(block->links[upMain], direction, numBlocks, foundBlocks, pathsToFoundBlocks, depth);
            }
            if(block->links[upBranching] != NULL)
            {
                crust_remap_berths_block_walk(block->links[upBranching], direction, numBlocks, foundBlocks, pathsToFoundBlocks, depth);
            }
            return;

        case UP:
            if(block->links[downMain] != NULL)
            {
                crust_remap_berths_block_walk(block->links[downMain], direction, numBlocks, foundBlocks, pathsToFoundBlocks, depth);
            }
            if(block->links[downBranching] != NULL)
            {
                crust_remap_berths_block_walk(block->links[downBranching], direction, numBlocks, foundBlocks, pathsToFoundBlocks, depth);
            }
            return;
    }
}

void crust_remap_berths(CRUST_DIRECTION direction, CRUST_STATE * state)
{
    for(CRUST_IDENTIFIER i = 0; i < state->blockIndexPointer; i++)
    {
        if(state->blockIndex[i]->berth && state->blockIndex[i]->berthDirection == direction)
        {
            free(state->blockIndex[i]->rearBerths);
            for(CRUST_IDENTIFIER j = 0; j < state->blockIndex[i]->numRearBerths; j++)
            {
                crust_path_destroy(state->blockIndex[i]->pathsToRearBerths[j]);
            }
            state->blockIndex[i]->rearBerths = NULL;
            state->blockIndex[i]->numRearBerths = 0;
            crust_remap_berths_block_walk(state->blockIndex[i],
                                          direction,
                                          &state->blockIndex[i]->numRearBerths,
                                          &state->blockIndex[i]->rearBerths,
                                          &state->blockIndex[i]->pathsToRearBerths,
                                          0);
        }
    }
}

bool crust_enable_berth(CRUST_BLOCK *block, CRUST_DIRECTION direction, CRUST_STATE * state)
{
    if(block->berth)
    {
        return false;
    }
    block->berth = true;
    block->berthDirection = direction;
    crust_remap_berths(direction, state);
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

bool crust_headcode_advance(CRUST_BLOCK * fromBlock, CRUST_BLOCK * toBlock)
{
    if(!fromBlock->berth)
    {
        return false;
    }

    if(!crust_interpose(toBlock, fromBlock->headcode))
    {
        return false;
    }

    return crust_interpose(fromBlock, CRUST_EMPTY_BERTH_HEADCODE);
}

size_t crust_headcode_auto_advance(CRUST_TRACK_CIRCUIT * occupiedTrackCircuit, CRUST_BLOCK *** affectedBlocks, CRUST_STATE * state)
{
    CRUST_BLOCK * rearBlock = NULL;
    CRUST_BLOCK * advancedBlock = NULL;
    CRUST_IDENTIFIER shortestPathFound = UINT32_MAX;

    // Go through the berths in the occupied circuit
    for(int i = 0; i < occupiedTrackCircuit->numBlocks; i++)
    {
        // Focus on the empty berths
        if(occupiedTrackCircuit->blocks[i]->berth && occupiedTrackCircuit->blocks[i]->headcode[0] == CRUST_EMPTY_BERTH_CHARACTER)
        {
            // Go through the berths in the rear of the empty berth (the berth in 'advance')
            for(int j = 0; j < occupiedTrackCircuit->blocks[i]->numRearBerths; j++)
            {
                // If there is a headcode on the berth that is movable
                if(occupiedTrackCircuit->blocks[i]->rearBerths[j]->headcode[0] != CRUST_EMPTY_BERTH_CHARACTER
                && occupiedTrackCircuit->blocks[i]->rearBerths[j]->headcode[0] != CRUST_STATIC_BERTH_CHARACTER)
                {
                    // Find the first occupied circuit in the path that isn't the newly occupied circuit
                    for(int k = 0; k < occupiedTrackCircuit->blocks[i]->pathsToRearBerths[j]->numLinkedBlocks; k++)
                    {
                        if(occupiedTrackCircuit->blocks[i]->pathsToRearBerths[j]->linkedBlocks[k]->trackCircuit->occupied
                        && occupiedTrackCircuit->blocks[i]->pathsToRearBerths[j]->linkedBlocks[k]->trackCircuit != occupiedTrackCircuit)
                        {
                            // If this path is shorter than the last one found then set it as the one we will use
                            if(shortestPathFound < k)
                            {
                                shortestPathFound = k;
                                rearBlock = occupiedTrackCircuit->blocks[i]->rearBerths[j];
                                advancedBlock = occupiedTrackCircuit->blocks[i];
                                break;
                            }
                            // If this path is the same length as the last one but the origin berth is now unoccupied then also set it
                            else if(shortestPathFound == k && !occupiedTrackCircuit->blocks[i]->rearBerths[j]->trackCircuit->occupied)
                            {
                                rearBlock = occupiedTrackCircuit->blocks[i]->rearBerths[j];
                                advancedBlock = occupiedTrackCircuit->blocks[i];
                                break;
                            }
                        }
                    }

                    // If this is the first headcode we have found then take it anyway, regardless of path length
                    // Also take it if we have a headcode with no path length but this one is in an unoccupied circuit
                    if(advancedBlock == NULL
                    || (shortestPathFound == UINT32_MAX && !occupiedTrackCircuit->blocks[i]->rearBerths[j]->trackCircuit->occupied))
                    {
                        rearBlock = occupiedTrackCircuit->blocks[i]->rearBerths[j];
                        advancedBlock = occupiedTrackCircuit->blocks[i];
                    }
                }
            }
        }
    }

    // If we've found an advancement to make then action it and output the blocks that have changed
    if(advancedBlock != NULL)
    {
        crust_headcode_advance(rearBlock, advancedBlock);

        *affectedBlocks = malloc(sizeof(CRUST_BLOCK *) * 2);
        (*affectedBlocks)[0] = rearBlock;
        (*affectedBlocks)[1] = advancedBlock;
        return 2;
    }

    return 0;
}