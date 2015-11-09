#include <stdio.h>

#define STATUS_SIZE 20
int main() {

	FILE *fp;
	char status[STATUS_SIZE];
	unsigned int prev_read=0;
	unsigned int prev_write=0;
	unsigned int read;
	unsigned int write;

	// COMPROBAR SI EXISTE /dev/usb/blinkstick0

	while(1) {
		/* lee de /sys/block/sda/stat para coger estadísticas sobre el disco
		duro, en este caso read I/Os y write I/Os procesaos de sda.
		Más información en kernel.org/doc/Documentation/block/stat.txt */
		fp = popen("cat /sys/block/sda/stat | cut -d' ' -f4,17", "r");
		if (fp == NULL)
			return -1;;


		fgets(status, STATUS_SIZE, fp);

		sscanf(status, "%u %u", &read, &write);

		// CAMBIAR PARA QUE SOLO HAYA DOS IFs EN VEZ DE ESTOS IFELSES
		if (read != prev_read && write != prev_write) {
			system("echo 5:0x100000,7:0x000010 > /dev/usb/blinkstick0");
			prev_read = read;
			prev_write = write;
		}
		else if (read != prev_read) {
			// si se lee de disco, encience el led rojo
			system("echo 5:0x100000 > /dev/usb/blinkstick0");
			prev_read = read;
		}
		else if(write != prev_write) {
			// si se escribe en disco, enciende el led azul
			system("echo 7:0x000010 > /dev/usb/blinkstick0");
			prev_write = write;
		}

		usleep(1000); // espera microsegundos (se ve bien con 10000)

		pclose(fp);
		system("echo > /dev/usb/blinkstick0");
	}

	return 0;
}
