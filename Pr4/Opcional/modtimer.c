#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/jiffies.h>
#include <linux/random.h>
#include "cbuffer.h"
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/list.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");


/* CONSTANTS */
#define DEFAULT_TIMER_PERIOD HZ
#define DEFAULT_MAX_RANDOM 300
#define DEFAULT_EMERGENCY_THRESHOLD 80
#define BUFFER_LENGTH 100
#define MAX_ITEMS_CBUFFER 10 

/* GLOBAL VARIABLES */
static struct proc_dir_entry *proc_entry; /* modtimer entry */
static struct proc_dir_entry *proc_config; /* modconfig entry */
struct timer_list timer; /* Structure that describes the kernel timer */
static ssize_t timer_period, max_random, emergency_threshold;
static cbuffer_t* cbuf_pair;  /* Circular buffer */
static cbuffer_t* cbuf_odd;
struct work_struct copy_items_into_list_pair_ws;
struct work_struct copy_items_into_list_odd_ws;
typedef struct list_item { /* Node of the list */
    unsigned int data;
    struct list_head links;
} list_item_t;
struct list_head mylist_pair; /* head of the linked list. all the nodes are in dynamic memory. */
struct list_head mylist_odd;

/* SYNCHRONIZATION VARIABLES */
DEFINE_SPINLOCK(cbuff_pair_sp);
struct semaphore list_mtx_pair;
struct semaphore sem_list_pair; /* user queue consumer */
int waiting_pair=0;
DEFINE_SPINLOCK(cbuff_odd_sp);
struct semaphore list_mtx_odd;
struct semaphore sem_list_odd; /* user queue consumer */
int waiting_odd=0;


/* PROTOTYPES */
static ssize_t modtimer_config_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t modtimer_config_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
/* Invoked when calling open() at /proc entry */
static int modtimer_open(struct inode *inode, struct file *file);
/* Invoked when calling close() at /proc entry */
static int modtimer_release(struct inode *inode, struct file *file);
static ssize_t modtimer_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
/* Function invoked when timer expires (fires) */
static void fire_timer(unsigned long data);
/* Work's handler function, invoked to move the data in the buffer to a list */
static void copy_items_into_list_func(struct work_struct *work);
/* Linked list auxiliary functions */
static int my_list_add(unsigned int aux_buffer[], int count, struct semaphore *list_mtx, struct list_head *mylist, struct semaphore *sem_list);
static int list_cleanup(void);

/* FILE OPS FOR PROC ENTRIES */
static const struct file_operations proc_entry_fops = {
    .open = modtimer_open,
    .release = modtimer_release,
    .read = modtimer_read,   
};

static const struct file_operations proc_conf_entry_fops = {
    .read = modtimer_config_read,
    .write = modtimer_config_write,
};

/* FUNCTIONS */
int init_modtimer( void ) {

    /* PROC ENTRIES SETUP */
    proc_entry = proc_create("modtimer", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
        printk(KERN_INFO "modtimer: Can't create /proc entry\n");
        return -ENOMEM;
    }

    proc_config = proc_create("modconfig", 0666, NULL, &proc_conf_entry_fops);
    if (proc_entry == NULL) {
        printk(KERN_INFO "modtimer: Can't create /proc conf entry\n");
        return -ENOMEM;
    }

    /* CIRCULAR BUFFER SETUP */
    cbuf_pair = create_cbuffer_t(MAX_ITEMS_CBUFFER);
    cbuff_odd = create_cbuffer_t(MAX_ITEMS_CBUFFER);

    if (!cbuf) {
        return -ENOMEM;
    }

    /* CBUFFER WORK STRUCTURE SETUP */
    INIT_WORK(&copy_items_into_list_pair_ws, copy_items_into_list_pair_func);
    INIT_WORK(&copy_items_into_list_odd_ws, copy_items_into_list_odd_func);

    /* LINKED LIST SETUP */
    INIT_LIST_HEAD( &mylist_pair );
    INIT_LIST_HEAD( &mylist_odd );

    if (!list_empty(&mylist_pair) || !list_empty(&mylist_odd))
        return -ENOMEM;

    /* USER WAITING QUEUE SETUP */
    sema_init(&sem_list_pair, 0);
    sema_init(&sem_list_odd, 0);

    sema_init(&list_mtx_pair, 1);
    sema_init(&list_mtx_odd, 1);

    /* TIMER SETUP */
    timer_period = DEFAULT_TIMER_PERIOD;
    max_random = DEFAULT_MAX_RANDOM;
    emergency_threshold = DEFAULT_EMERGENCY_THRESHOLD;

    /* Create timer */
    init_timer(&timer);
    /* Initialize field */
    timer.data=0;
    timer.function=fire_timer;
    timer.expires=0;

    printk(KERN_INFO "modtimer: Module loaded.\n");

    return 0;
}


void exit_modtimer( void ) {

    destroy_cbuffer_t(cbuf_pair);
    destroy_cbuffer_t(cbuf_odd);
    remove_proc_entry("modtimer", NULL);
    remove_proc_entry("modconfig", NULL);

    printk(KERN_INFO "modtimer: Module unloaded.\n");
}

static void fire_timer(unsigned long data) {
    unsigned long flags = 0;

    unsigned int rand_number = get_random_int() % (max_random - 1);
    printk(KERN_INFO "Generated number: %u\n", rand_number);

    if(rand_number % 2) { // odd number
        spin_lock_irqsave(&cbuff_odd_sp, flags);
        insert_cbuffer_t(cbuf_odd, rand_number);
        spin_unlock_irqrestore(&cbuff_odd_sp, flags);

        if (!work_pending(&copy_items_into_list_odd_ws) && size_cbuffer_t(cbuf_odd)==((emergency_threshold*MAX_ITEMS_CBUFFER)/100)){
            schedule_work_on(!smp_processor_id(), &copy_items_into_list_odd_ws); // just 2 cpus, 0 or 1. get the other one and queue work
            printk("%i elements moved from the buffer to the odd list\n", size_cbuffer_t(cbuf_odd));
        }
    }
    else { // pair number
        spin_lock_irqsave(&cbuff_pair_sp, flags);
        insert_cbuffer_t(cbuf_pair, rand_number);
        spin_unlock_irqrestore(&cbuff_pair_sp, flags);

        if (!work_pending(&copy_items_into_list_pair_ws) && size_cbuffer_t(cbuf_pair)==((emergency_threshold*MAX_ITEMS_CBUFFER)/100)){
            schedule_work_on(!smp_processor_id(), &copy_items_into_list_pair_ws); // just 2 cpus, 0 or 1. get the other one and queue work
            printk("%i elements moved from the buffer to the pair list\n", size_cbuffer_t(cbuf_pair));
        }
    }

    /* Re-activate the timer 'timer_period' from now */
    mod_timer(&(timer), jiffies + timer_period);
}

static void copy_items_into_list_pair_func(struct work_struct *work) {
    int count = 0;
    unsigned long flags = 0;
    unsigned int aux_buffer[MAX_ITEMS_CBUFFER];

    while(!is_empty_cbuffer_t(cbuf_pair)) {
        spin_lock_irqsave(&cbuff_pair_sp, flags);
        aux_buffer[count] = head_cbuffer_t(cbuf_pair);
        remove_cbuffer_t(cbuf_pair);
        spin_unlock_irqrestore(&cbuff_pair_sp, flags);
        count++;
    }

    my_list_add(aux_buffer, count, &list_mtx_odd, &mylist_pair, &sem_list_pair);
}

static void copy_items_into_list_odd_func(struct work_struct *work) {
    int count = 0;
    unsigned long flags = 0;
    unsigned int aux_buffer[MAX_ITEMS_CBUFFER];

    while(!is_empty_cbuffer_t(cbuf_odd)) {
        spin_lock_irqsave(&cbuff_odd_sp, flags);
        aux_buffer[count] = head_cbuffer_t(cbuf_odd);
        remove_cbuffer_t(cbuf_odd);
        spin_unlock_irqrestore(&cbuff_odd_sp, flags);
        count++;
    }

    my_list_add(aux_buffer, count, &list_mtx_pair, &mylist_odd, &sem_list_pair);
}

static ssize_t modtimer_config_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    char* configbuffer = (char *)vmalloc( BUFFER_LENGTH );
    ssize_t buf_length = 0;

    if ((*off) > 0) /* Tell the application that there is nothing left to read */
        return 0;
    
    if (len<1)
        return -ENOSPC;

    /* Print current configuration to the buffer */
    buf_length += sprintf(configbuffer + buf_length, "timer_period_ms=%u\n", jiffies_to_msecs(timer_period));
    buf_length += sprintf(configbuffer + buf_length, "emergency_threshold=%zu\n", emergency_threshold);
    buf_length += sprintf(configbuffer + buf_length, "max_random=%zu\n", max_random);

    /* Transfer data from the kernel to userspace  */
    if (copy_to_user(buf, configbuffer, buf_length))
        return -EINVAL;

    (*off)+=len;  /* Update the file pointer */
    vfree(configbuffer);

    return buf_length;
}

static ssize_t modtimer_config_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    char configbuffer[BUFFER_LENGTH];
    int available_space = BUFFER_LENGTH-1;
    ssize_t num = 0;

    if ((*off) > 0) /* The application can write in this entry just once !! */
    return 0;

    if (len > available_space) {
        printk(KERN_INFO "modlist: not enough space!!\n");
        return -ENOSPC;
    }

    /* Transfer data from user to kernel space */
    if (copy_from_user( &configbuffer[0], buf, len ))  
        return -EFAULT;

    configbuffer[len] = '\0'; /* Add the `\0' */ 

    if(sscanf(configbuffer, "timer_period_ms %zu", &num) == 1) {
        timer_period = msecs_to_jiffies(num);
    }
    else if(sscanf(configbuffer, "emergency_threshold %zu", &num) == 1) {
        emergency_threshold = num;
    }
    else if(sscanf(configbuffer, "max_random %zu", &num) == 1) {
        max_random = num;
    }

    *off+=len; /* Update the file pointer */

    return len;
}

static int modtimer_open(struct inode *inode, struct file *file) {

    /* Activate it X seconds from now */
    timer.expires=jiffies + timer_period;
    /* Activate the timer */
    add_timer(&timer);

    try_module_get(THIS_MODULE);

    return 0;
}

static int modtimer_release(struct inode *inode, struct file *file) {
    /* Wait until completion of the timer function (if it's currently running) and delete timer */
    del_timer_sync(&timer);

    /* Wait until all jobs scheduled so far have finished */
    flush_scheduled_work();

    clear_cbuffer_t(cbuf);
    list_cleanup();

    module_put(THIS_MODULE);

    return 0;
}

static ssize_t modtimer_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    struct list_item* item = NULL;
    struct list_head* node=NULL;
    struct list_head* aux=NULL;
    char modlistbuffer[BUFFER_LENGTH];
    ssize_t buf_length = 0;

    if (len<1)
        return -ENOSPC;

    if (down_interruptible(&list_mtx))
        return -EINTR;

    while(list_empty(&mylist)){
        waiting++;
        up(&list_mtx);  
        if(down_interruptible(&sem_list)){
            down(&list_mtx);
            waiting--;
            up(&list_mtx);  
            return -EINTR;    
        }
        if (down_interruptible(&list_mtx))
            return -EINTR;
    }   

    list_for_each_safe(node, aux, &mylist) {
        item = list_entry(node, struct list_item, links);
        buf_length += sprintf(modlistbuffer + buf_length, "%u\n", item->data);
        list_del(node);
        vfree(item);
    }

    up(&list_mtx);    

    /* Transfer data from the kernel to userspace  */
    if (copy_to_user(buf, modlistbuffer, buf_length))
    return -EINVAL;

    (*off)+=len;  /* Update the file pointer */

    return buf_length;
}

static int my_list_add(unsigned int aux_buffer[], int count,
    struct semaphore *list_mtx, struct list_head *mylist, struct semaphore *sem_list){

    int i;

    if (down_interruptible(list_mtx))
        return -EINTR;

    for(i = 0; i < count; i++){
        struct list_item* nodo = vmalloc(sizeof(struct list_item));
        nodo->data = aux_buffer[i];
        list_add_tail(&nodo->links,mylist);
    }

    if(!list_empty(mylist) && waiting > 0){
        up(sem_list);
        waiting--;
    }

    up(list_mtx); 

    return 0;
}

static int list_cleanup(void) {
    struct list_item* item=NULL;
    struct list_head* cur_node=NULL;
    struct list_head* aux=NULL;

    if (down_interruptible(&list_mtx))
        return -EINTR;

    list_for_each_safe(cur_node, aux, &mylist){
        item = list_entry(cur_node, struct list_item, links);

        list_del(cur_node);
        vfree(item);
    }

    up(&list_mtx);

    return 0;
}

module_init( init_modtimer );
module_exit( exit_modtimer );

/*
Implementar una versión alternativa de la práctica en la cual los números pares generados
se inserten en una lista enlazada y los impares en otra:

    - El módulo permitirá que dos programas de usuario abran la entrada /proc/modtimer. SIN HACER

    - El primer proceso en abrir la entrada procesará los números pares y el segundo los impares. SIN HACER
        Pista: Para poder distinguir entre ambos procesos puede modificarse el campo private_data de struct file al abrir el fichero

    - La secuencia de números comenzará a generarse cuando ambos procesos hayan abierto la entrada /proc. SIN HACER
*/
