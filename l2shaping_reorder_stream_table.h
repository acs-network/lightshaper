#include "l2shaping_policy.h"
#include "l2shaping_rbtree.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "l2shaping_stack.h"

#define HASH_STREAM_TABLE_SIZE 1024
#define BUCKET_CAPACITY 16

typedef struct rte_mbuf* StackDateType;
typedef uint32_t KeyType;
typedef struct rte_mbuf ValueType;

typedef struct hash_stream_table{
    uint32_t size;
    //map_t bucket[];
    ts_mbuf_stack **stacks;
}reorder_table_t;

reorder_table_t *reorder_table;

#if 0
typedef struct map_elem {
    struct rb_node node;
    KeyType *key;
    ValueType *val;
}map_elem_t;

typedef struct map {
    root_t root;
    uint16_t size;
    uint16_t capacity;
}map_t;

map_elem_t *get(root_t *root, KeyType *key) 
{
   rb_node_t *node = root->rb_node; 
   while (node) {
        map_elem_t *data = container_of(node, map_elem_t, node);
        //compare between the key with the keys in map
        //int cmp = strcmp(key, data->key);
        if (key < data->key) {
            node = node->rb_left;
        }else if (key > data->key) {
            node = node->rb_right;
        }else {
            return data;
        }
   }
   return NULL;
}

int get_and_del(map_t *map/*root_t *root*/, KeyType *key,map_elem_t * elem) 
{
    if(map->size==0){
        return -1;
    }
    rb_node_t *node = map->root->rb_node; 
    while (node) {
        map_elem_t *data = container_of(node, map_elem_t, node);
        //compare between the key with the keys in map
        //int cmp = strcmp(key, data->key);
        if (key < data->key) {
            node = node->rb_left;
        }else if (key > data->key) {
            node = node->rb_right;
        }else {
            elem->key=data->key;
            elem->value=data->value;
            rb_erase(&data->node, root);
            map_elem_free(data);
            map->size--;
            return 0;
        }
   }
   return -1;
}

int put(map_t *map/*root_t *root*/, KeyType* key, ValueType* val) 
{
    if(map->size==map->capacity){
        return -1;
    }
    map_elem_t *data = (map_elem_t*)malloc(sizeof(map_elem_t));
    data->key = (KeyType*)malloc(sizeof(KeyType));
    memcpy(data->key,key,sizeof(KeyType));
    data->val = (ValueType*)malloc((sizeof(ValueType)));
    memcpy(data->val,val,sizeof(ValueType));

    root_t root=map->root;
    rb_node_t **new_node = &(root->rb_node), *parent = NULL;
    while (*new_node) {
        map_elem_t *this_node = container_of(*new_node, map_elem_t, node);
        //int result = strcmp(key, this_node->key);
        parent = *new_node;
        if (key < this_node->key) {
            new_node = &((*new_node)->rb_left);
        }else if (key > this_node->key) {
            new_node = &((*new_node)->rb_right);
        }else {
            free(data);
            return -1;
        }
    }

    rb_link_node(&data->node, parent, new_node);
    rb_insert_color(&data->node, root);
    map->size++:

    return 0;
}

map_elem_t *map_first(/*root_t *tree*/) 
{
    rb_node_t *node = rb_first(tree);
    return (rb_entry(node, map_elem_t, node));
}

map_elem_t *map_next(rb_node_t *node) 
{
    rb_node_t *next =  rb_next(node);
    return rb_entry(next, map_elem_t, node);
}

void map_elem_free(map_elem_t *node)
{
    if (node != NULL) {
        if (node->key != NULL) {
            free(node->key);
            node->key = NULL;
            free(node->val);
            node->val = NULL;
    }
        free(node);
        node = NULL;
    }
}
//test map

map_t* map_init(map_t* map,int capacity){
    map->root=RB_ROOT;
    map->capacity=capacity;
    return map;
}
#endif

#if 0 
    root_t tree = RB_ROOT;

    int main() {
    char *key = "hello";
    char *word = "world";
    put(&tree, key, word);

    char *key1 = "hello 1";
    char *word1 = "world 1";
    put(&tree, key1, word1);


    char *key2 = "hello 1";
    char *word2 = "world 2 change";
    put(&tree, key2, word2);

    map_t *data1 = get(&tree, "hello 1");

    if (data1 != NULL)
        printf("%s\n", data1->val);

    map_t *node;
    for (node = map_first(&tree); node; node=map_next(&(node->node))) {
        printf("%s\n", node->key);
    }
 
    // free map if you don't need
    map_t *nodeFree = NULL;
    for (nodeFree = map_first(&tree); nodeFree; nodeFree = map_first(&tree)) {
        if (nodeFree) {
            rb_erase(&nodeFree->node, &tree);
            map_elem_free(nodeFree);
        }
    }
    return 0;
    }
#endif


/*
typedef struct Stack
{
	int top; 
	int capacity; 
    StackDateType* element; 
}Stack;

int StackInit(Stack* stack,int capacity)
{
	if(stack){
        stack=(struct Stack*)malloc(sizeof(struct Stack)+capacity*sizeof(StackDateType));
	    stack->element = NULL;
	    stack->capacity = capacity;
	    stack->top = 0;
        return 0;
    }
    else
        return -1;
}

int StackDestory(Stack* stack)
{
	if(stack){
        free(stack);
    }
    else
        return -1;
}

int StackPush(Stack* stack, StackDateType x)
{
	if(stack){
        if (stack->top == stack->capacity)
	    {
            return -1;
	    }
	    stack->element[stack->top] = x;
	    stack->top++;
        return 0;
    }
    else
        return -1;
}

StackDateType StackPop(Stack* stack)
{
	if(stack&&stack->top > 0){
        StackDateType a = stack->element[stack->top - 1];
	    stack->top--;
        return a;
    }
    else
        return NULL;
}

int StackIsEmpty(Stack* stack)
{
    return stack->top == 0;
}

StackDateType StackTop(Stack* stack)
{
    if(stack&&stack->top > 0){
	    return stack->element[stack->top - 1];
    }
    else
        return NULL;
}
*/
