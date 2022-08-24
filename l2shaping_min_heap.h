#ifndef _L2SHAPING_MINHEAP_H_
#define _L2SHAPING_MINHEAP_H_

#include <sys/time.h>
#include <time.h>
#include "l2shaping_policy.h"
#define timespeccmp(tvp, uvp, cmp)          \
     (((tvp)->tv_sec == (uvp)->tv_sec) ?     \
      ((tvp)->tv_nsec cmp (uvp)->tv_nsec) :  \
      ((tvp)->tv_sec cmp (uvp)->tv_sec))
#define NSECS_PER_SEC 1000000000
#define timespec_normalize(t) { if ((t) ->tv_nsec >= NSECS_PER_SEC) { (t) ->tv_nsec -= NSECS_PER_SEC; (t) ->tv_sec++; } else if ((t) ->tv_nsec < 0) { (t) ->tv_nsec += NSECS_PER_SEC; (t) ->tv_sec -- ; }}

#define timespec_add_ns(t,n) do { (t) ->tv_nsec += (n); timespec_normalize(t); } while (0)
typedef struct heap{
    uint64_t capacity; 
    uint64_t size;     
    struct ts_mbuf **data; 
}minHeap;

minHeap* MinHeapInit(uint64_t capacity)
{
    minHeap* h = (minHeap*)malloc(sizeof(minHeap));          
    if(h!=NULL)
    {
        h->capacity = capacity;                              
        h->size = 0;                                         
        h->data = (struct ts_mbuf *)malloc(sizeof(struct ts_mbuf)*(h->capacity+1)); 
        if(h->data!=NULL)
        {
            h->data[0] = NULL;                            // the data[0] don't store valid data 
        }
        else
        {
            free(h);
            return NULL;
        } 
    }
    return h;
}

// insert, empty element go on
bool MinHeapInsert(minHeap* heap, struct ts_mbuf* x)
{
    
    if(heap->size >= heap->capacity)         
        return false;
    else
    {
        uint64_t i = ++heap->size;// i == empty element
        while(heap->data[i/2]){
	        if(timespeccmp(&(heap->data[i/2]->ts), &x->ts,>)){
            	heap->data[i] = heap->data[i/2];
            	i /= 2;
             }
	        else
		        break;
	    }
        heap->data[i] = x;  
        return true;
    }
}

// delete, last element go down
struct ts_mbuf * MinHeapDelete(minHeap* heap)
{
    if(heap->size==0)                   
        return NULL;
    else
    {
        uint64_t i = 1;                     
        struct ts_mbuf * x = heap->data[heap->size--];
        uint64_t child;                  
        struct ts_mbuf *min = heap->data[1];       
        while(i*2<=heap->size)     
        {
            child = i*2;               // get empty's left child
            if(child!=heap->size && timespeccmp(&(heap->data[child]->ts), &(heap->data[child+1]->ts),>))
                child++;               // if right child < left child ，then get right child   
            if(timespeccmp(&(heap->data[child]->ts), &x->ts,<))  // last element godown 
            {
                heap->data[i] = heap->data[child];
                i = child;             
            }
            else                      
                break;
        }
        heap->data[i] = x;
        if(heap->size==0)
		    heap->data[1]=NULL;  
	    return min;
    }
}
#endif
