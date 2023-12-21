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

#ifndef CRUST_STATE_H
#define CRUST_STATE_H

#include <stdbool.h>

#define CRUST_BLOCK struct crustBlock
#define CRUST_TRACK_CIRCUIT struct crustTrackCircuit
#define CRUST_STATE struct crustState
#define CRUST_LINK_TYPE enum crustLinkType
#define CRUST_IDENTIFIER u_int32_t
#define CRUST_MAX_LINKS 4
#define CRUST_HEADCODE_LENGTH 4
#define CRUST_EMPTY_BERTH_CHARACTER '_'

enum crustLinkType {
    upMain,
    upBranching,
    downMain,
    downBranching
};

struct crustBlock {
    CRUST_IDENTIFIER blockId;
    char * blockName;
    CRUST_BLOCK * links[CRUST_MAX_LINKS];
    CRUST_TRACK_CIRCUIT * trackCircuit;
    bool berth;
    char headcode[CRUST_HEADCODE_LENGTH + 1]; // +1 for trailing null
};

struct crustTrackCircuit {
    CRUST_IDENTIFIER trackCircuitId;
    CRUST_BLOCK ** blocks;
    CRUST_IDENTIFIER numBlocks;
    bool occupied;
};

struct crustState {
    CRUST_BLOCK * initialBlock;
    CRUST_BLOCK ** blockIndex;
    unsigned int blockIndexLength;
    unsigned int blockIndexPointer;
    CRUST_TRACK_CIRCUIT ** trackCircuitIndex;
    unsigned int trackCircuitIndexLength;
    unsigned int trackCircuitIndexPointer;
};

void crust_state_init(CRUST_STATE ** state);
bool crust_block_get(unsigned int blockId, CRUST_BLOCK ** block, CRUST_STATE * state);
bool crust_track_circuit_get(unsigned int trackCircuitId, CRUST_TRACK_CIRCUIT ** trackCircuit, CRUST_STATE * state);
void crust_block_init(CRUST_BLOCK ** block, CRUST_STATE * state);
void crust_track_circuit_init(CRUST_TRACK_CIRCUIT ** trackCircuit, CRUST_STATE * state);
int crust_block_insert(CRUST_BLOCK * block, CRUST_STATE * state);
int crust_track_circuit_insert(CRUST_TRACK_CIRCUIT * trackCircuit, CRUST_STATE * state);
bool crust_track_circuit_set_occupation(CRUST_TRACK_CIRCUIT * trackCircuit, bool occupied, CRUST_STATE * state);
void crust_enable_berth(CRUST_BLOCK * block);

#endif //CRUST_STATE_H
