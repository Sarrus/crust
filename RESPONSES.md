# CRUST Responses

These are the outputs that the CRUST daemon can provide.

## BLock
```
BL[block number][link designator][link number]:[friendly name]
```
A block can have up to four link designators and link numbers.

### Example
```
BL2UM1DM0:HX15
```
Block number 2 has it's up main connected to block 1, it's 
down main connected to block 0 and the friendly name 'HX15'

## Track Circuit
```
TC[track circuit number]:[block number]/[block number][occupation state]
```

A track circuit contains at least one block. Additional blocks are 
separated by `/`.

Blocks are either occupied (`OC`) or clear (`CL`).

### Examples
```
TC2:5CL
```
Track circuit 2 contains block 5 and is clear.
```
TC9:2/4/8OC
```
Track circuit 9 contains blocks 2, 4 and 8 and is occupied.