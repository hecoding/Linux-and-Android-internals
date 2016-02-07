#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <linux/list.h>
#include <linux/spinlock.h>

MODULE_LICENSE("GPL");

#define BUFFER_LENGTH 240
#define ENTRY_NAME_LENGTH 20
DEFINE_SPINLOCK(sp);

static struct proc_dir_entry *proc_control_entry;
static struct proc_dir_entry *proc_default_entry;
static struct proc_dir_entry *proc_dir=NULL;

/* Nodos de la lista */
typedef struct list_item {
	int data;
	struct list_head links;
} list_item_t;

struct list_head mylist; /* Lista enlazada. OJO todos los demás nodos están en memoria dinámica. */

static ssize_t control_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t control_add(char* name);
static void control_remove(char* name);
static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static void modlist_add(int num);
static void modlist_remove(int num);
static void modlist_cleanup(void);

static const struct file_operations proc_control_fops = {
    .write = control_write,    
};

static const struct file_operations proc_entry_fops = {
    .read = modlist_read,
    .write = modlist_write,    
};

int init_modlist_module( void )
{
  int ret = 0;
  INIT_LIST_HEAD( &mylist );

  /* Create proc directory */
  proc_dir=proc_mkdir("multilist",NULL);

  if (!proc_dir)
      return -ENOMEM;

  /* Create proc entry /proc/multilist/control */
  proc_control_entry = proc_create( "control", 0666, proc_dir, &proc_control_fops);
  proc_default_entry = proc_create( "default", 0666, proc_dir, &proc_entry_fops);

  if (proc_control_entry == NULL) {
      remove_proc_entry("multilist", NULL);
      printk(KERN_INFO "multilist: Can't create /proc entry\n");
      return -ENOMEM;
  }

  if (!list_empty(&mylist))
    ret = -ENOMEM;

  printk(KERN_INFO "multilist: Module loaded\n");
  //try_module_get(THIS_MODULE);

  return ret;
}

void exit_modlist_module( void )
{
  remove_proc_entry("control", proc_dir);
  remove_proc_entry("multilist", NULL);
  //vfree(&mylist); LA CABEZA NO ESTÁ EN MEMORIA DINÁMICA, CON HACER UN CLEANUP VALE
  modlist_cleanup(); // <- porque los demás nodos sí están en memoria dinámica pero mylist en la pila
  
  printk(KERN_INFO "multilist: Module unloaded.\n");
  //module_put(THIS_MODULE);
}


static ssize_t control_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
  char modlistbuffer[BUFFER_LENGTH];
  int available_space = BUFFER_LENGTH-1;
  char name[ENTRY_NAME_LENGTH];
  
  if ((*off) > 0) /* The application can write in this entry just once !! */
    return 0;
  
  if (len > available_space) {
    printk(KERN_INFO "multilist: not enough space!!\n");
    return -ENOSPC;
  }
  
  /* Transfer data from user to kernel space */
  if (copy_from_user( &modlistbuffer[0], buf, len ))  
    return -EFAULT;

  modlistbuffer[len] = '\0'; /* Add the `\0' */ 

  if(sscanf(modlistbuffer, "create %s", name) == 1) {
    control_add(name);
	}
  else if(sscanf(modlistbuffer, "remove %s", name) == 1) {
    control_remove(name);
	}

  *off+=len; /* Update the file pointer */

  return len;
}

static ssize_t control_add(char* name) {
  if (proc_create(name, 0666, proc_dir, &proc_entry_fops) == NULL)
      return -ENOMEM;
  
  return 0;
}

static void control_remove(char* name) {
  remove_proc_entry(name, proc_dir);
}

static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
  char modlistbuffer[BUFFER_LENGTH];
  struct list_item* item=NULL;
  struct list_head* cur_node=NULL;
  ssize_t buf_length = 0;
  
  if ((*off) > 0) /* Tell the application that there is nothing left to read */
    return 0;
    
  if (len<1)
    return -ENOSPC;

  spin_lock(&sp);
  /* copiar los datos al buffer de modlist */
  list_for_each(cur_node, &mylist) {
    /* item points to the structure wherein the links are embedded */
    if(buf_length >= BUFFER_LENGTH-1){ // TODO sprintf de lo que hay en el buffer +  lo que leemos
    	break;
    }
    item = list_entry(cur_node, struct list_item, links);
    buf_length += sprintf(modlistbuffer + buf_length, "%i\n", item->data);
  }

  spin_unlock(&sp);

  /* Transfer data from the kernel to userspace  */
  if (copy_to_user(buf, modlistbuffer, buf_length))
    return -EINVAL;
    

  (*off)+=len;  /* Update the file pointer */

  return buf_length;
}

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
  char modlistbuffer[BUFFER_LENGTH];
  int available_space = BUFFER_LENGTH-1;
  int num = 0;
  
  if ((*off) > 0) /* The application can write in this entry just once !! */
    return 0;
  
  if (len > available_space) {
    printk(KERN_INFO "multilist: not enough space!!\n");
    return -ENOSPC;
  }
  
  /* Transfer data from user to kernel space */
  if (copy_from_user( &modlistbuffer[0], buf, len ))  
    return -EFAULT;

  modlistbuffer[len] = '\0'; /* Add the `\0' */ 

  if(sscanf(modlistbuffer, "add %i", &num) == 1) {
    modlist_add(num);
	}
  else if(sscanf(modlistbuffer, "remove %i", &num) == 1) {
    modlist_remove(num);
	}
  else if(strcmp(modlistbuffer, "cleanup\n") == 0) {
      modlist_cleanup();
	}

  *off+=len;           /* Update the file pointer */

  return len;
}

static void modlist_add(int num) {
  struct list_item* nodo = vmalloc(sizeof(struct list_item));
  
  nodo->data = num;

  spin_lock(&sp);
  list_add_tail(&nodo->links,&mylist);
  spin_unlock(&sp); 
}

static void modlist_remove(int num) {
  struct list_item* item=NULL;
  struct list_head* cur_node=NULL;
  struct list_head* aux=NULL;
  
  spin_lock(&sp); 
  list_for_each_safe(cur_node, aux, &mylist){
  	item = list_entry(cur_node, struct list_item, links);

  	if(item->data == num) {
  		list_del(cur_node);
        vfree(item);
    }
  }
  spin_unlock(&sp); 
}

static void modlist_cleanup(void) {
  struct list_item* item=NULL;
  struct list_head* cur_node=NULL;
  struct list_head* aux=NULL;
  
  spin_lock(&sp); 
  list_for_each_safe(cur_node, aux, &mylist){
    item = list_entry(cur_node, struct list_item, links);

    list_del(cur_node);
    vfree(item);
  }
  spin_unlock(&sp);
}

// TODO llevar una lista con las entradas creadas para:
//	- al borrar una entrada que no exista no pete
//	- limpiar todas las entradas (y por tanto las listas) al quitar el módulo


module_init( init_modlist_module );
module_exit( exit_modlist_module );
