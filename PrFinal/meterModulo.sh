#!/bin/bash

make clean 1&>/dev/null
make
echo
echo ---------------------------------------------
echo
sudo insmod modlist.ko
echo dmesg:
dmesg | tail -n 1
echo lsmod:
lsmod | head -n 2
