obj-m =  proctimer.o #proctimer.c no debe existir
proctimer-objs = modtimer.o cbuffer.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

