#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");

#define BUFFER_LENGTH       PAGE_SIZE

struct list_head mylist;
/* Lista enlazada */
/* Nodos de la lista */
typedef struct {
	int data;
	struct list_head links;
} list_item_t;



static struct proc_dir_entry *proc_entry;
static char *modlist;  // Space for the "modlist"

static const struct file_operations proc_entry_fops = {
    .read = modlist_read,
    .write = modlist_write,    
    .add = modlist_add,
    .remove = modlist_remove,
    .cleanup = modlist_cleanup,
};

int init_modlist_module( void )
{
  int ret = 0;
  modlist = (char *)vmalloc( BUFFER_LENGTH );
  INIT_LIST_HEAD( &mylist );


  if (!modlist) {
    ret = -ENOMEM;
  } else {

    memset( modlist, 0, BUFFER_LENGTH );
    proc_entry = proc_create( "modlist", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
      ret = -ENOMEM;
      vfree(modlist);
      printk(KERN_INFO "modlist: Can't create /proc entry\n");
    } else {
      printk(KERN_INFO "modlist: Module loaded\n");
    }
  }

  return ret;

}


void exit_modlist_module( void )
{
  remove_proc_entry("modlist", NULL);
  vfree(modlist);
  printk(KERN_INFO "modlist: Module unloaded.\n");
}


module_init( init_modlist_module );
module_exit( exit_modlist_module );
