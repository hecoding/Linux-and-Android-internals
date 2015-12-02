#!/bin/bash

while true;
do
	for i in 34 56 13 22 76 1 2 34; do echo add $i > /proc/modlist ; done
done
