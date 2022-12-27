#ifndef CRUST_STATE_H
#define CRUST_STATE_H

#include <stdbool.h>

#define CRUST_BLOCK struct crustBlock
#define CRUST_TRACK_CIRCUIT struct crustTrackCircuit
#define CRUST_STATE struct crustState
#define CRUST_LINK_TYPE enum crustLinkType
#define CRUST_MAX_LINKS 4

enum crustLinkType {
    upMain,
    upBranching,
    downMain,
    downBranching
};

/*
 * It is important that the crust block ID is a predictable length to prevent buffer overruns when printing the details
 * of blocks. It should always be a 32 bit unsigned int so that when it is converted to decimal, it has a maximum of
 * 10 digits
 */
struct crustBlock {
    u_int32_t blockId;
    CRUST_BLOCK * links[CRUST_MAX_LINKS];
    CRUST_TRACK_CIRCUIT * trackCircuit;
};

struct crustTrackCircuit {
    u_int32_t trackCircuitId;
    CRUST_BLOCK ** blocks;
    u_int32_t numBlocks;
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
void crust_block_init(CRUST_BLOCK ** block, CRUST_STATE * state);
void crust_track_circuit_init(CRUST_TRACK_CIRCUIT ** trackCircuit, CRUST_STATE * state);
int crust_block_insert(CRUST_BLOCK * block, CRUST_STATE * state);
int crust_track_circuit_insert(CRUST_TRACK_CIRCUIT * trackCircuit, CRUST_STATE * state);

#endif //CRUST_STATE_H
