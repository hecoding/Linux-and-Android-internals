#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define STATUS_SIZE 20
int main() {

	FILE *stat_fp;
	char status[STATUS_SIZE];
	unsigned int readIO;
	unsigned int writeIO;

	stat_fp = popen("cat /sys/block/sda/stat | cut -d' ' -f4,17", "r");
	fgets(status, STATUS_SIZE, stat_fp);
	sscanf(status, "%u %u", &readIO, &writeIO);

	printf("%u %u\n", &readIO, &writeIO);
	pclose(stat_fp);

	return 0;
}
