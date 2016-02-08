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

static struct proc_dir_entry *proc_control_entry;
static struct proc_dir_entry *proc_dir=NULL;

/* list items */
typedef struct {
    int data;
    struct list_head links;
} list_item_t;

/* proc entry items */
typedef struct {
    struct proc_dir_entry* proc_entry;
    struct list_head list;
    struct list_head links;
    spinlock_t sp;
    char name[ENTRY_NAME_LENGTH];
    int marked_for_removal; // better than sudden removal
} entry_list_node_t;

struct list_head list_of_proc_lists;
DEFINE_SPINLOCK(sp_lists);

static ssize_t control_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);
static ssize_t control_create(char* name);
static void control_remove(entry_list_node_t *modlist_entry);
static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off);
static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off);

static const struct file_operations proc_control_fops = {
    .write = control_write,    
};

static const struct file_operations proc_entry_fops = {
    .read = modlist_read,
    .write = modlist_write,    
};

int init_modlist_module( void )
{
  /* Init list of proc entries */
  INIT_LIST_HEAD(&list_of_proc_lists);
  spin_lock_init(&sp_lists);

  /* Create proc directory */
  proc_dir = proc_mkdir("multilist", NULL);
  if (proc_dir == NULL) {
    printk(KERN_INFO "multilist: Can't create /proc directory\n");
    return -ENOMEM;
  }

  /* Create proc entry /proc/multilist/control */
  proc_control_entry = proc_create("control", 0666, proc_dir, &proc_control_fops);
  if (proc_control_entry == NULL) {
    printk(KERN_INFO "multilist: Can't create 'control' entry\n");
    return -ENOMEM;
  }

  control_create("default");

  printk(KERN_INFO "multilist: Module loaded.\n");
  try_module_get(THIS_MODULE);

  return 0;
}

void exit_modlist_module( void )
{
  entry_list_node_t *tmp, *pos;

  remove_proc_entry("control", proc_dir);
  remove_proc_entry("multilist", NULL);

  /* Remove entries that haven't been manually deleted */
  list_for_each_entry_safe(pos, tmp, &list_of_proc_lists, links) {
    control_remove(pos);
  }
    
  printk(KERN_INFO "multilist: Module unloaded.\n");
  module_put(THIS_MODULE);
}


static ssize_t control_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
  char aux_buffer[BUFFER_LENGTH];
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
  if (copy_from_user(aux_buffer, buf, len))
    return -EFAULT;
  
  aux_buffer[len] = '\0';

  if(sscanf(modlistbuffer, "create %s", name) == 1) {
    control_create(name);
  }
  else if (sscanf(modlistbuffer, "remove %s", name) == 1) {
    int found = 0;
    entry_list_node_t *modlist_entry, *temp;

    spin_lock(&sp_lists);
    list_for_each_entry_safe(modlist_entry, temp, &list_of_proc_lists, links) {
      if (!strcmp(modlist_entry->name, name)) {
          found = 1;
          break;
      }
    }
    if (!found) {
      printk(KERN_INFO "multilist: non-existing entry remove\n");
      spin_unlock(&sp_lists);
      return -ENOENT;
    }

    /* It's better to have a marked so that different processes
    *  won't cause deadlock to each other */
    spin_lock(&modlist_entry->sp);
    if (modlist_entry->marked_for_removal) {
      spin_unlock(&modlist_entry->sp);
      spin_unlock(&sp_lists);
      return len;
    }
    modlist_entry->marked_for_removal = 1;
    spin_unlock(&modlist_entry->sp);
    
    spin_unlock(&sp_lists);
    control_remove(modlist_entry);
  }

  (*off)+=len; /* Update the file pointer */

  return len;
}

static ssize_t control_create(char* name) {
  entry_list_node_t *entry_node = vmalloc(sizeof(entry_list_node_t));;
  /* Create data for the proc entry */
  INIT_LIST_HEAD(&entry_node->list);

  spin_lock_init(&entry_node->sp);
  entry_node->proc_entry = NULL;
  entry_node->marked_for_removal = 0;
  strncpy(entry_node->name, name, ENTRY_NAME_LENGTH);

  entry_node->proc_entry = proc_create_data(name, 0666, proc_dir, &proc_entry_fops, entry_node);
  
  if (entry_node->proc_entry == NULL) {
    spin_lock(&sp_lists);
    vfree(entry_node);
    return 0;
  }

  spin_lock(&sp_lists);
  list_add_tail(&entry_node->links, &list_of_proc_lists); 
  spin_unlock(&sp_lists);

  return 0;
}

static void control_remove(entry_list_node_t *modlist_entry) {
  list_item_t *pos;
  list_item_t *temp;

  remove_proc_entry( modlist_entry->name, proc_dir);

  /* Remove from list of lists */
  spin_lock(&sp_lists);
  list_del(&modlist_entry->links);
  spin_unlock(&sp_lists);

  spin_lock(&modlist_entry->sp);
  list_for_each_entry_safe(pos, temp, &modlist_entry->list, links) {
    list_del(&pos->links);
    vfree(pos);
  }
  spin_unlock(&modlist_entry->sp);

  vfree(modlist_entry);
}

static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
  list_item_t *pos;
  int nchars, nbytes, ret = 0;
  char *buff_pos = buf;

  entry_list_node_t *entry_node;
  char int_buf[20];  

  if ((*off) > 0) /* Tell the application that there is nothing left to read */
    return 0;

  entry_node = (entry_list_node_t*)PDE_DATA(filp->f_inode);

  spin_lock(&sp_lists);
  spin_lock(&entry_node->sp);
  /* copiar los datos al buffer de modlist */
  list_for_each_entry(pos, &entry_node->list, links) {
    nchars = sprintf(int_buf, "%d\n", pos->data);
    nbytes = nchars*sizeof(char); // with the \n char
    if ( (nbytes) > (len-ret) ) // if there is space in buffer
      break;

    /* Transfer data from the kernel to userspace  */ 
    if( copy_to_user(buff_pos, int_buf, nbytes) ) {
      spin_unlock(&entry_node->sp);
      spin_unlock(&sp_lists);
      return -EFAULT;
    }

    buff_pos += nchars;
    ret += nbytes;
  }

  spin_unlock(&entry_node->sp);
  spin_unlock(&sp_lists);    

  (*off)+=ret;  /* Update the file pointer */

  return ret;
}

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
  char aux_buffer[BUFFER_LENGTH] = "\0";
  char modlistbuffer[BUFFER_LENGTH];
  list_item_t *pos, *temp;
  entry_list_node_t *entry_node;
  int num = 0;
  
  if ((*off) > 0) /* The application can write in this entry just once !! */
    return 0;

  entry_node = (entry_list_node_t*)PDE_DATA(filp->f_inode);
  
  /* Transfer data from user to kernel space */
  if (copy_from_user(aux_buffer, buf, len)) {
    return -EFAULT;
  }

  aux_buffer[len] = '\0'; /* Add the `\0' */

  // optamos por no hacer métodos para cada operación porque no merece la pena
  if(sscanf(modlistbuffer, "add %i", &num) == 1) {

    temp = vmalloc(sizeof(list_item_t));
    temp->data = num;

    spin_lock(&entry_node->sp);
    list_add_tail(&temp->links, &entry_node->list);
	spin_unlock(&entry_node->sp);

	}
  else if(sscanf(modlistbuffer, "remove %i", &num) == 1) {

    spin_lock(&entry_node->sp);
    list_for_each_entry_safe(pos, temp, &entry_node->list, links) {
        if (pos->data == num) {          
            list_del(&pos->links);
            vfree(pos);
        }
    }
    spin_unlock(&entry_node->sp);

  }
  else if(strcmp(modlistbuffer, "cleanup\n") == 0) {

    spin_lock(&entry_node->sp);
    list_for_each_entry_safe(pos, temp, &entry_node->list, links) {
        list_del(&pos->links);
        vfree(pos);
    }
    spin_unlock(&entry_node->sp);

  }

  (*off)+=len; /* Update the file pointer */

  return len;
}


module_init( init_modlist_module );
module_exit( exit_modlist_module );
