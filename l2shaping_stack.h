#include <stdio.h>
#include "l2shaping_policy.h"

typedef struct stack{
    int capacity;
    int top;
    struct timespec *oldest;
    struct ts_mbuf *data[REORDER_STACK_LEVEL];
}ts_mbuf_stack;
/*
ts_mbuf_stack* new_ts_mbuf_stack(int capacity){
    ts_mbuf_stack *stack=(ts_mbuf_stack*)malloc(sizeof(ts_mbuf_stack)+capacity*sizeof(struct ts_mbuf *));
    stack->capacity=capacity;
    stack->top=0;
    stack->oldest=NULL;
    return stack;
}
*/
void ts_mbuf_stack_init(ts_mbuf_stack* stack,int capacity){
    stack->capacity=capacity;
    stack->top=0;
    stack->oldest=NULL;
}

int ts_mbuf_stack_size(ts_mbuf_stack* stack){
    if(stack)
        return stack->top;
    else    
        return 0;
}

int ts_mbuf_stack_capacity(ts_mbuf_stack* stack,int capacity){
    stack->capacity=capacity;
    return 0;
}

int ts_mbuf_stack_push(ts_mbuf_stack* stack,struct ts_mbuf * elem){
    stack->data[++stack->top]=elem;
    if(stack->top==1){
        stack->oldest=&(elem->ts);
    }
    if(elem->mbuf==NULL){
        fprintf(stderr,"%s %d push fail!",__func__,__LINE__);
		exit(-1);
    }
    return stack->top;
}

struct ts_mbuf * ts_mbuf_stack_pop(ts_mbuf_stack* stack){
    if (stack->top<=0) {
        return -1;
    }
    int tmp=stack->top;
    stack->top--;
    if(stack->top==0){
        stack->oldest=NULL;
    }
    if(stack->data[tmp]->mbuf==NULL){
        fprintf(stderr,"%s %d pop fail!",__func__,__LINE__);
		exit(-1);
    }
    return stack->data[tmp];
}
