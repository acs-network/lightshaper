#ifndef _POLICY_H_
#define _POLICY_H_

#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>


#define BOOL int
#define TRUE 1
#define FALSE 0
volatile BOOL send_state;
volatile BOOL timing;

#define IPV4_ADDR(a, b, c, d)(((a & 0xff) << 24) | ((b & 0xff) << 16) | \
		((c & 0xff) << 8) | (d & 0xff))
 
//lcore control
#define RECEIVE_LCORE_NUM 4
#define START_LCORE 6

#define IS_RECEIVE_LCORE_FOR_CLIENT rte_lcore_id()==1    
#define IS_RECEIVE_LCORE_FOR_SERVER rte_lcore_id()==2 
#define IS_POLICY_LCORE     		rte_lcore_id()==3 
#define IS_SEND_LCORE_TO_SERVER  	rte_lcore_id()==4
#define IS_SEND_LCORE_TO_CLIENT  	rte_lcore_id()==5
#define IS_TRANS_TO_SERVER 			rte_lcore_id()==6
#define IS_TRANS_TO_CLIENT 			rte_lcore_id()==7
#define IS_PRINT_LCORE 				rte_lcore_id()==8

#define RATE_CONTROL 100

extern struct rte_mempool *produce_packs_pool;
#define MAX_VOID_PKT_LEN 1024
#define MAX_VOID_BURST_SIZE 1000
extern struct rte_mbuf* void_packs[MAX_VOID_PKT_LEN+1][MAX_VOID_BURST_SIZE];

//information of receive burst 
#define BURST_GAP_IN 15 
#define PACKET_GAP_IN 92.5
#define GROUP_SIZE_IN 4
#define BURST_SIZE_IN 40000
#define POLICY_TIME policy_init(BURST_GAP_IN,PACKET_GAP_IN,GROUP_SIZE_IN,BURST_SIZE_IN)

//signal of send_state
/*
向server端发的不带payload包降速
向client端发的带payload包降速
QUEUE_TO_XXX 带payload
QUEUE_TO_XXX +1  不带payload
*/

#define DUE_TIMER_SIG SIGRTMAX
#define PORT_TO_SERVER 1
#define QUEUE_TO_SERVER_WITH_PAYLOAD 0  //with payload
#define QUEUE_TO_SERVER_WITHOUT_PAYLOAD QUEUE_TO_SERVER_WITH_PAYLOAD+1

#define PORT_TO_CLIENT 0
#define QUEUE_TO_CLIENT_WITH_PAYLOAD 0 //without payload
#define QUEUE_TO_CLIENT_WITHOUT_PAYLOAD QUEUE_TO_CLIENT_WITH_PAYLOAD+1


//#define RING_THRESHOLD 120000

//send burst control 

/*
以220B大小包为例
10G速率时，包间隔为9.6ns
5G速率时，包间隔为225.6ns，SEND_PACKET_GAP应为216
2.5G速率时，包间隔为657.6ns，SEND_PACKET_GAP应为648
1G速率时，包间隔为657.6ns，SEND_PACKET_GAP应为648

220B
1737.6 == 1G
*/

#define SEND_PACKET_GAP 1000

//information of cpu,to control the send rate
#define CPU_MHZ 1600 //1600Mhz mean ,1 cycle cost 0.000 000 000 625s = 0.625ns
#define IP_DEFTTL 64


#define RING_SIZE 524288
extern struct rte_ring *rte_list_c2s;
extern struct rte_ring *rte_list_s2c;
extern struct rte_ring *rte_list_trans_c2s;
extern struct rte_ring *rte_list_trans_s2c;
extern struct rte_mempool *void_pack_pool;
extern struct rte_mbuf *template_void_pack_mbuf;
extern struct rte_mbuf *send_void_pack_mbufs[32];




/*
* @param burst_gap :in millisecond;
* @param packet_gap :in nanosecond;
* @param group_size :the size of the burst set to be integrated,here is 4;
* @param burst_size : the size of burst,in packet;
* @return send_time: the send time after receive the first packet;
*/

static double policy_init(double burst_gap,double packet_gap,double group_size,double burst_size){
	double burst_width = burst_size*packet_gap;                                      //20000*120.8=2416000
	double total_time = (burst_width*group_size)+burst_gap*(group_size -1)*10000000;//39 664 000
	double speed = 1/packet_gap;// ns / packet_gap                      			//8 278 145
	double total_size= burst_size*group_size;                                        //20000*4=80000
	double send_time=total_time-(total_size/speed);                                  //39664000-80000*120.8=30,000,000
	return send_time;
}




#endif




