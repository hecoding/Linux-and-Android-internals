#!/bin/bash

for i in $(seq 1 100); do
	echo remove $i > /proc/modlist

	sleep 0.3
done