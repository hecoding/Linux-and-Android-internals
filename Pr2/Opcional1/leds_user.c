#include <linux/errno.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#define __NR_ledctl 316

long ledctl(unsigned int leds) {
	return (long) syscall(__NR_ledctl, leds);
}

int main () {
	unsigned int mask = 0;
	unsigned long ret = 500000;
	while(1){
		mask = 0;
		ledctl(mask);
		usleep (ret);
		mask = 1;
		ledctl(mask);
		usleep (ret);
		mask = 3;
		ledctl(mask);
		usleep (ret);
		mask = 7;
		ledctl(mask);
		usleep (ret);
		mask = 6;
		ledctl(mask);
		usleep (ret);
		mask = 4;
		ledctl(mask);
		usleep (ret);
}
	return 0;
}
