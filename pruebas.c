#include <stdio.h>
#include "list.h"
#include <stdlib.h>

/* Lista enlazada */
struct list_head mylist;
/* Nodos de la lista */
typedef struct {
	int data;
	struct list_head links;
} list_item_t;

void blah(void);

int main() {
	blah();
	 

	return 0;
}

void populate(struct list_head* list) {
	list_item_t* ptr;
	int i=0;
	int NUM = 20;

	for (i=0;i<NUM;i++){
		ptr=malloc(sizeof(list_item_t));
		ptr->data = i;
		list_add_tail(&ptr->links,&mylist);
	}
}

void introducir_profesor(struct list_head* list) {
	list_item_t* ptr;
	int i=0;
	int NUM = 20;

	for (i=0;i<NUM;i++){
		ptr=malloc(sizeof(list_item_t));
		list_add_tail(&ptr->links,&mylist);
	}
}

void print_list(struct list_head* list) {
	list_item_t* item=NULL;
	struct list_head* cur_node=NULL;
	list_for_each(cur_node, list) {
	/* item points to the structure wherein the links are embedded */
		item = list_entry(cur_node, list_item_t, links);
		printf("%i \n", item->data);
	}
}

void blah(void) {
	INIT_LIST_HEAD(&mylist); /* Initialize the list */
	populate(&mylist);	/* Populate the list */
	print_list(&mylist);
}