#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <asm-generic/uaccess.h>
#include <linux/jiffies.h>
#include <linux/random.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");


/* CONSTANTS */
#define DEFAULT_TIMER_PERIOD HZ
#define DEFAULT_MAX_RANDOM 300
#define DEFAULT_EMERGENCY_THRESHOLD 80
#define CONFIG_BUFFER_LENGTH 100
#define BUFFER_LENGTH 20
#define MAX_ITEMS_FIFO 10 // kfifo uses 2-base sizes and rounds-up the power

/* GLOBAL VARIABLES */
static struct proc_dir_entry *proc_entry; /* modtimer entry */
static struct proc_dir_entry *proc_config; /* modconfig entry */
struct timer_list timer; /* Structure that describes the kernel timer */
static ssize_t timer_period, max_random, emergency_threshold;
static struct kfifo fifobuff; /* Circular buffer */
struct work_struct copy_items_into_list_ws;
typedef struct list_item { /* Node of the list */
    unsigned int data;
    struct list_head links;
} list_item_t;
struct list_head mylist; /* head of the linked list. all the nodes are in dynamic memory. */

/* SYNCHRONIZATION VARIABLES */
DEFINE_SPINLOCK(cbuff_sp);
DEFINE_SPINLOCK(list_sp);
struct semaphore sem_list; /* user queue consumer */


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
static void modlist_add(unsigned int num);
static struct list_item* modlist_pop(void);
static void list_cleanup(void);

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
    int ret;

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
    ret = kfifo_alloc(&fifobuff, MAX_ITEMS_FIFO, GFP_KERNEL);

    if (ret) {
        printk(KERN_ERR "modtimer: Can't allocate memory for the buffer\n");
        return ret;
    }

    /* KFIFO WORK STRUCTURE SETUP */
    INIT_WORK(&copy_items_into_list_ws, copy_items_into_list_func);

    /* LINKED LIST SETUP */
    INIT_LIST_HEAD( &mylist );

    if (!list_empty(&mylist))
        return -ENOMEM;

    /* USER WAITING QUEUE SETUP */
    sema_init(&sem_list, 0);

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
    remove_proc_entry("modtimer", NULL);
    remove_proc_entry("modconfig", NULL);

    printk(KERN_INFO "modtimer: Module unloaded.\n");
}

static void fire_timer(unsigned long data) {
    unsigned long flags = 0;

    unsigned int rand_number = get_random_int() % (max_random - 1);
    printk(KERN_INFO "Generated number: %u\n", rand_number);

    spin_lock_irqsave(&cbuff_sp, flags);
    kfifo_put(&fifobuff, rand_number); // kfifo_in_spinlocked would be fine if not irqsave
    spin_unlock_irqrestore(&cbuff_sp, flags);

    if (!work_pending(&copy_items_into_list_ws) && ((kfifo_len(&fifobuff) * 100 / kfifo_size(&fifobuff)) >= emergency_threshold))
        schedule_work_on(!smp_processor_id(), &copy_items_into_list_ws); // just 2 cpus, 0 or 1. get the other one and queue work

    /* Re-activate the timer 'timer_period' from now */
    mod_timer(&(timer), jiffies + timer_period);
}

static void copy_items_into_list_func(struct work_struct *work) {
    unsigned int data = 0;
    unsigned long flags = 0;
    unsigned int count = 0;
    unsigned int n;

    while(!kfifo_is_empty(&fifobuff)) {
        spin_lock_irqsave(&cbuff_sp, flags); // can't take off the loop. blocking calls in modlist_add
        n = kfifo_get(&fifobuff, &data);
        spin_unlock_irqrestore(&cbuff_sp, flags);

        modlist_add(data); // list lock inside

        count++;
        up(&sem_list); /* one more item into the list */
    }

    printk(KERN_INFO "%u elements moved from the buffer to the list\n", count);
}

static ssize_t modtimer_config_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    char* configbuffer = (char *)vmalloc( CONFIG_BUFFER_LENGTH );
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
    char configbuffer[CONFIG_BUFFER_LENGTH];
    int available_space = CONFIG_BUFFER_LENGTH-1;
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

    kfifo_free(&fifobuff);
    list_cleanup();

    module_put(THIS_MODULE);

    return 0;
}

static ssize_t modtimer_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    struct list_item* item = NULL;
    char modlistbuffer[BUFFER_LENGTH];
    ssize_t buf_length = 0;

    if ((*off) > 0) /* Tell the application there is nothing left to read */
        return 0;

    if (len<1)
        return -ENOSPC;

    if(down_interruptible(&sem_list)) /* Hang in until there are items to read */
        return -EINTR;

    item = modlist_pop();

    buf_length = sprintf(modlistbuffer, "%u\n", item->data);
    vfree(item);

    /* Transfer data from the kernel to userspace  */
    if (copy_to_user(buf, modlistbuffer, buf_length))
    return -EINVAL;

    (*off)+=len;  /* Update the file pointer */

    return buf_length;
}

static void modlist_add(unsigned int num) {
    struct list_item* nodo = vmalloc(sizeof(struct list_item));

    nodo->data = num;

    spin_lock(&list_sp);
    list_add_tail(&nodo->links,&mylist);
    spin_unlock(&list_sp); 
}

static struct list_item* modlist_pop(void) {
    struct list_head* node=NULL;
    struct list_item* item = NULL;

    spin_lock(&list_sp);

    node = mylist.next;
    item = list_entry(node, struct list_item, links);
    list_del(node);

    spin_unlock(&list_sp);

    return item;
}

static void list_cleanup(void) {
    struct list_item* item=NULL;
    struct list_head* cur_node=NULL;
    struct list_head* aux=NULL;

    spin_lock(&list_sp); 
    list_for_each_safe(cur_node, aux, &mylist){
        item = list_entry(cur_node, struct list_item, links);

        list_del(cur_node);
        vfree(item);
    }
    spin_unlock(&list_sp);
}

module_init( init_modtimer );
module_exit( exit_modtimer );
