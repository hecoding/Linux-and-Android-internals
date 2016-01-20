1
2
3
4
5
6
7
8
9
10
11
12
13
14
15
16
17
18
19
20
21
22
23
24
25
26
27
28
29
30
31
32
33
34
35
36
37
38
39
40
41
42
43
44
45
46
47
48
49
50
51
52
53
54
55
56
57
58
59
60
61
62
63
64
65
66
67
68
69
70
71
72
73
74
75
76
77
78
79
80
81
82
83
84
85
86
87
88
89
90
91
92
93
94
95
96
97
98
99
100
101
102
103
104
105
106
107
108
109
110
111
112
113
114
115
116
117
118
119
120
121
122
123
124
125
126
127
128
129
130
131
132
133
134
135
136
137
138
139
140
141
142
143
144
145
146
147
148
149
150
151
152
153
154
155
156
157
158
159
160
161
162
163
164
165
166
167
168
169
170
171
172
173
174
175
176
177
178
179
180
181
182
183
184
185
186
187
188
189
190
191
192
193
194
195
196
197
198
199
200
201
202
203
204
205
206
207
208
209
210
211
212
213
214
215
216
217
218
219
220
221
222
223
224
225
226
227
228
229
230
231
232
233
234
235
236
237
238
239
240
241
242
243
244
245
246
247
248
249
250
251
252
253
254
255
256
257
258
259
260
261
262
263
264
265
266
267
268
269
270
271
272
273
274
275
276
277
278
279
280
281
282
283
284
285
286
287
288
289
290
291
292
293
294
295
296
297
298
299
300
301
302
303
304
305
306
307
308
309
310
311
312
313
314
315
316
317
318
319
320
321
322
323
324
325
326
327
328
329
330
331
332
333
334
335
336
337
338
339
340
341
342
343
344
345
346
347
348
349
350
351
352
353
354
355
356
357
358
359
360
361
362
363
364
365
366
367
368
369
370
371
372
373
374
375
376
377
378
379
380
381
382
383
384
385
386
387
388
389
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
 
#include <linux/random.h>
#include "cbuffer.h"
#include <asm-generic/uaccess.h>
#include <asm-generic/errno.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
 
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/semaphore.h>
 
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("timermod Module");
MODULE_AUTHOR("L C & Y E");
 
#define MAX_ITEMS_CBUF  10
#define BUF_LEN         100
DEFINE_SPINLOCK(sp);
 
struct timer_list my_timer; /* Structure that describes the kernel timer */
 
static cbuffer_t* cbuf; /* Buffer circular compartido */
 
unsigned int timer_period_ms=1000;
unsigned int max_random=300;
unsigned int emergency_threshold=80;
 
static struct proc_dir_entry *proc_config;
static struct proc_dir_entry *proc_timer;
 
/* Work descriptor */
struct work_struct transfer_task;
 
/* Nodos de la lista */
typedef struct {
  unsigned int data;
  struct list_head links;
}list_item;
 
struct list_head mylist; /* Lista enlazada */
 
struct semaphore mtx; /* para garantizar Exclusión Mutua */
struct semaphore sem_mtx; /* cola de espera */
int nr_waiting=0;
 
/* Function invoked when timer expires (fires) */
static void fire_timer(unsigned long data)
{
    unsigned long flags = 0;
    unsigned int num = 0;
     
    num = get_random_int()%max_random;
    printk("Generando numero: %u\n", num);
     
    spin_lock_irqsave(&sp, flags);
    insert_cbuffer_t(cbuf, num);
    spin_unlock_irqrestore(&sp, flags);
     
    if(!work_pending(&transfer_task) && size_cbuffer_t(cbuf)==((emergency_threshold*MAX_ITEMS_CBUF)/100)){  
        printk("%i numeros movidos de buffer a lista\n", size_cbuffer_t(cbuf));
        /* Enqueue work */
        schedule_work_on((smp_processor_id()+1)%2, &transfer_task);
    }
     
    /* Re-activate the timer timer_period_ms from now */
    mod_timer( &(my_timer), jiffies + msecs_to_jiffies(timer_period_ms)); 
}
 
int list_insert(unsigned int nums[], int cont)
{
    int i;
     
    /* "Adquiere" el mutex */
    if (down_interruptible(&mtx))
        return -EINTR;
     
    for(i=0; i<cont; i++){
        list_item* nodo = vmalloc(sizeof(list_item));
        nodo->data = nums[i];
        list_add_tail(&nodo->links, &mylist);
    }
     
    if(!list_empty(&mylist) && nr_waiting > 0){
        up(&sem_mtx);
        nr_waiting--;
    }
     
    /* "Libera" el mutex */
    up(&mtx);
     
    return 0;
}
 
/* Work's handler function */
static void copy_items_into_list(struct work_struct *work)
{
    unsigned long flags = 0;
    unsigned int nums[10];
    int cont = 0;
     
    while(!is_empty_cbuffer_t(cbuf)){
        spin_lock_irqsave(&sp, flags);
        /* Obtener el primer elemento del buffer y eliminarlo */
        nums[cont] = head_cbuffer_t(cbuf);
        remove_cbuffer_t(cbuf);
        cont++;
        spin_unlock_irqrestore(&sp, flags);
    }
     
    list_insert(nums, cont);
}
 
static ssize_t modconfig_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    char kbuf[BUF_LEN] = "";
    char* dest = kbuf;
    int tam = 0;
 
    if((*off) > 0) /* Tell the application that there is nothing left to read */
        return 0;
 
    if(len < 1)
        return -ENOSPC;
 
    dest += sprintf(dest, "timer_period_ms=%i\nemergency_threshold=%i\nmax_random=%i\n", timer_period_ms, emergency_threshold, max_random);
    tam=dest-kbuf;
     
    /* Transfer data from the kernel to userspace */
    if (copy_to_user(buf, kbuf, tam))
        return -EINVAL;
 
    (*off)+=len;  /* Update the file pointer */
 
    return tam;
}
 
/* Se invoca al hacer write() de entrada /proc */
static ssize_t modconfig_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
    char kbuf[BUF_LEN] = "";
    unsigned int num = 0;
 
    if(len > BUF_LEN)
        return -ENOSPC;
         
    if(copy_from_user(kbuf, buf, len))
        return -EFAULT;
 
    kbuf[len] ='\0';
    *off+=len; /* Update the file pointer */
 
    if(sscanf(kbuf, "timer_period_ms %i", &num) == 1) {
        timer_period_ms = num;
    }
     
    if(sscanf(kbuf, "emergency_threshold %i", &num) == 1) {
        if (num <= 100 && num >= 1)
            emergency_threshold = num;
    }
     
    if(sscanf(kbuf, "max_random %i", &num) == 1) {
        max_random = num;
    }
 
    return len;
}
 
/* Se invoca al hacer open() de entrada /proc */
static int modtimer_open(struct inode *inode, struct file *file)
{
    /* "Adquiere" el mutex */
    /*if(down_interruptible(&mtx))
        return -EINTR;
     
    if(file->f_mode & FMODE_READ){*/
        my_timer.expires=jiffies + msecs_to_jiffies(timer_period_ms);  /* Activate it timer_period_ms from now */
        /* Activate the timer for the first time */
        add_timer(&my_timer);
         
        //if(nr_waiting > 0) {
            /* Despierta a uno de los hilos bloqueados */
            /*up(&sem_mtx);
            nr_waiting--;
        }
 
        while(list_empty(&mylist)){        
            nr_waiting++;
            *///up(&mtx);  /* "Libera" el mutex */
            /* Se bloquea en la cola */
            /*if(down_interruptible(&sem_mtx)){
                down(&mtx);
                nr_waiting--;
                up(&mtx);
                return -EINTR;
            }*/
            /* "Adquiere" el mutex */  
            /*if(down_interruptible(&mtx))
                return -EINTR;
        }
    }*/
    /* "Libera" el mutex */
    //up(&mtx);
     
    try_module_get(THIS_MODULE);
     
    return 0;
}
 
int list_cleanup(void)
{
    list_item* item = NULL;
    struct list_head* cur_node = NULL;
    struct list_head* aux = NULL;
 
    /* "Adquiere" el mutex */
    if(down_interruptible(&mtx))
        return -EINTR;
     
    list_for_each_safe(cur_node, aux, &mylist){
        item = list_entry(cur_node, list_item, links);
        list_del(cur_node);
        vfree(item);
    }
     
    /* "Libera" el mutex */
    up(&mtx);
     
    return 0;
}
 
/* Se invoca al hacer close() de entrada /proc */
static int modtimer_release(struct inode *inode, struct file *file)
{   
    /* "Adquiere" el mutex */
    /*if(down_interruptible(&mtx))
        return -EINTR;
 
    if(file->f_mode & FMODE_READ){*/
        /* Un consumidor salió del FIFO */
        //if(nr_waiting > 0) {
            /* Despierta a uno de los hilos bloqueados */
            /*up(&sem_mtx);
            nr_waiting--;
        }
    }*/
     
    /* Wait until completion of the timer function (if it's currently running) and delete timer */
    del_timer_sync(&my_timer);
    flush_scheduled_work();
    clear_cbuffer_t(cbuf);
    list_cleanup();
     
    /* "Libera" el mutex */
    //up(&mtx);
     
    module_put(THIS_MODULE);
     
    return 0;
}
 
static ssize_t modtimer_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    char kbuf[BUF_LEN] = "";
    char* dest = kbuf;
    int tam = 0;
    list_item* item=NULL;
    struct list_head* cur_node=NULL;
    struct list_head* aux = NULL;
 
    if(len < 1)
        return -ENOSPC;
     
    /* "Adquiere" el mutex */
    if(down_interruptible(&mtx))
        return -EINTR;
     
    while(list_empty(&mylist)){
        nr_waiting++;
        up(&mtx);
        if(down_interruptible(&sem_mtx)){
            down(&mtx);
            nr_waiting--;
            up(&mtx);       
            return -EINTR;
        }
         
        if(down_interruptible(&mtx))
            return -EINTR;
    }
     
    list_for_each_safe(cur_node, aux, &mylist) {
        /* item points to the structure wherein the links are embedded */
        item = list_entry(cur_node, list_item, links);
        dest += sprintf(dest, "%u\n", item->data);
        list_del(cur_node);
        vfree(item);
    } 
     
     
    /* "Libera" el mutex */
    up(&mtx);
 
    tam=dest-kbuf;
 
    if (copy_to_user(buf,kbuf,tam)) { return -EFAULT;}
 
    (*off)+=len;  /* Update the file pointer */
     
    return tam;
}
 
static const struct file_operations proc_config_fops = {
    .read = modconfig_read,
    .write = modconfig_write
};
 
static const struct file_operations proc_timer_fops = {
    .open = modtimer_open,
    .release = modtimer_release,
    .read = modtimer_read
};
 
int init_timer_module(void)
{
    proc_config = proc_create_data("modconfig",0666, NULL, &proc_config_fops, NULL);
 
    if(proc_config == NULL){
        printk(KERN_INFO "Configmod: No puedo crear la entrada en proc\n");
        return  -ENOMEM;
    }
     
    proc_timer = proc_create_data("modtimer",0666, NULL, &proc_timer_fops, NULL);
 
    if(proc_timer == NULL){
        printk(KERN_INFO "Timermod: No puedo crear la entrada en proc\n");
        return  -ENOMEM;
    }
     
    /* Create timer */
    init_timer(&my_timer);
    /* Initialize field */
    my_timer.data=0;
    my_timer.function=fire_timer;
    my_timer.expires=0;
     
    /* Inicialización del buffer */
    cbuf = create_cbuffer_t(MAX_ITEMS_CBUF);
 
    if (!cbuf) {
        return -ENOMEM;
    }
 
    INIT_WORK(&transfer_task, copy_items_into_list);
     
    INIT_LIST_HEAD(&mylist);
 
    if (!list_empty(&mylist)) {
        return -ENOMEM;
    }
     
    /* Inicialización a 0 de los semáforos usados como colas de espera */
    sema_init(&sem_mtx, 0);
 
    /* Inicializacion a 1 del semáforo que permite acceso en exclusión mutua a la SC */
    sema_init(&mtx, 1);
     
    nr_waiting = 0;
     
    printk(KERN_INFO "Timermod: Modulo cargado.\n");
     
    return 0;
}
 
void cleanup_timer_module(void)
{   
    destroy_cbuffer_t(cbuf);
    remove_proc_entry("modconfig", NULL);
    remove_proc_entry("modtimer", NULL);
    printk(KERN_INFO "Timermod: Modulo descargado.\n");
}
 
module_init(init_timer_module);
module_exit(cleanup_timer_module);
