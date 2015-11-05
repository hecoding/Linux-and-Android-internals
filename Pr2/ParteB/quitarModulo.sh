#!/bin/bash

sudo rmmod modleds
lsmod | head -n 2
dmesg | tail -n 1
make clean
