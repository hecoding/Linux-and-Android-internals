#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");

#define BUFFER_LENGTH PAGE_SIZE

struct list_head mylist; /* Lista enlazada */
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


static int modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    int nr_bytes = sizeof(int);
    char *kbuf;
    int cont = 0;
    int num =0;
    int tam =0;
    list_item* item=NULL;
    struct list_head* cur_node=NULL;
    
    if ((*off) > 0) /* Tell the application that there is nothing left to read */
        return 0;
      
    if (len<nr_bytes)
      return -ENOSPC;

    list_for_each(cur_node, &mylist) {
      /* item points to the structure wherein the links are embedded */
      item = list_entry(cur_node, list_item, links);
      num = item->data;
      //trace_printk("Current value of clipboard: %i\n",num );
      kbuf+= sprintf(kbuf,"%i\n",num);
      trace_printk("Current value of clipboard: %s\n",kbuf );
      cont++;
      //printk(KERN_INFO "%i\n",item->data);
      /* Transfer data from the kernel to userspace  */
      
    }
    kbuf += '\0';
    tam = strlen(kbuf);
     if (copy_to_user(buf, kbuf,tam))
        return -EINVAL;
    
      /* Transfer data from the kernel to userspace */
    //if (copy_to_user(buf, clipboard,nr_bytes))
     // return -EINVAL;
      
    (*off)+=len;  /* Update the file pointer */

    return nr_bytes; 
}

static int modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
	  int available_space = BUFFER_LENGTH-1;
    char kbuf[1000];
    int num = 0;
    
    if ((*off) > 0) /* The application can write in this entry just once !! */
      return 0;
    
    if (len > available_space) {
      printk(KERN_INFO "modlist: not enough space!!\n");
      return -ENOSPC;
    }

    
    /* Transfer data from user to kernel space */
    if (copy_from_user( &kbuf, buf, len ))  
      return -EFAULT;

    

    if(sscanf(kbuf, "add %i", &num) == 1) {
      list_item* nodo = vmalloc(sizeof(list_item));
      
      nodo->data = num;

      trace_printk("Current list empty: %i\n",list_empty(&mylist) );

      list_add_tail(&nodo->links,&mylist);
      trace_printk("Current value of clipboard: %i\n",num );
      trace_printk("Current list empty: %i\n",list_empty(&mylist) );
  }

  //clipboard[len] = '\0'; /* Add the `\0' */  
  *off+=len;            /* Update the file pointer */
  
  return len;
}

static int modlist_add(/* parametros */) {

}

static int modlist_remove(/* parametros */) {

}

static int modlist_cleanup(/* parametros */) {

}

void print_list(struct list_head* list) {
  struct list_item_t* item = NULL;
  struct list_head* cur_node = NULL;
  list_for_each(cur_node, list) {
  /* item points to the structure wherein the links are embedded */
    item = list_entry(cur_node, struct list_item, links);
    printk(KERN_INFO "%i \n", item->data);
  }
}

void f(void) {
  INIT_LIST_HEAD(&my_list); /* Initialize the list */
  //do_something(&my_list); /* Populate the list */
  print_list(&my_list);
}


module_init( init_modlist_module );
module_exit( exit_modlist_module );
