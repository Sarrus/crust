#!/bin/python3

import time
import sys
def pinWrite(pinNo, state):
    pin = open(f"/sys/devices/platform/gpio-sim.0/gpiochip{sys.argv[1]}/sim_gpio{pinNo}/pull", "w")
    pin.write("pull-" + state)
    pin.close()

def writeCycle(onPin, offPin):
    pinWrite(onPin, "up")
    time.sleep(dwellTime / 2)

    pinWrite(offPin, "down")
    time.sleep(dwellTime)

def messyWrite(pinOne, pinTwo, direction):
    writes = 100
    while writes:
        if direction:
            pinWrite(pinOne, "up")
            time.sleep(0.01)
            pinWrite(pinTwo, "down")
            time.sleep(0.01)
        writes =  writes - 1

if len(sys.argv) != 2:
    print("Must specify GPIO chip number")
    exit(1)

dwellTime = 0.1

while True:
    input("Press enter for a quick write:")

    pinWrite(1, "up")
    time.sleep(dwellTime)
    pinWrite(2, "down")

    input("Press enter for a quick write:")

    pinWrite(1, "down")
    time.sleep(dwellTime)
    pinWrite(2, "up")