#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>
// no copiar esto
#include <linux/proc_fs.h>

#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0


struct tty_driver* kbd_driver= NULL;

// no copiar esto
static struct proc_dir_entry *proc_entry;


/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void){
   printk(KERN_INFO "modleds: loading\n");
   printk(KERN_INFO "modleds: fgconsole is %x\n", fg_console);
   return vc_cons[fg_console].d->port.tty->driver;
}

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static inline int set_leds(struct tty_driver* handler, unsigned int mask);

// no copiar esto
static const struct file_operations proc_entry_fops = {
    .write = modlist_write,
};

int init_modlist_module( void )
{
  // no copiar esto
  proc_entry = proc_create( "modleds", 0666, NULL, &proc_entry_fops);

  kbd_driver= get_kbd_driver_handler();
   set_leds(kbd_driver,ALL_LEDS_ON);
   return 0;
}

void exit_modlist_module( void )
{
  set_leds(kbd_driver,ALL_LEDS_OFF);

  // no copiar esto
  printk(KERN_INFO "modlist: Module unloaded.\n");
}

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
  char* modlistbuffer = (char *)vmalloc( 1024 );
  int available_space = 1024-1;
  unsigned int num = 0;

  if ((*off) > 0) /* The application can write in this entry just once !! */
    return 0;

  if (len > available_space) {
    printk(KERN_INFO "modlist: not enough space!!\n");
    return -ENOSPC;
  }


  /* Transfer data from user to kernel space */
  if (copy_from_user( &modlistbuffer[0], buf, len ))
    return -EFAULT;

  sscanf(modlistbuffer, "%x", &num);
  printk(KERN_INFO "meter %x\n", num);
  set_leds(kbd_driver, num);

  vfree(modlistbuffer);

  return len;
}

static inline int set_leds(struct tty_driver* handler, unsigned int mask){

    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}


module_init( init_modlist_module );
module_exit( exit_modlist_module );

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modlist");
