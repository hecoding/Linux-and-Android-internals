#!/bin/bash

for i in $(seq 1 100); do
	echo add $i > /proc/modlist

	sleep 0.5
done