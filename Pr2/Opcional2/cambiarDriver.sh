#!/bin/bash

sudo rmmod usbhid
sudo modprobe usbhid quirks=0x20A0:0x41E5:0x0004
usb-devices | tail

cd ../ParteC/
./meterModulo.sh
cd ../Opcional2/
