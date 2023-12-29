# CRUST Commands

These are the commands that the CRUST server understands:

## Resend State
```
RS
```
Causes CRUST to return the current state. That is, all the 
objects that CRUST is aware of. The output is accurate at 
the moment that CRUST begins to return data. If the state
changes while CRUST is returning it, the output will remain
the same.

## Start Listening
```
SL
```
Causes CRUST to return the current state and then to provide
updates as the state changes. If a state change takes place
while CRUST is providing an update, the change will be queued 
and delivered when the client is ready.

## Insert Block
```
IB[link designator][link number]:[friendly name]
```
The friendly name is optional. CRUST will use the block ID 
if no friendly name is specified.

You must specify at least one link designator and link 
number. You may specify up to three link designators and
link numbers.

If the insert was successful, CRUST will write the new
block to all listeners. Otherwise CRUST will do nothing.

### Examples:
```
IBDM3
```
Inserts a block with it's down main link connected to block
three.
```
IBDM3DB5:HN7
```
Inserts a block with the friendly name 'HN7' with it's down 
main link connected to block 3 and it's down branch link 
connected to block 5

## Insert track Circuit
```
IC[block number]/[block number]/[block number]
```
Inserts a new track circuit containing one or more blocks.
You must specify at least one block. 

The first block is specified immediately after `IC`. Additional
blocks are separated by `/`.

### Examples
```
IC3
```
Inserts a new track circuit containing block 3.
```
IC4/7/9
```
Inserts a new track circuit containing blocks 4, 7 and 9.

## Occupy Circuit
```
OC[track circuit number]
```
Sets a track circuit to occupied.

## Clear Circuit
```
CC[track circuit number]
```
Sets a track circuit to cleared.

## Enable berth (Up / Down)
```
EU[block number]
ED[block number]
```
Sets the specified block to function as a berth. A block that
is designated as a berth can hold a headcode. You can only enable
a block to function as a berth in one direction. To berth in both
directions, create two blocks joined together and enable both of them.