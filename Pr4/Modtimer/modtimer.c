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

MODULE_LICENSE("GPL");


#define DEFAULT_TIMER_PERIOD HZ
#define DEFAULT_MAX_RANDOM 300
#define DEFAULT_EMERGENCY_THRESHOLD 80
#define CONFIG_BUFFER_LENGTH 100
#define MAX_ITEMS_FIFO 10 // kfifo uses 2-base sizes and rounds-up

static struct proc_dir_entry *proc_entry; /* modtimer entry */
static struct proc_dir_entry *proc_config; /* modconfig entry */
struct timer_list timer; /* Structure that describes the kernel timer */
static ssize_t timer_period, max_random, emergency_threshold;
static struct kfifo fifobuff; /* Circular buffer */
struct work_struct copy_items_into_list;

DEFINE_SPINLOCK(timer_sp);


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
static void buff_wq_function(struct work_struct *work);

static const struct file_operations proc_entry_fops = {
    .open = modtimer_open,
    .release = modtimer_release,
    .read = modtimer_read,   
};

static const struct file_operations proc_conf_entry_fops = {
    .read = modtimer_config_read,
    .write = modtimer_config_write,
};

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
    INIT_WORK(&copy_items_into_list, buff_wq_function);

    /* TIMER SETUP */
    timer_period = DEFAULT_TIMER_PERIOD;
    max_random = DEFAULT_MAX_RANDOM;
    emergency_threshold = DEFAULT_EMERGENCY_THRESHOLD;

    /* Create timer */
    init_timer(&timer);
    /* Initialize field */
    timer.data=0;
    timer.function=fire_timer;
    timer.expires=jiffies + timer_period;  /* Activate it X seconds from now */
    /* Activate the timer for the first time */
    add_timer(&timer);

    printk(KERN_INFO "modtimer: Module loaded.\n");

    return 0;
}


void exit_modtimer( void ) {
    /* Wait until completion of the timer function (if it's currently running) and delete timer */
    del_timer_sync(&timer);

    /* Wait until all jobs scheduled so far have finished */
    flush_scheduled_work();

    kfifo_free(&fifobuff);

    remove_proc_entry("modtimer", NULL);
    remove_proc_entry("modconfig", NULL);

    printk(KERN_INFO "modtimer: Module unloaded.\n");
}

static void fire_timer(unsigned long data) {
    unsigned long flags = 0;

    unsigned int rand_number = get_random_int() % (max_random - 1);
    printk(KERN_INFO "Generated number: %u\n", rand_number);

    spin_lock_irqsave(&timer_sp, flags);
    kfifo_put(&fifobuff, rand_number); // kfifo_in_spinlocked would be fine if not irqsave
    spin_unlock_irqrestore(&timer_sp, flags);

    if (!work_pending(&copy_items_into_list) && ((kfifo_len(&fifobuff) * 100 / kfifo_size(&fifobuff)) >= emergency_threshold))
        schedule_work_on(!smp_processor_id(), &copy_items_into_list); // just 2 cpus, 0 or 1. get the other one and queue work

    /* Re-activate the timer 'timer_period' from now */
    mod_timer( &(timer), jiffies + timer_period);
}

static void buff_wq_function(struct work_struct *work) {
    printk(KERN_INFO "%u elements moved from the buffer to the list\n", kfifo_len(&fifobuff));
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
    // ...

    try_module_get(THIS_MODULE);
}

static int modtimer_release(struct inode *inode, struct file *file) {
    // ...

    module_put(THIS_MODULE);
}

static ssize_t modtimer_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {

}

module_init( init_modtimer );
module_exit( exit_modtimer );
