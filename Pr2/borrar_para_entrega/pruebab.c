#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define STATUS_SIZE 20
#define BUFFER_SIZE 500
int main() {

	int iodone_fp;
	unsigned int readIO;
	unsigned int writeIO;
	unsigned int buf;
	iodone_fp = open("/sys/block/sda/device/iodone_cnt", O_RDONLY);
	if (iodone_fp == -1) {
	    perror("open_port: Unable to open /dev");
	    return(-1);
	}

	read( iodone_fp, &buf, sizeof(buf) );
	printf("%u\n", &buf);

	close(iodone_fp);

	return 0;
}
