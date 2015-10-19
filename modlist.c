#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");

#define BUFFER_LENGTH PAGE_SIZE

static struct proc_dir_entry *proc_entry;
char* modlistbuffer;

/* Nodos de la lista */
typedef struct list_item {
	int data;
	struct list_head links;
} list_item_t;

struct list_head mylist; /* Lista enlazada. OJO todos los demás nodos están en memoria dinámica. */

static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static void modlist_add(int num);
static void modlist_remove(int num);
static void modlist_cleanup(void);
static void modlist_sort(struct list_head* list);

static const struct file_operations proc_entry_fops = {
    .read = modlist_read,
    .write = modlist_write,    
};

int init_modlist_module( void )
{
  int ret = 0;
  modlistbuffer = (char *)vmalloc( BUFFER_LENGTH );
  INIT_LIST_HEAD( &mylist );


  if (!list_empty(&mylist)) {
    ret = -ENOMEM;
  } else {

    proc_entry = proc_create( "modlist", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
      ret = -ENOMEM;
      vfree(&mylist);
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
  //vfree(&mylist); LA CABEZA NO ESTÁ EN MEMORIA DINÁMICA, CON HACER UN CLEANUP VALE
  modlist_cleanup(); // <- porque los demás nodos sí están en memoria dinámica pero mylist en la pila
  vfree(modlistbuffer);
  printk(KERN_INFO "modlist: Module unloaded.\n");
}


static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
  struct list_item* item=NULL;
  struct list_head* cur_node=NULL;
  ssize_t buf_length = 0;
  
  if ((*off) > 0) /* Tell the application that there is nothing left to read */
    return 0;
    
  if (len<1)
    return -ENOSPC;

  /* copiar los datos al buffer de modlist */
  list_for_each(cur_node, &mylist) {
    /* item points to the structure wherein the links are embedded */
    item = list_entry(cur_node, struct list_item, links);
    buf_length += sprintf(modlistbuffer + buf_length, "%i\n", item->data);
  }

  /* Transfer data from the kernel to userspace  */
  if (copy_to_user(buf, modlistbuffer, buf_length))
    return -EINVAL;
    
  (*off)+=len;  /* Update the file pointer */

  return buf_length;
}

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
  int available_space = BUFFER_LENGTH-1;
  int num = 0;
  
  if ((*off) > 0) /* The application can write in this entry just once !! */
    return 0;
  
  if (len > available_space) {
    printk(KERN_INFO "modlist: not enough space!!\n");
    return -ENOSPC;
  }

  
  /* Transfer data from user to kernel space */
  if (copy_from_user( &modlistbuffer[0], buf, len ))  
    return -EFAULT;

  

  if(sscanf(modlistbuffer, "add %i", &num) == 1) {
    modlist_add(num);
	}
  else if(sscanf(modlistbuffer, "remove %i", &num) == 1) {
    modlist_remove(num);
	}
  else if(strncmp(modlistbuffer, "cleanup", 7) == 0) {
      modlist_cleanup();
	}
  else if(strncmp(modlistbuffer, "sort", 4) == 0) {
      modlist_sort(&mylist);
  }

  modlistbuffer[len] = '\0'; /* Add the `\0' */  
  *off+=len;           /* Update the file pointer */

  return len;
}

static void modlist_add(int num) {
  struct list_item* nodo = vmalloc(sizeof(struct list_item));
    
  nodo->data = num;

  list_add_tail(&nodo->links,&mylist);
}

static void modlist_remove(int num) {
  struct list_item* item=NULL;
  struct list_head* cur_node=NULL;
  struct list_head* aux=NULL;
  
  list_for_each_safe(cur_node, aux, &mylist){
  	item = list_entry(cur_node, struct list_item, links);

  	if(item->data == num) {
  		list_del(cur_node);
      vfree(item);
    }
	}
}

static void modlist_cleanup(void) {
  struct list_item* item=NULL;
  struct list_head* cur_node=NULL;
  struct list_head* aux=NULL;

  list_for_each_safe(cur_node, aux, &mylist){
    item = list_entry(cur_node, struct list_item, links);

    list_del(cur_node);
    vfree(item);
  }
}

static void modlist_sort(struct list_head* list) {
  struct list_item* item = NULL;
  struct list_item* nxt = NULL;
  struct list_head* cur_node = NULL;
  int still = 1;

  while(still) {
    still = 0;

    list_for_each(cur_node, list) {
      /* item points to the structure wherein the links are embedded */
      item = list_entry(cur_node, struct list_item, links);
      if((struct list_head*) &( (&(item->links))->next ) == list) break; // es el último, hemos acabado
      nxt = list_entry(cur_node->next, struct list_item, links);

      if(item->data > nxt->data) {
        still = 1;

        list_move(cur_node->next, list);

        break; // salir del list_for_each, NO del while
      }
    }

  }
}


module_init( init_modlist_module );
module_exit( exit_modlist_module );
