#include <linux/errno.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#define __NR_ledctl 316

long ledctl(unsigned int leds) {
	return (long) syscall(__NR_ledctl, leds);
}

int main (int argc, char *argv[]) {
	if(argc == 2)
		ledctl(strtoul(argv[1], NULL, 16)); //strtoul convierte una cadena en un entero largo en base 16
	else
		printf("Usage: ./ledctl_invoke <ledmask>\n");
	return 0;
}
