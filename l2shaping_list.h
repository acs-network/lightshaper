#ifndef _L2SHAPING_LIST_H_
#define _L2SHAPING_LIST_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>

#define MEMPOOL_CACHE_SIZE 256

struct linked_list {
    struct list_node *head;
    struct list_node *tail;
};

/*mbuf linked list node. */
struct list_node {
    struct rte_mbuf* data;
    struct list_node *prev;
    struct list_node *next;
    struct timeval timestamp;
};

struct rte_mempool *delay_list_pool;
struct linked_list *delay_process_list;

void
list_node_init(struct rte_mempool *mp __rte_unused,
		 __attribute__((unused)) void *opaque_arg,
		 void *_m,
		 __attribute__((unused)) unsigned i)
{
	struct list_node *m = _m;
	uint32_t mbufsus_size;

	mbufsus_size = sizeof(struct list_node);
	memset(m, 0, mbufsus_size);
	m->data = NULL;
	m->prev = NULL;
	m->next = NULL;
	//m->timestamp = NULL;
}

bool list_pool_init(){
	unsigned int nb_mbufs = 81920U;
	int socket_id = 0;
	const char *mp_ops_name;
	unsigned elt_size;
	int ret;
	elt_size=sizeof(struct list_node);
	delay_list_pool=rte_mempool_create_empty("delay_list_pool", nb_mbufs, elt_size, MEMPOOL_CACHE_SIZE,0, socket_id, 0);
	if (delay_list_pool == NULL)
		return false;

	/* set it to single producer single consumer(variable ops_sp_sc) */
	mp_ops_name = "ring_sp_sc";
	ret = rte_mempool_set_ops_byname(delay_list_pool, mp_ops_name, NULL);
	if (ret != 0) {
		fprintf(stderr, "error setting mempool handler\n");
		rte_mempool_free(delay_list_pool);
		return false;
	}
	/*Add memory for objects in the pool at init*/
	ret = rte_mempool_populate_default(delay_list_pool);
	if (ret < 0) {
		rte_mempool_free(delay_list_pool);
		return false;
	}

	rte_mempool_obj_iter(delay_list_pool, list_node_init, NULL);
	return true;
}

struct list_node *list_node_alloc(struct rte_mbuf* data,struct timeval *timestamp){
	struct list_node *mn;
	void *mb = NULL;
	
	if (rte_mempool_get(delay_list_pool, &mb) < 0)
		return NULL;
	mn = (struct list_node *)mb;
	
	mn->data = data;
    mn->next = NULL;
    mn->prev = NULL;
	mn->timestamp = *timestamp;
	return mn;
}

bool is_empty_list(struct linked_list *list)
{
	return (list->head == NULL && list->tail == NULL);
}

bool list_push(struct linked_list *list, struct list_node* mn){
	if (mn == NULL) {
		return false;
	}

	if (is_empty_list(list)) {
		/* An empty list. */
		list->head = list->tail = mn;
		return true;
	}
	/* Link a node to tail of doubly linked list. */
	list->tail->next = mn;
	mn->prev = list->tail;
	list->tail = mn;

	return true;
}

struct rte_mbuf* list_pop(struct linked_list *list){

	if (is_empty_list(list)) {
		return NULL;
	}
	struct list_node *tmp = list->head;
	struct list_node *mn = list->head;

	mn = mn->next;
	
	if (mn != NULL) {
		list->head = mn;
		mn->prev->next = NULL;
		list->head->prev = NULL;

	} else {
		list->head = NULL;
		list->tail = NULL;
	}
	return tmp;
}

bool timestamp_timeout(struct linked_list *list){
	struct timeval now;
	gettimeofday(&now,NULL);
	if(list->head->timestamp.tv_sec>now.tv_sec)
		return true;
	else if((list->head->timestamp.tv_sec==now.tv_sec)&&(list->head->timestamp.tv_usec>=now.tv_usec))
		return true;
	else
		return false;
}
#endif