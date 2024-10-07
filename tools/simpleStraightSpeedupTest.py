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


if len(sys.argv) != 2:
    print("Must specify GPIO chip number")
    exit(1)

dwellTime = 2.0

while True:
    print(f"Speed: {dwellTime}")

    writeCycle(0, 3)
    writeCycle(1, 0)
    writeCycle(2, 1)
    writeCycle(3, 2)

    dwellTime = dwellTime * 0.9

