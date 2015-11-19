#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/syscalls.h>

#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0


struct tty_driver* kbd_driver= NULL;

/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void){
   printk(KERN_INFO "modleds: loading\n");
   printk(KERN_INFO "modleds: fgconsole is %x\n", fg_console);
   return vc_cons[fg_console].d->port.tty->driver;
}

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask){
  unsigned int state = 0;
  unsigned int current_led = 0;
  unsigned int i = 0;

  for (i = 0; i < 3; ++i) {
    current_led = mask & (1 << i);

    if (current_led) {
      if (current_led == 2)
        state |= 4;
      else if (current_led == 4)
        state |= 2;
      else state |= current_led;
    }
  }

  return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,state);
}

SYSCALL_DEFINE1(ledctl,unsigned int,leds)
{
	kbd_driver= get_kbd_driver_handler();
 	set_leds(kbd_driver, leds);
	return 0;
}
