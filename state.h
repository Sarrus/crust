#ifndef CRUST_STATE_H
#define CRUST_STATE_H

#define CRUST_BLOCK struct crustBlock
#define CRUST_STATE struct crustState

struct crustBlock {
    unsigned int blockId;
    CRUST_BLOCK * upMain;
    CRUST_BLOCK * upBranching;
    CRUST_BLOCK * downMain;
    CRUST_BLOCK * downBranching;
};


struct crustState {
    CRUST_BLOCK * initialBlock;
    CRUST_BLOCK ** blockIndex;
    unsigned int blockIndexLength;
    unsigned int blockIndexPointer;
};

void crust_state_init(CRUST_STATE ** state);

#endif //CRUST_STATE_H
