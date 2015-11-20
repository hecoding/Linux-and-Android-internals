#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define STATUS_SIZE 20
int main() {

	FILE *stat_fp;
	char status[STATUS_SIZE];
	unsigned int prev_read=0;
	unsigned int prev_write=0;
	unsigned int readIO;
	unsigned int writeIO;
	int blink_fp;

	// COMPROBAR SI EXISTE /dev/usb/blinkstick0
	blink_fp = open("/dev/usb/blinkstick0", O_RDWR);

	while(1) {
		/* lee de /sys/block/sda/stat para coger estadísticas sobre el disco
		duro, en este caso read I/Os y write I/Os procesados de sda.
		Más información en kernel.org/doc/Documentation/block/stat.txt */
		stat_fp = popen("cat /sys/block/sda/stat | cut -d' ' -f4,17", "r");
		if (stat_fp == NULL)
			return -1;;


		fgets(status, STATUS_SIZE, stat_fp);

		sscanf(status, "%u %u", &readIO, &writeIO);

		// CAMBIAR PARA QUE SOLO HAYA DOS IFs EN VEZ DE ESTOS IFELSES
		if (readIO != prev_read && writeIO != prev_write) {
			if(-1 == write(blink_fp, "5:0x100000,7:0x000010", 22)) {
				perror("perror output:");
			}
			prev_read = readIO;
			prev_write = writeIO;
		}
		else if (readIO != prev_read) {
			// si se lee de disco, encience el led rojo
			if(-1 == write(blink_fp, "5:0x100000", 11)) {
				perror("perror output:");
			}
			prev_read = readIO;
		}
		else if(writeIO != prev_write) {
			// si se escribe en disco, enciende el led azul
			if(-1 == write(blink_fp, "7:0x000010", 11)) {
				perror("perror output:");
			}
			prev_write = writeIO;
		}

		usleep(5000); // espera microsegundos (se ve bien con 10000)

		pclose(stat_fp);
		if(-1 == write(blink_fp, "", 1)) {
				perror("perror output:");
			}
	}
	close(blink_fp);

	return 0;
}
