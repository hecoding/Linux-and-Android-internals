#!/bin/bash

for i in $(seq 1 100); do
	echo remove $i > /proc/modlist

	cat /proc/modlist

	echo

	sleep 0.5
done