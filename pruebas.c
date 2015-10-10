#include <stdio.h>
#include "list.h"

struct list_head mylist;
/* Lista enlazada */
/* Nodos de la lista */
typedef struct {
	int data;
	struct list_head links;
} list_item_t;

int main() {
	printf("afa\n");
	INIT_LIST_HEAD(mylist);
	list_item_t* ptr;
	int i=0;

	for (i=0;i<20;i++){
		ptr=malloc(sizeof(list_item_t));
		list_add_tail(&ptr->links,&mylist);
	}	
	 

	return 0;
}
