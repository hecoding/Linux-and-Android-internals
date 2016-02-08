#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm-generic/uaccess.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/errno.h>

MODULE_LICENSE("GPL");

#define BUFFER_LENGTH 240
#define ENTRY_NAME_LENGTH 20

static struct proc_dir_entry *proc_control_entry;
static struct proc_dir_entry *proc_dir=NULL;

/* List nodes */
typedef struct list_item {
	int data;
	struct list_head links;
} list_item_t;

typedef struct proc_list_head {
	spinlock_t sp;
	struct list_head head;
	char name[ENTRY_NAME_LENGTH];
} proc_list_t;

/* Proc list nodes of lists */
typedef struct list_proc_item {
	struct proc_list_head* pl;
	struct list_head links;
} list_proc_item_t;

struct list_head list_of_proc_lists;
DEFINE_SPINLOCK(sp_lists);

static ssize_t control_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t control_create(char* name);
static void control_remove(char* name);
static void control_cleanup(void);
static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static void modlist_add(int num, struct proc_list_head* pl);
static void modlist_remove(int num, struct proc_list_head* pl);
static void modlist_cleanup(struct proc_list_head* pl);

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
  /* Init list of proc entries */
  INIT_LIST_HEAD( &list_of_proc_lists );
  if (!list_empty(&list_of_proc_lists))
    return -ENOMEM;

  /* Create proc directory */
  proc_dir=proc_mkdir("multilist",NULL);

  if (!proc_dir)
      return -ENOMEM;

  /* Create proc entry /proc/multilist/control */
  proc_control_entry = proc_create( "control", 0666, proc_dir, &proc_control_fops);
  /* Add default entry */
  control_create("default");

  if (proc_control_entry == NULL) {
      remove_proc_entry("multilist", NULL);
      printk(KERN_INFO "multilist: Can't create /proc entry\n");
      return -ENOMEM;
  }

  printk(KERN_INFO "multilist: Module loaded\n");
  //try_module_get(THIS_MODULE);

  return ret;
}

void exit_modlist_module( void )
{
  remove_proc_entry("control", proc_dir);
  remove_proc_entry("multilist", NULL);
  //vfree(&mylist); LA CABEZA NO ESTÁ EN MEMORIA DINÁMICA, CON HACER UN CLEANUP VALE
  //modlist_cleanup(); // <- porque los demás nodos sí están en memoria dinámica pero mylist en la pila
  
  control_cleanup();
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
    control_create(name);
	}
  else if(sscanf(modlistbuffer, "remove %s", name) == 1) {
    control_remove(name);
	}

  *off+=len; /* Update the file pointer */

  return len;
}

static ssize_t control_create(char* name) {
  /* Create data for the proc entry */
  struct proc_list_head* pl = vmalloc(sizeof(struct proc_list_head));
  INIT_LIST_HEAD( &pl->head );
  spin_lock_init( &pl->sp );
  strncpy(pl->name, name, ENTRY_NAME_LENGTH);

  if (!list_empty(&pl->head))
    return -ENOMEM;

  proc_create_data(name, 0666, proc_dir, &proc_entry_fops, pl);
  
  /* Create list_of_lists entry */
  struct list_proc_item* nodo = vmalloc(sizeof(struct list_proc_item));
  nodo->pl = pl;

  spin_lock(&sp_lists);
  list_add_tail(&nodo->links,&list_of_proc_lists);
  spin_unlock(&sp_lists);
  
  return 0;
}

static void control_remove(char* name) {
  struct list_proc_item* item=NULL;
  struct list_head* cur_node=NULL;
  struct list_head* aux=NULL;
  int found = 0;
  
  /* Remove from list of lists */
  spin_lock(&sp_lists); 
  list_for_each_safe(cur_node, aux, &list_of_proc_lists){
  	item = list_entry(cur_node, struct list_proc_item, links);

  	if(strcmp(item->pl->name, name) == 0) {
  		list_del(cur_node);

  		/* --- Remove item (actually the list) */
  		modlist_cleanup(item->pl);
  		vfree(item->pl);
  		// ---

        vfree(item);
        found = 1;
    }
  }
  spin_unlock(&sp_lists);

  /* Remove entry */
  remove_proc_entry(name, proc_dir);

  if(!found) {
  	//fprintf(stderr, "Write error: entry doesn't exist");
    printk(KERN_INFO "multilist: non-existing entry remove\n");
  }
}

static void control_cleanup(void) {
  struct list_proc_item* item=NULL;
  struct list_head* cur_node=NULL;
  struct list_head* aux=NULL;
  
  spin_lock(&sp_lists); 
  list_for_each_safe(cur_node, aux, &list_of_proc_lists){
    item = list_entry(cur_node, struct list_proc_item, links);

    list_del(cur_node);

    /* --- Remove item (actually the list) */
	modlist_cleanup(item->pl);
	vfree(item->pl);
	// ---

    vfree(item);
  }
  spin_unlock(&sp_lists);

  /* Remove entry */
  //remove_proc_entry(name, proc_dir);
}

static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
  struct proc_list_head* pl = (struct proc_list_head*) PDE_DATA(filp->f_inode);
  spinlock_t sp = pl->sp;
  struct list_head mylist = pl->head;
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
  struct proc_list_head* pl = (struct proc_list_head*) PDE_DATA(filp->f_inode);
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
    modlist_add(num, pl);
	}
  else if(sscanf(modlistbuffer, "remove %i", &num) == 1) {
    modlist_remove(num, pl);
	}
  else if(strcmp(modlistbuffer, "cleanup\n") == 0) {
      modlist_cleanup(pl);
	}

  *off+=len;           /* Update the file pointer */

  return len;
}

static void modlist_add(int num, struct proc_list_head* pl) {
  spinlock_t sp = pl->sp;
  struct list_head mylist = pl->head;
  struct list_item* nodo = vmalloc(sizeof(struct list_item));
  
  nodo->data = num;

  spin_lock(&sp);
  list_add_tail(&nodo->links,&mylist);
  spin_unlock(&sp); 
}

static void modlist_remove(int num, struct proc_list_head* pl) {
  spinlock_t sp = pl->sp;
  struct list_head mylist = pl->head;
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

static void modlist_cleanup(struct proc_list_head* pl) {
  printk(KERN_INFO "multilist: entra en cleanup\n");
  printk(KERN_INFO "multilist: pl %u\n", pl);
  spinlock_t sp = pl->sp;
  struct list_head mylist = pl->head;
  struct list_item* item=NULL;
  struct list_head* cur_node=NULL;
  struct list_head* aux=NULL;
  printk(KERN_INFO "multilist: hasta aqui bien\n");
  printk(KERN_INFO "multilist: sp %u\n", sp);
  printk(KERN_INFO "multilist: head %u\n", mylist);
  spin_lock(&sp); 
  list_for_each_safe(cur_node, aux, &mylist){
  	printk(KERN_INFO "multilist: entra en el foreach\n");
    item = list_entry(cur_node, struct list_item, links);

    list_del(cur_node);
    vfree(item);
  }
  spin_unlock(&sp);
}

// TODO limpiar todas las entradas con remove_proc_entry en control_cleanup
// TODO limpiar código


module_init( init_modlist_module );
module_exit( exit_modlist_module );
