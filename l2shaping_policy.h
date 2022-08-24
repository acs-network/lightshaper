#ifndef _POLICY_H_
#define _POLICY_H_

#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <stdio.h>

//#define DEBUG
//#define DIST_MODE //开启后速率实时变化
#define GAP_DIST_MODE 1 //0: linerate control,1 : pkt gap dist control

//model file control
#define DIST_FLAG 2 	//1 is shaping dist model,2 is gap dist model, 3 is delay dist model

#define NETEM_DIST_SCALE	8192
#define GAP_PREC 2000 	//unit: nanosecond
#define GAP_JITTER  20000  //unit: nanosecond
#define GAP_MEAN   100000  //unit: nanosecond
#define GAP_CORR 25
//#define GAP_ERROR_CORRECTION 0//unit: nanosecond
#define GAP_ERROR_CORRECTION 1200//unit: nanosecond

#define DELAY_MODE_OPEN 0  //0: close, 1 : open
#define DELAY_JITTER  0  //unit: nanosecond
#define DELAY_MEAN   50000000  //unit: nanosecond 
#define DELAY_PREC		1	   //unit: nanosecond
#define DELAY_IP_MASK 20   //32~20,determine the range of disttable

//#define DELAY_IP_MIN IPV4_ADDR(192, 168, 100, 1)
//#define DELAY_IP_MAX IPV4_ADDR(192, 168, 116,0 )
#define DELAY_IP_MIN IPV4_ADDR(192, 168, 131, 99)
#define DELAY_IP_MAX IPV4_ADDR(192, 168, 131, 101)

#define REORDER_MODE_OPEN 0 //0: close, 1 : open
//#define REORDER_IP_MIN IPV4_ADDR(192, 168, 100, 1)
//#define REORDER_IP_MAX IPV4_ADDR(192, 168, 116,0 )
#define REORDER_IP_MIN IPV4_ADDR(192, 168, 131, 99)
#define REORDER_IP_MAX IPV4_ADDR(192, 168, 131, 101)
#define REORDER_STACK_LEVEL 2
#define REORDER_STACK_TIMER 300000000
#define REORDER_RATIO 0.25
//#define TCP_CRR	
struct crndstate {
	uint32_t last;
	uint64_t rho;
} ;
struct crndstate * gap_corr;

struct disttable{
	uint32_t size;
	int64_t table[];
};

struct ts_mbuf{
    struct rte_mbuf *mbuf;
    struct timespec ts;
};

struct disttable *shaping_dist;
int16_t shaping_max,shaping_min; 	//the max and min of shaping_dist->table, Used to shrink the table's value to 0~10000
struct disttable *gap_dist;			//Probability distribution function
struct disttable * gap_dens_dist;	//Probability density function
struct disttable * gap_pool;

struct disttable *delay_dist;
struct disttable *delay_pool;

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
#define IS_DROP_LCORE 				rte_lcore_id()==9
#define IS_DELAY_LCORE 				rte_lcore_id()==10
#define IS_REORDER_LCORE 			rte_lcore_id()==11
#define IS_REFRAME_LCORE 			rte_lcore_id()==12



/*buffer time this value should between 0 and 999, e.g. 100 mean 100ms*/
#define BUFFER_TIME 100
#define BUFFER_PKT_SIZE 0

/*c2s send speed control ratio , e.g. 50 mean 10G *50%*/
#define RATE_CONTROL 30
#define DROP_RATIO 0

struct rte_mempool *produce_packs_pool;
#define MAX_VOID_PKT_LEN 2044
#define MAX_VOID_BURST_SIZE 1000
struct rte_mbuf* void_packs[MAX_VOID_PKT_LEN+1][MAX_VOID_BURST_SIZE];

//information of receive burst 
#define BURST_GAP_IN 15 
#define PACKET_GAP_IN 92.5
#define GROUP_SIZE_IN 4
#define BURST_SIZE_IN 40000
#define POLICY_TIME policy_init(BURST_GAP_IN,PACKET_GAP_IN,GROUP_SIZE_IN,BURST_SIZE_IN)

#define RING_SIZE 524288

/*process queues*/
struct rte_ring *c2s_reframe_queue;
struct rte_ring *c2s_reframe_queue0;
struct rte_ring *c2s_reframe_queue1;
struct rte_ring *c2s_reframe_queue2;
struct rte_ring *c2s_reframe_queue3;
struct rte_ring *c2s_send_queue;
struct rte_ring *c2s_send_queue_highpri;//put the pkt from delay_worker
struct rte_ring *c2s_receive_queue;
struct rte_ring *c2s_drop_process_queue;
struct rte_ring *c2s_delay_process_queue;
struct rte_ring *c2s_reorder_process_queue;
struct rte_ring *c2s_dump_process_queue;
struct rte_ring *s2c_send_queue;
struct rte_ring *s2c_receive_queue;

/*void pkt rate control*/
struct rte_mempool *void_pack_pool;
struct rte_mbuf *template_void_pack_mbuf;
struct rte_mbuf *send_void_pack_mbufs[32];

//signal of send_state
/*
向server端发的不带payload包降速
向client端发的带payload包降速
QUEUE_TO_XXX 带payload
QUEUE_TO_XXX +1  不带payload
*/

#define DUE_TIMER_SIG SIGRTMAX
#define PORT_TO_SERVER 0
#define QUEUE_TO_SERVER_WITH_PAYLOAD 0  //with payload
#define QUEUE_TO_SERVER_WITHOUT_PAYLOAD QUEUE_TO_SERVER_WITH_PAYLOAD+1

#define PORT_TO_CLIENT 1
#define QUEUE_TO_CLIENT_WITH_PAYLOAD 0 //without payload
#define QUEUE_TO_CLIENT_WITHOUT_PAYLOAD QUEUE_TO_CLIENT_WITH_PAYLOAD+1


//#define RING_THRESHOLD 120000

//send burst control 

/*
* example:220Byte pkt
* 5G，pkt gap is 225.6ns，SEND_PACKET_GAP is 216
* 2.5G，pkt gap is 657.6ns，SEND_PACKET_GAP is 648
*/
#define SEND_PACKET_GAP 1000

//information of cpu,to control the send rate
#define CPU_MHZ 1600 //1600Mhz mean ,1 cycle cost 0.000 000 000 625s = 0.625ns
#define IP_DEFTTL 64

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




