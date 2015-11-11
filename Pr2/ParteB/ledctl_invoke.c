#include <linux/errno.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <stdio.h>
#define __NR_gettid 316

long ledctl(void) {
	return (long) syscall(__NR_ledctl);
}

int main (void) {
	printf("El código de retorno de la llamada ledctl es %ld\n",
	ledctl());
	return 0;
}
