#!/bin/python3
# This script takes a character drawing of some track and converts it into a window layout
# file that can be loaded into CRUST


import sys

if len(sys.argv) < 2:
    print("File path required")
    exit(1)

inputFile = open(sys.argv[1], "r")
if not inputFile:
    print("Unable to open file")
    exit(1)

lines = inputFile.readlines()

print("# X,Y,Symbol")

y = 0

for line in lines:
    x = 0
    for character in line:
        if character not in " \r\n":
            print(x, y, character, sep=",")
        x += 1
    y += 1
