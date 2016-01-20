#!/bin/bash

sudo rmmod modtimer
echo lsmod:
lsmod | head -n 2
echo dmesg:
dmesg | tail -n 1
make clean 1&>/dev/null
