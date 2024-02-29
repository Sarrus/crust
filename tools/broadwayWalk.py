#!/bin/python3
# Simulates a train entering Broadway (GWSR) station, running round and then departing.
# To use, initialise a CRUST daemon with broadwayInit.txt, run enableTestGPIO.sh and start a node pointing to the test
# GPIO chip that the script creates. You can then run this script which will loop endlessly until stopped.

import time
import sys


def pinWrite(pinNo, state):
    pin = open(f"/sys/devices/platform/gpio-sim.0/gpiochip{sys.argv[1]}/sim_gpio{pinNo}/pull", "w")
    pin.write("pull-" + state)
    pin.close()


if len(sys.argv) != 2:
    print("Must specify GPIO chip number")
    exit(1)

pinWrite(0, "down")
pinWrite(1, "down")
pinWrite(2, "down")
pinWrite(3, "down")
pinWrite(4, "down")
pinWrite(5, "down")
pinWrite(6, "down")
pinWrite(7, "down")
pinWrite(8, "down")
pinWrite(9, "down")

while True:
    time.sleep(30)
    pinWrite(9, "up")
    time.sleep(10)
    pinWrite(8, "up")
    time.sleep(2)
    pinWrite(9, "down")
    time.sleep(10)
    pinWrite(7, "up")
    time.sleep(2)
    pinWrite(8, "down")
    time.sleep(10)
    pinWrite(6, "up")
    time.sleep(2)
    pinWrite(7, "down")
    time.sleep(10)
    pinWrite(5, "up")
    time.sleep(2)
    pinWrite(6, "down")
    time.sleep(10)
    pinWrite(4, "up")
    time.sleep(2)
    pinWrite(2, "up")
    time.sleep(2)
    pinWrite(5, "down")
    time.sleep(2)
    pinWrite(4, "down")
    time.sleep(30)
    pinWrite(1, "up")
    time.sleep(5)
    pinWrite(0, "up")
    time.sleep(5)
    pinWrite(1, "down")
    time.sleep(5)
    pinWrite(1, "up")
    time.sleep(5)
    pinWrite(0, "down")
    time.sleep(5)
    pinWrite(3, "up")
    time.sleep(5)
    pinWrite(1, "down")
    time.sleep(10)
    pinWrite(4, "up")
    time.sleep(5)
    pinWrite(3, "down")
    time.sleep(5)
    pinWrite(5, "up")
    time.sleep(5)
    pinWrite(4, "down")
    time.sleep(5)
    pinWrite(4, "up")
    time.sleep(5)
    pinWrite(5, "down")
    time.sleep(5)
    pinWrite(4, "down")
    time.sleep(30)
    pinWrite(4, "up")
    time.sleep(2)
    pinWrite(5, "up")
    time.sleep(2)
    pinWrite(2, "down")
    time.sleep(2)
    pinWrite(4, "down")
    time.sleep(10)
    pinWrite(6, "up")
    time.sleep(2)
    pinWrite(5, "down")
    time.sleep(10)
    pinWrite(7, "up")
    time.sleep(2)
    pinWrite(6, "down")
    time.sleep(10)
    pinWrite(8, "up")
    time.sleep(2)
    pinWrite(7, "down")
    time.sleep(10)
    pinWrite(9, "up")
    time.sleep(2)
    pinWrite(8, "down")
    time.sleep(10)
    pinWrite(9, "down")


