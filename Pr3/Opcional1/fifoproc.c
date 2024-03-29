#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/semaphore.h>
#include <linux/kfifo.h>

MODULE_LICENSE("GPL");

#define MAX_ITEMS_FIFO 64
#define MAX_KBUF 128

static struct proc_dir_entry *proc_entry;
static struct kfifo fifobuff; /* buffer circular de linux */
int prod_count = 0; /* Número de procesos que abrieron la entrada
					   /proc para escritura (productores) */
int cons_count = 0; /* Número de procesos que abrieron la entrada
					   /proc para lectura (consumidores) */
struct semaphore mtx; /* para garantizar Exclusión Mutua */
struct semaphore sem_prod; /* cola de espera para productor(es) */
struct semaphore sem_cons; /* cola de espera para consumidor(es) */
int nr_prod_waiting=0; /* Número de procesos productores esperando */
int nr_cons_waiting=0; /* Número de procesos consumidores esperando */


/* Se invoca al hacer open() de entrada /proc */
static int fifoproc_open(struct inode *inode, struct file *file) {
	/* "Adquiere" el mutex */
	if (down_interruptible(&mtx))
		return -EINTR;


	if (file->f_mode & FMODE_READ) {
		// un consumidor abrió el fifo
		cons_count++;
		
		if (nr_prod_waiting>0) {
			/* Despierta a uno de los hilos bloqueados */
			up(&sem_prod);
			nr_prod_waiting--;
		}

		while (prod_count==0) {
			nr_cons_waiting++;
			up(&mtx); /* "Libera" el mutex */
			
			/* Se bloquea en la cola */
			if (down_interruptible(&sem_cons)){
				down(&mtx);
				nr_cons_waiting--;
				up(&mtx);
				
				return -EINTR;
			}
			
			/* "Adquiere" el mutex */
			if (down_interruptible(&mtx))
				return -EINTR;

		}

	} else {
		// un productor abrió el fifo
		prod_count++;

		if (nr_cons_waiting>0) {
			/* Despierta a uno de los hilos bloqueados */
			up(&sem_cons);
			nr_cons_waiting--;
		}

		while (cons_count==0) {
			nr_prod_waiting++;
			up(&mtx); /* "Libera" el mutex */
			
			/* Se bloquea en la cola */
			if (down_interruptible(&sem_prod)){
				down(&mtx);
				nr_prod_waiting--;
				up(&mtx);
				
				return -EINTR;
			}
			
			/* "Adquiere" el mutex */
			if (down_interruptible(&mtx))
				return -EINTR;

		}
	}

	/* "Libera" el mutex */
	up(&mtx);

	return 0;
}

/* Se invoca al hacer close() de entrada /proc */
static int fifoproc_release(struct inode *inode, struct file *file){
	if (down_interruptible(&mtx))
		return -EINTR;

	if (file->f_mode & FMODE_READ) {
		// un consumidor cerró el fifo
		cons_count--;
		/* Despertar a posible productor bloqueado */
		if (nr_prod_waiting>0) {
			/* Despierta a uno de los hilos bloqueados */
			up(&sem_prod);
			nr_prod_waiting--;
		}

	} else {
		// un productor cerró el fifo
		prod_count--;
		/* Despertar a posible consumidor bloqueado */
		if (nr_cons_waiting>0) {
			/* Despierta a uno de los hilos bloqueados */
			up(&sem_cons);
			nr_cons_waiting--;
		}
	}

	if (cons_count == 0 && prod_count == 0)
		kfifo_reset(&fifobuff);

	/* "Libera" el mutex */
	up(&mtx);
	return 0;
}

/* Se invoca al hacer read() de entrada /proc */
static ssize_t fifoproc_read(struct file *filp, char __user *buff, size_t len, loff_t *off){
	char kbuffer[MAX_KBUF]="";
	int leido;
        
	if (len> MAX_ITEMS_FIFO || len> MAX_KBUF) { return -ENOSPC;}

	if (down_interruptible(&mtx))
		return -EINTR;

	/* Esperar hasta que haya un dato para consumir (debe haber productores) */
	while (kfifo_len(&fifobuff)<len && prod_count>0){
		nr_cons_waiting++;
		up(&mtx); /* "Libera" el mutex */
		
		/* Se bloquea en la cola */
		if (down_interruptible(&sem_cons)){
			down(&mtx);
			nr_cons_waiting--;
			up(&mtx);
			
			return -EINTR;
		}
		
		/* "Adquiere" el mutex */
		if (down_interruptible(&mtx))
			return -EINTR;
	}

	/* Detectar fin de comunicación por error (productor cierra FIFO antes) */
	if (prod_count==0 && kfifo_is_empty(&fifobuff)) {up(&mtx); return 0;}

	// leer kfifo
	leido = kfifo_out(&fifobuff,kbuffer,len);

	/* Despertar a posible productor bloqueado */
	if (nr_prod_waiting>0) {
		/* Despierta a uno de los hilos bloqueados */
		up(&sem_prod);
		nr_prod_waiting--;
	}

	up(&mtx); 
	// USAR OUT_TO_USER
	if (copy_to_user(buff, kbuffer, len)) { return -EINVAL;}
	(*off)+=len;
	return len;
}

/* Se invoca al hacer write() de entrada /proc */
static ssize_t fifoproc_write(struct file *filp, const char __user *buff, size_t len, loff_t *off){
	char kbuffer[MAX_KBUF]="";
	int escrito;

	/*if (off>0)
		return 0;*/

	if (len> MAX_ITEMS_FIFO || len> MAX_KBUF) { return -ENOSPC;}
	if (copy_from_user(kbuffer,buff,len)) { return -EFAULT;} // USAR OUT TO USER
	kbuffer[len] = '\0';
	*off += len;

	if (down_interruptible(&mtx))
		return -EINTR;

	/* Esperar hasta que haya hueco para insertar (debe haber consumidores) */
	while (kfifo_avail(&fifobuff)<len && cons_count>0){
		nr_prod_waiting++;
		up(&mtx); /* "Libera" el mutex */
		
		/* Se bloquea en la cola */
		if (down_interruptible(&sem_prod)){
			down(&mtx);
			nr_prod_waiting--;
			up(&mtx);
			
			return -EINTR;
		}
		
		/* "Adquiere" el mutex */
		if (down_interruptible(&mtx))
			return -EINTR;
	}

	/* Detectar fin de comunicación por error (consumidor cierra FIFO antes) */
	if (cons_count==0) {up(&mtx); return -EPIPE;}

	escrito = kfifo_in(&fifobuff,kbuffer,len);

	/* Despertar a posible consumidor bloqueado */
	if (nr_cons_waiting>0) {
		/* Despierta a uno de los hilos bloqueados */
		up(&sem_cons);
		nr_cons_waiting--;
	}

	up(&mtx); 
	return len;
}

static const struct file_operations proc_entry_fops = {
    .open = fifoproc_open,
    .release = fifoproc_release,
    .read = fifoproc_read,
    .write = fifoproc_write,    
};

/* Funciones de inicialización y descarga del módulo */
int init_fifoproc_module(void){
	int ret;
	ret = kfifo_alloc(&fifobuff, MAX_ITEMS_FIFO, GFP_KERNEL);

	if (ret) {
		printk(KERN_ERR "error al reservar espacio para kfifo\n");
		return ret;
	}

	sema_init(&mtx, 1);
	sema_init(&sem_prod, 0);
	sema_init(&sem_cons, 0);

	proc_entry = proc_create("modfifo",0666, NULL, &proc_entry_fops);
	if (proc_entry == NULL) {
		kfifo_free(&fifobuff);
		return -ENOMEM;
	}

	printk(KERN_INFO "modfifo: Module loaded.\n");

	return 0;
}

void cleanup_fifoproc_module(void){
	kfifo_free(&fifobuff);
	remove_proc_entry("modfifo", NULL);
	printk(KERN_INFO "modfifo: Module unloaded.\n");
}

module_init( init_fifoproc_module );
module_exit( cleanup_fifoproc_module );
