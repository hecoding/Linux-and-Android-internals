#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>

MODULE_LICENSE("GPL");


static struct proc_dir_entry *proc_entry;
static struct proc_dir_entry *proc_config;
struct timer_list timer; /* Structure that describes the kernel timer */


static ssize_t modtimer_config_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t modtimer_config_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
/* Function invoked when timer expires (fires) */
static void fire_timer(unsigned long data);

static const struct file_operations proc_entry_fops = {
//    .open = modtimer_open,
//    .release = modtimer_release,
//    .read = modtimer_read,   
};

static const struct file_operations proc_conf_entry_fops = {
    .read = modtimer_config_read,
    .write = modtimer_config_write,
};

int init_modtimer( void ) {
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

    /* Create timer */
    init_timer(&timer);
    /* Initialize field */
    timer.data=0;
    timer.function=fire_timer;
    timer.expires=jiffies + HZ;  /* Activate it one second from now */
    /* Activate the timer for the first time */
    add_timer(&timer);

    printk(KERN_INFO "modtimer: Module loaded.\n");

    return 0;
}


void exit_modtimer( void ) {
    /* Wait until completion of the timer function (if it's currently running) and delete timer */
    del_timer_sync(&timer);

    remove_proc_entry("modtimer", NULL);
    remove_proc_entry("modconfig", NULL);

    printk(KERN_INFO "modtimer: Module unloaded.\n");
}

static void fire_timer(unsigned long data) {
    printk(KERN_INFO "modtimer: timer fire.\n");

    /* Re-activate the timer one second from now */
    mod_timer( &(timer), jiffies + HZ); 
}

static ssize_t modtimer_config_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {

}

static ssize_t modtimer_config_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {

}

module_init( init_modtimer );
module_exit( exit_modtimer );
