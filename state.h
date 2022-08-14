#ifndef CRUST_STATE_H
#define CRUST_STATE_H

#include <stdbool.h>

#define CRUST_BLOCK struct crustBlock
#define CRUST_STATE struct crustState
#define CRUST_LINK_TYPE enum crustLinkType
#define CRUST_MAX_LINKS 4

enum crustLinkType {
    upMain,
    upBranching,
    downMain,
    downBranching
};

struct crustBlock {
    unsigned int blockId;
    CRUST_BLOCK * links[CRUST_MAX_LINKS];
};

struct crustState {
    CRUST_BLOCK * initialBlock;
    CRUST_BLOCK ** blockIndex;
    unsigned int blockIndexLength;
    unsigned int blockIndexPointer;
};

void crust_state_init(CRUST_STATE ** state);
bool crust_block_get(unsigned int blockId, CRUST_BLOCK ** block, CRUST_STATE * state);

#endif //CRUST_STATE_H
