#!/bin/bash
set -e
sudo modprobe gpio-sim
mkdir -p configfs
sudo mount -t configfs none configfs
sudo mkdir configfs/gpio-sim/crust-tester
sudo mkdir configfs/gpio-sim/crust-tester/bank0
echo 16 | sudo tee configfs/gpio-sim/crust-tester/bank0/num_lines
echo 1 | sudo tee configfs/gpio-sim/crust-tester/live
sudo umount configfs
rmdir configfs
sudo chown -R `whoami` /sys/devices/platform/gpio-sim.0/
