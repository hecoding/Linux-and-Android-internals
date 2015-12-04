#include <stdio.h>

cbuffer_t* cbuffer; /* Buffer circular */
int prod_count = 0; /* Número de procesos que abrieron la entrada
					   /proc para escritura (productores) */
int cons_count = 0; /* Número de procesos que abrieron la entrada
					   /proc para lectura (consumidores) */
struct semaphore mtx; /* para garantizar Exclusión Mutua */
struct semaphore sem_prod; /* cola de espera para productor(es) */
struct semaphore sem_cons; /* cola de espera para consumidor(es) */
int nr_prod_waiting=0; /* Número de procesos productores esperando */
int nr_cons_waiting=0; /* Número de procesos consumidores esperando */

/* Funciones de inicialización y descarga del módulo */
int init_module(void);
void cleanup_module(void);
/* Se invoca al hacer open() de entrada /proc */
static int fifoproc_open(struct inode *, struct file *) {
	...
	if (file->f_mode & FMODE_READ)
	{
	/* Un consumidor abrió el FIFO */
	...
	} else{
	/* Un productor abrió el FIFO */
	...
	}
	...
}
/* Se invoca al hacer close() de entrada /proc */
static int fifoproc_release(struct inode *, struct file *);
/* Se invoca al hacer read() de entrada /proc */
static ssize_t fifoproc_read(struct file *, char *, size_t, loff_t *);
/* Se invoca al hacer write() de entrada /proc */
static ssize_t fifoproc_write(struct file *, const char *, size_t,
loff_t *);
