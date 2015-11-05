#!/bin/bash

make clean
make
sudo insmod modleds.ko
dmesg | tail -n 1
lsmod | head -n 2
