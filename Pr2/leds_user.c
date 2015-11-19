#include <linux/errno.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#define __NR_ledctl 316

long ledctl(unsigned int leds) {
	return (long) syscall(__NR_ledctl, leds);
}

int main () {
	unsigned int mask = 0;
	while(1){
		ledctl(mask);
		sleep ( 1 );
		mask = 3;
		ledctl(mask);
		sleep ( 1 );
		mask = 7;
		ledctl(mask);
		sleep ( 1 );
		mask = 6;
		ledctl(mask);
		sleep ( 1 );
		mask = 4;
		ledctl(mask);
		sleep ( 1 );
		mask = 0;
		ledctl(mask);
		sleep ( 1 );	
}
	return 0;
}
