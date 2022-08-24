
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>

#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>
#include <rte_lpm6.h>

#include "l2shaping.h"
#include <rte_ring.h>

#include "l2shaping_policy.h"
#include "l2shaping_list.h"
#include "l2shaping_min_heap.h"
#include "l2shaping_reorder_stream_table.h"
struct ipv4_l2shaping_lpm_route {
	uint32_t ip;
	uint8_t  depth;
	uint8_t  if_out;
};

struct ipv6_l2shaping_lpm_route {
	uint8_t  ip[16];
	uint8_t  depth;
	uint8_t  if_out;
};

/* 198.18.0.0/16 are set aside for RFC2544 benchmarking (RFC5735). */
static struct ipv4_l2shaping_lpm_route ipv4_l2shaping_lpm_route_array[] = {
	{RTE_IPV4(192, 168, 133, 0), 24, 0},
	{RTE_IPV4(192, 168, 140, 0), 24, 1},
	{RTE_IPV4(192, 168, 137, 0), 24, 1},
	{RTE_IPV4(192, 168, 136, 0), 24, 1},
	{RTE_IPV4(192, 168, 139, 0), 24, 1},
};

/* 2001:0200::/48 is IANA reserved range for IPv6 benchmarking (RFC5180) */
static struct ipv6_l2shaping_lpm_route ipv6_l2shaping_lpm_route_array[] = {
	{{32, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 32, 1},
};


struct time_val
{
    int tv_sec;         
    int tv_usec;        
};

#define IPV4_l2shaping_LPM_NUM_ROUTES \
	(sizeof(ipv4_l2shaping_lpm_route_array) / sizeof(ipv4_l2shaping_lpm_route_array[0]))
#define IPV6_l2shaping_LPM_NUM_ROUTES \
	(sizeof(ipv6_l2shaping_lpm_route_array) / sizeof(ipv6_l2shaping_lpm_route_array[0]))

#define IPV4_l2shaping_LPM_MAX_RULES         1024
#define IPV4_l2shaping_LPM_NUMBER_TBL8S (1 << 8)
#define IPV6_l2shaping_LPM_MAX_RULES         1024
#define IPV6_l2shaping_LPM_NUMBER_TBL8S (1 << 16)

struct rte_lpm *ipv4_l2shaping_lpm_lookup_struct[NB_SOCKETS];
struct rte_lpm6 *ipv6_l2shaping_lpm_lookup_struct[NB_SOCKETS];

static inline uint16_t
lpm_get_ipv4_dst_port(void *ipv4_hdr, uint16_t portid, void *lookup_struct)
{
	uint32_t next_hop;
	struct rte_lpm *ipv4_l2shaping_lookup_struct =
		(struct rte_lpm *)lookup_struct;

	return (uint16_t) ((rte_lpm_lookup(ipv4_l2shaping_lookup_struct,
		rte_be_to_cpu_32(((struct rte_ipv4_hdr *)ipv4_hdr)->dst_addr),
		&next_hop) == 0) ? next_hop : portid);
}

static inline uint16_t
lpm_get_ipv6_dst_port(void *ipv6_hdr, uint16_t portid, void *lookup_struct)
{
	uint32_t next_hop;
	struct rte_lpm6 *ipv6_l2shaping_lookup_struct =
		(struct rte_lpm6 *)lookup_struct;

	return (uint16_t) ((rte_lpm6_lookup(ipv6_l2shaping_lookup_struct,
			((struct rte_ipv6_hdr *)ipv6_hdr)->dst_addr,
			&next_hop) == 0) ?  next_hop : portid);
}


/*edit*/
#include "l2shaping_lpm.h"

volatile double current_rate; 

uint64_t packet_received_from_client;
uint64_t packet_received_from_server;
uint64_t packet_sent_to_server_with_payload;
uint64_t packet_sent_to_server_with_payload_from_client;
uint64_t packet_sent_to_server_high_pri;
uint64_t packet_sent_to_server_low_pri;
uint64_t packet_sent_to_client_with_payload;
uint64_t packet_sent_to_server_without_payload;
uint64_t packet_sent_to_client_without_payload;

/* forward declarations */
void get_void_pkts(uint32_t burst_size,uint32_t packet_size);
uint8_t delay_level(struct rte_mbuf *m);
int reorder_check(struct rte_mbuf *m);
int delay_check(struct rte_mbuf *m);

int max(int a,int b);
int min(int a,int b);
/*if belong_to_one_burst,difference less than 700ms,return 0 ,else return -1*/
int belong_to_one_burst(struct timeval  *last,struct timeval  *current)
{
    int sec=current->tv_sec-last->tv_sec;
    int usec=current->tv_usec-last->tv_usec;

    if(sec==0 && (usec/1000)<=700){
        return 0;
    }
    else if(sec==1 && (usec/1000)<=-300){
        return 0;
    }
    else{
        return -1;
    }
}

int width_compute(struct timeval *last,struct timeval  *current)
{
	int width;
    int sec=current->tv_sec-last->tv_sec;
    int usec=current->tv_usec-last->tv_usec;   
	//fprintf(file, "compute-----last sec is  %d , usec is %d ,current sec is  %d , usec is %d ,\n", last->tv_sec,last->tv_usec,current->tv_sec,current->tv_usec);
	//fprintf(file,"sec is %d,use is %d \n",sec,usec);        
    width=sec*1000+usec/1000; 
	return width;
}

static void
due_timer_handler(int sig)
{
	if (sig == DUE_TIMER_SIG) {
		//fprintf(stderr,"buffer end , now the send queue count is %d,high pri snd queue is %d\n",rte_ring_count(c2s_send_queue),rte_ring_count(c2s_send_queue_highpri));
		send_state = TRUE;
		timing=FALSE;
	}
	
}

static void
print_stats(void)
{
	const char clr[] = { 27, '[', '2', 'J', '\0' };
	const char topLeft[] = { 27, '[', '1', ';', '1', 'H','\0' };

	/* Clear screen and move to top left */
	printf("%s%s", clr, topLeft);
	printf("\n");
	printf("table update when receive packet with payload from client\n");
	printf("====client  to server ====\n");
	printf("packet_in from client total: %llu\n",packet_received_from_client);
	printf("packet_out to server total: %llu\n",packet_sent_to_server_without_payload+packet_sent_to_server_with_payload);
	printf("packet_out to server without payload: %llu\n",packet_sent_to_server_without_payload);
	printf("packet_out to server with payload: %llu\n",packet_sent_to_server_with_payload);
	printf("packet_out to server high pri: %llu\n",packet_sent_to_server_high_pri);
	printf("packet_out to server low pri : %llu\n",packet_sent_to_server_low_pri);
	printf("\n\n\n");
	printf("====server  to client ====\n");
	printf("packet_in from server total: %llu\n",packet_received_from_server);
	printf("packet_out to client total: %llu\n",packet_sent_to_client_without_payload+packet_sent_to_client_with_payload);
	printf("packet_out to client without payload: %llu\n",packet_sent_to_client_without_payload);
	printf("packet_out to client with payload: %llu\n",packet_sent_to_client_with_payload);
	printf("============================\n");

}

static void 
pri_check(struct rte_tcp_hdr * tcp_hdr)
{
	char * payload = (char *)((uint8_t *)tcp_hdr + tcp_hdr->data_off);
		if(unlikely(payload[5]==1)){
			packet_sent_to_server_high_pri++;
			packet_sent_to_server_with_payload_from_client++;
		}
		else{
			packet_sent_to_server_low_pri++;
			packet_sent_to_server_with_payload_from_client++;
		}
}

/* init_crandom - initialize correlated random number generator
 * Use entropy source for initial seed.
 */
static void
init_crandom(struct crndstate *state, uint32_t rho)
{
	state=(struct crndstate *)malloc(sizeof(struct crndstate));
	if(state==NULL){
		fprintf(stderr,"crndstate malloc fail!\n");
		exit(-1);
	}
	state->rho = (((uint64_t)rho) << 32 ) / 100;
	state->last = (uint32_t) rand();
}

/* get_crandom - correlated random number generator
 * Next number depends on last value.
 * rho is scaled to avoid floating point.
 */
static uint32_t 
get_crandom(struct crndstate *state)//whk
{
	uint64_t value, rho;
	uint32_t answer;

	//value = (((uint64_t) rand() <<  0) & 0x00000000FFFFFFFFull) | (((uint64_t) rand() << 32) & 0xFFFFFFFF00000000ull);
	value=(uint32_t) rand();
	if (!state || state->rho == 0)	/* no correlation */
		return value;

	//rho = state->rho + 1;

	answer = (value * ((1ull<<32) - rho) + state->last * rho) >> 32;
	state->last = answer;

	return answer;
}

static int
get_dist_rand(int mu, int sigma,//whk
		     struct crndstate *state,
		     const struct disttable *dist)
{
    //int sigma = GAP_JITTER/4;//jitter == 4 sigma
    //int mu = GAP_MEAN;
    int i = 0;
    int t,x;

	if (sigma == 0)
		return mu;

	uint32_t rnd = get_crandom(state);
	/* default uniform distribution */
	if (dist == NULL){
		return ((rnd% (2 * 4 * sigma/*GAP_JITTER*/)) + mu - 4 * sigma);
	}
		

    t = dist->table[(rnd%dist->size)];
    x = (sigma % NETEM_DIST_SCALE) * t;

	if (x >= 0)
		x += NETEM_DIST_SCALE/2;
	else
		x -= NETEM_DIST_SCALE/2;
    
	int value = x / NETEM_DIST_SCALE + (sigma / NETEM_DIST_SCALE) * t + mu;

	return value;
}

/* main processing loop */
int lpm_main_loop(__attribute__((unused)) void *dummy)
{
	send_state=FALSE;
	if(IS_POLICY_LCORE)
		c2s_policy_main_loop();
    if(IS_RECEIVE_LCORE_FOR_CLIENT)
        c2s_receive_main_loop();
	if (IS_SEND_LCORE_TO_SERVER)
        c2s_rate_control_send_main_loop_compare();
		//c2s_rate_control_send_main_loop();
		
    if (IS_RECEIVE_LCORE_FOR_SERVER)
        s2c_receive_main_loop();
	if (IS_SEND_LCORE_TO_CLIENT)
        s2c_send_main_loop();

	if (IS_TRANS_TO_SERVER)
		c2s_filter_main_loop();
	if (IS_TRANS_TO_CLIENT)
		s2c_filter_main_loop();
	if (IS_PRINT_LCORE)
		print_main_loop();
	if (IS_DROP_LCORE)
		c2s_drop_main_loop();
	if(IS_DELAY_LCORE)
		c2s_delay_main_loop();
	if(IS_REORDER_LCORE)
		c2s_reorder_main_loop();
    return 0;
}

#define MAX_TIMER_PERIOD 86400 
#define US_PER_S 1000000
#define BURST_TX_DRAIN_US 100

/* printer */
int print_main_loop(){
	unsigned lcore_id;
	uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;
	uint64_t timer_period = 1 * rte_get_timer_hz();//1 second
	const uint64_t drain_tsc = ((rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S) * BURST_TX_DRAIN_US;
	prev_tsc = 0;
	timer_tsc = 0;
	fprintf(stderr,"lcore %d——printer\n",lcore_id);
	sleep(3);
	while (!force_quit) {
		cur_tsc = rte_rdtsc();
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {
			/* if timer is enabled */
			if (timer_period > 0) {
				/* advance the timer */
				timer_tsc += diff_tsc;
				/* if timer has reached its timeout */
				if (unlikely(timer_tsc >= timer_period)) {
					print_stats();
					/* reset the timer */
					timer_tsc = 0;	
				}
			}
			prev_tsc = cur_tsc;
		}
	}
	fprintf(stderr,"lcore %d——printer:finished\n",lcore_id);
}

/*C2S mean Client to Server direction */
/* C2S receiver */
int c2s_receive_main_loop(){
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	unsigned lcore_id;
	int i, nb_rx,enq_num;
	uint16_t portid;
	uint8_t queueid;
	struct lcore_conf *qconf;
	int test;

	lcore_id = rte_lcore_id();
	packet_received_from_client=0;

	qconf = &lcore_conf[lcore_id];
	fprintf(stderr,"lcore %d——c2s_receiver\n",lcore_id);

	if (qconf->n_rx_queue == 0) {
		RTE_LOG(INFO, l2shaping, "lcore %u has nothing to do\n", lcore_id);
		return 0;
	}

	RTE_LOG(INFO, l2shaping, "entering main loop on lcore %u\n", lcore_id);
	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		RTE_LOG(INFO, l2shaping,
			" -- lcoreid=%u portid=%u rxqueueid=%hhu\n",
			lcore_id, portid, queueid);
	}
	while (!force_quit) {
		/*
		 * Read packet from RX queues
		 */
		for (i = 0; i < qconf->n_rx_queue; ++i) {
			portid = qconf->rx_queue_list[i].port_id;
			queueid = qconf->rx_queue_list[i].queue_id;
			nb_rx = rte_eth_rx_burst(PORT_TO_CLIENT, queueid, pkts_burst,
				MAX_PKT_BURST);		
			if (nb_rx == 0)
				continue;
		#if 0
			struct rte_ipv4_hdr *ip_hdr;
     		struct rte_ether_hdr *eth_hdr;
			struct rte_tcp_hdr *tcp_hdr;
     		int tmpj=0;
     		uint32_t dst_ip,src_ip;
     		struct rte_ether_addr dst_mac,src_mac;
     		for(;tmpj<nb_rx;tmpj++){
        		struct rte_mbuf *m = pkts_burst[tmpj];
        		eth_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,0);
        		ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_ether_hdr));
        		char buf[RTE_ETHER_ADDR_FMT_SIZE];
        		dst_mac= eth_hdr->d_addr;
        		src_mac= eth_hdr->s_addr;
        		rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &dst_mac);
	    		printf("dst mac is %s,",buf);
        		rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &src_mac);
	    		printf("src mac is %s,", buf);        
        		dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
        		src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
        		printf("dst ip  %d.%d.%d.%d ,src ip  %d.%d.%d.%d from port %d\n",
        		dst_ip >> 24 & 0xff,dst_ip>>16 & 0xff,dst_ip>>8 & 0xff,dst_ip& 0xff,     
        		src_ip >> 24 & 0xff,src_ip>>16 & 0xff,src_ip>>8 & 0xff,src_ip& 0xff, 
        		portid);
     		}
		#endif

            /*put packet in ring*/
            enq_num=rte_ring_sp_enqueue_bulk(c2s_receive_queue, pkts_burst,nb_rx,NULL);
			if(enq_num==0)	
				fprintf(stderr,"!!!!!!!!![%s] enq c2s_receive_queue fail,enq_num is %d,nb_rx is %d,[%d]!!!!!!!\n",__func__, enq_num,nb_rx, __LINE__);
			packet_received_from_client+=enq_num;
		}
	}
	fprintf(stderr,"lcore %d——c2s_receiver:packet_received_from_client num is %llu\n",lcore_id,packet_received_from_client);
	return 0;
}

/* C2S filter */
int c2s_filter_main_loop()
{
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int nb_trans,i,i2,n,enq_num,deq_num,available=0;
	int status;
	int filter_to_sendqueue_num=0,filter_to_dumpqueue_num=0,filter_to_dropqueue_num=0,filter_to_reorderqueue_num=0,filter_to_delayqueue_num=0,filter_to_reframequeue_num=0;
	unsigned lcore_id;
    int count,tmpn;
	struct rte_ipv4_hdr *ip_hdr;
    struct rte_ether_hdr *eth_hdr;
	struct rte_tcp_hdr *tcp_hdr;
	struct rte_vlan_hdr *vhdr;
	uint16_t ether_type;
	char * payload;
	packet_sent_to_server_without_payload=0;
	packet_sent_to_server_high_pri=0;
	packet_sent_to_server_low_pri=0;
	packet_sent_to_server_with_payload_from_client=0;

	lcore_id = rte_lcore_id();
	fprintf(stderr,"lcore %d——c2s_filter\n",lcore_id);
    count = 0;
	srand((unsigned)time(NULL));
    while (!force_quit) {
		if(likely(count !=0)) {
			nb_trans=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_trans;
			deq_num=rte_ring_sc_dequeue_bulk(c2s_receive_queue,pkts_burst,nb_trans,&available);
			if(deq_num==0) {
				deq_num=rte_ring_sc_dequeue_bulk(c2s_receive_queue,pkts_burst,available,NULL);
			}
			if(deq_num==0) continue;

			for(i=0;i<deq_num;i++){
				/*filter loop*/
				if(rand()%100<DROP_RATIO){
					enq_num=rte_ring_mp_enqueue(c2s_drop_process_queue,pkts_burst[i]);
					if(unlikely(enq_num!=0))	{
						rte_exit(EXIT_FAILURE, "c2s_filter_main_loop lcore enq c2s_drop_process_queue fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans);
					}
					filter_to_dropqueue_num+=1;
					continue;
				}
				if(REORDER_MODE_OPEN){
					status=reorder_check(pkts_burst[i]);
					if(status){
						enq_num=rte_ring_mp_enqueue(c2s_reorder_process_queue,pkts_burst[i]);
						if(unlikely(enq_num!=0))	{
							rte_exit(EXIT_FAILURE, "c2s_filter_main_loop lcore enq c2s_reorder_queue fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans);
						}
						filter_to_reorderqueue_num+=1;
						continue;
					}
				}
				if(DELAY_MODE_OPEN){
					status=delay_check(pkts_burst[i]);
					if(status){
						enq_num=rte_ring_mp_enqueue(c2s_delay_process_queue,pkts_burst[i]);
						if(unlikely(enq_num!=0))	{
							rte_exit(EXIT_FAILURE, "c2s_filter_main_loop lcore enq c2s_delay_queue fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans);
						}
						filter_to_delayqueue_num+=1;
						continue;
					}
					
				}
				if(pkts_burst[i]->pkt_len>=BUFFER_PKT_SIZE){

					enq_num=rte_ring_mp_enqueue(c2s_send_queue,pkts_burst[i]);
					if(unlikely(enq_num!=0))	{
						fprintf(stderr,"%s %d enq fail,we want 1 ,actually %d,ring count is %d\n",__func__,__LINE__,enq_num,rte_ring_count(c2s_send_queue));
						exit(-1);
					}
					filter_to_sendqueue_num+=1;
					
				}
				else{
					n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_SERVER_WITHOUT_PAYLOAD, pkts_burst[i], 1);
					while(n<1){ 
						tmpn= rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_SERVER_WITHOUT_PAYLOAD, pkts_burst[i], 1);
						n+=tmpn;
					}
					packet_sent_to_server_without_payload+=n;
				}
				
			}
        }
		else{
			count =  rte_ring_count(c2s_receive_queue);
		}
    }
	fprintf(stderr,"lcore %d——c2s_filter:to_sendqueue_num is %d,to_dumpqueue_num is %d,to_dropqueue_num is %d,to the delay queue is %d,to the reframe queue is %d,now the receive ringcount is %d\n"
				,lcore_id,filter_to_sendqueue_num,filter_to_dumpqueue_num,filter_to_dropqueue_num,filter_to_delayqueue_num,filter_to_reframequeue_num,rte_ring_count(c2s_receive_queue));
}

/* C2S drop worker */
int c2s_drop_main_loop(){
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int nb_tx,i,n,enq_num,deq_num,available=0,drop_count=0;
	unsigned lcore_id;
    unsigned int count;

	lcore_id = rte_lcore_id();
	fprintf(stderr,"lcore %d——c2s_dropper\n",lcore_id);
    count = 0;

    while (!force_quit) {
		if(likely(count !=0)) {
			nb_tx=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_tx;
			deq_num=rte_ring_sc_dequeue_bulk(c2s_drop_process_queue,pkts_burst,nb_tx,&available);
			if(deq_num==0) {
				deq_num=rte_ring_sc_dequeue_bulk(c2s_drop_process_queue,pkts_burst,available,NULL);
			}
			if(deq_num==0) continue;

			/*process section*/
			for(i=0;i<deq_num;i++){
				rte_pktmbuf_free(pkts_burst[i]);
				drop_count++;
			}
    	}
		if(unlikely(count==0))
			count =  rte_ring_count(c2s_drop_process_queue);
    }
	fprintf(stderr,"lcore %d——c2s_dropper:drop_count is %d ,now the c2s_drop_process_queue ringcount is %d\n"
			,lcore_id,drop_count,rte_ring_count(c2s_drop_process_queue));
}

/*1 mean reorder,0 mean just send*/
int delay_check(struct rte_mbuf *m)
{
	struct rte_ether_hdr *eth_hdr;
	struct rte_vlan_hdr *vhdr;
	struct rte_ipv4_hdr *ip_hdr;
	struct rte_tcp_hdr *tcp_hdr;
	uint32_t dst_ip,src_ip;
	struct ts_mbuf *ts_m_del,*ts_m_ins;
	struct timespec now;
	eth_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,0);
	if(eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN)){
		vhdr=rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,sizeof(struct rte_ether_hdr));
		if(vhdr->eth_proto = RTE_BE16(RTE_ETHER_TYPE_IPV4))
			ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_vlan_hdr)+sizeof(struct rte_ether_hdr));
		else{
			return 0;
		}
	}
	else if (eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
        ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_ether_hdr));
	else{
		return 0;
	}
	//dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
    src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
	if(src_ip>=DELAY_IP_MIN&&src_ip<=DELAY_IP_MAX){
		return 1;
	}
	return 0;
}
/* C2S delay worker*/
int c2s_delay_main_loop(){
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST],*m;
	int nb_rcv,i,n,tmpn,delay_num,enq_num,deq_num,delay_count=0;
	uint32_t just_send_num=0;
	unsigned lcore_id;
    unsigned int count;
	struct list_node *node;
	lcore_id = rte_lcore_id();
	//uint8_t delay_time=15;/*default delay time:ms*/
	uint64_t delay_add_time;
    count = 0;
	minHeap* delay_heap;
	struct rte_ether_hdr *eth_hdr;
	struct rte_vlan_hdr *vhdr;
	struct rte_ipv4_hdr *ip_hdr;
	struct rte_tcp_hdr *tcp_hdr;
	uint32_t dst_ip,src_ip;
	struct ts_mbuf *ts_m_del,*ts_m_ins;
	struct timespec now;
	/*init min heap*/
	delay_heap=MinHeapInit(131072);
	if(delay_heap==NULL){
		fprintf(stderr,"\n\nlcore %d in c2s_delay_main_loop fail!!!!\n\n",lcore_id);
		exit(-1);
	}

	fprintf(stderr,"lcore %d——c2s_delayer\n",lcore_id);

	int i1=0,i2=0;
	while(!force_quit){
		if(delay_heap->size>0&&delay_heap->data[1]){
			clock_gettime(CLOCK_MONOTONIC,&now);
			if(timespeccmp(&(delay_heap->data[1]->ts),&now, < )){
				ts_m_del=MinHeapDelete(delay_heap);
				if(ts_m_del==NULL){
					fprintf(stderr,"%s %d, delete fail!\n",__func__,__LINE__);
					exit(-1);		
				}
				enq_num=rte_ring_mp_enqueue(c2s_send_queue_highpri,ts_m_del->mbuf);
				free(ts_m_del);
				if(enq_num==0)
					delay_count+=1;
			}
		}
		if(likely(count !=0)) {
			//nb_rcv=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			nb_rcv=1;
			count-=nb_rcv;
			deq_num=rte_ring_mc_dequeue_bulk(c2s_delay_process_queue, pkts_burst,nb_rcv,NULL);
			if (unlikely(deq_num==0)){//cause deq_num is 0 either nb_rcv,if deq_num==0,then continue
				count=0;
				continue;
			} 
			for(i=0;i<deq_num;i++){
				m = pkts_burst[i];
        			eth_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,0);
				if(eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN)){
					vhdr=rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,sizeof(struct rte_ether_hdr));
					if(vhdr->eth_proto = RTE_BE16(RTE_ETHER_TYPE_IPV4))
						ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_vlan_hdr)+sizeof(struct rte_ether_hdr));
					else{
						enq_num=rte_ring_mp_enqueue(c2s_send_queue_highpri,m);
						if(enq_num==0)
							just_send_num+=1;				
							continue;
					}
		
				}
				else if (eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
        				ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_ether_hdr));
				else{
					enq_num=rte_ring_mp_enqueue(c2s_send_queue_highpri,m);
					if(enq_num==0)
						just_send_num+=1;
					continue;
				}
				//dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
        		src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
				ts_m_ins=(struct ts_mbuf *)malloc(sizeof(struct ts_mbuf));
				
				clock_gettime(CLOCK_MONOTONIC,&(ts_m_ins->ts));
				//fprintf(stderr,"insert %d ,now_sec is %lld, now_nec is %lld \n",i1,ts_m_ins->ts.tv_sec,ts_m_ins->ts.tv_nsec);
				timespec_add_ns(&(ts_m_ins->ts),delay_pool->table[src_ip%(1<<(32-DELAY_IP_MASK))]);
				//fprintf(stderr,"insert %d ,delay_sec is %lld, delay_nec is %lld\n",i1++,ts_m_ins->ts.tv_sec,ts_m_ins->ts.tv_nsec);
				delay_dist->table[src_ip%(1<<(32-DELAY_IP_MASK))]++;
				/*
				int tmp_706=get_dist_rand(DELAY_MEAN,DELAY_JITTER/4,NULL,NULL);
				timespec_add_ns(&(ts_m_ins->ts),tmp_706);
				*/
				
				//timespec_add_ns(&(ts_m_ins->ts),DELAY_MEAN);
				ts_m_ins->mbuf=m;
				if(!MinHeapInsert(delay_heap, ts_m_ins)){
					fprintf(stderr,"%s %d, insert fail!\n",__func__,__LINE__);
					exit(-1);	
				}
			}

		#if 0
     		struct rte_ether_hdr *eth_hdr;
			struct rte_tcp_hdr *tcp_hdr;
     		int tmpj=0;
     		uint32_t dst_ip,src_ip;
     		struct rte_ether_addr dst_mac,src_mac;
     		for(;tmpj<nb_rx;tmpj++){
        		struct rte_mbuf *m = pkts_burst[tmpj];
        		eth_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,0);
        		ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_ether_hdr));
        		char buf[RTE_ETHER_ADDR_FMT_SIZE];
        		dst_mac= eth_hdr->d_addr;
        		src_mac= eth_hdr->s_addr;
        		rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &dst_mac);
	    		printf("dst mac is %s,",buf);
        		rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &src_mac);
	    		printf("src mac is %s,", buf);        
        		dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
        		src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
        		printf("dst ip  %d.%d.%d.%d ,src ip  %d.%d.%d.%d from port %d\n",
        		dst_ip >> 24 & 0xff,dst_ip>>16 & 0xff,dst_ip>>8 & 0xff,dst_ip& 0xff,     
        		src_ip >> 24 & 0xff,src_ip>>16 & 0xff,src_ip>>8 & 0xff,src_ip& 0xff, 
        		portid);
     		}
		#endif
        }
		if(unlikely(count==0)){
			count =  rte_ring_count(c2s_delay_process_queue);
		}
	}
	/*
	if(DIST_FLAG==3){
		for(i=0;i<delay_dist->size;i++){
            fprintf(stderr,"%s,dist->%d is %lld\n",__func__,i,delay_dist->table[i]);
        }
	}
	*/
	fprintf(stderr,"lcore %d——c2s_delayer:delay_count is %d ,now the c2s_delay_process_queue ringcount is %d,delay_heap size is %d\n"
			,lcore_id,delay_count,rte_ring_count(c2s_delay_process_queue),delay_heap->size);
}

void inspect_stream_table(){
	int i;
	struct timespec now;
	struct ts_mbuf *ts_tmp;
	for(i=0;i<reorder_table->size;i++){
		clock_gettime(CLOCK_MONOTONIC,&now);
		if(reorder_table->stacks[i]!=NULL){
			if(reorder_table->stacks[i]->oldest!=NULL){
				if(timespeccmp(reorder_table->stacks[i]->oldest, &now, > )){
					while(ts_mbuf_stack_size(reorder_table->stacks[i])>0){
						ts_tmp=ts_mbuf_stack_pop(reorder_table->stacks[i]);
						rte_ring_mp_enqueue(c2s_send_queue_highpri,ts_tmp->mbuf);
						//free(ts_tmp->ts);
						free(ts_tmp);
					}
				}
			}
		}

	}
}
/*1 mean reorder,0 mean just send*/
int reorder_check(struct rte_mbuf *m)
{
	struct rte_ether_hdr *eth_hdr;
	struct rte_vlan_hdr *vhdr;
	struct rte_ipv4_hdr *ip_hdr;
	struct rte_tcp_hdr *tcp_hdr;
	uint32_t dst_ip,src_ip;
	struct ts_mbuf *ts_m_del,*ts_m_ins;
	struct timespec now;
	eth_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,0);
	if(eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN)){
		vhdr=rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,sizeof(struct rte_ether_hdr));
		if(vhdr->eth_proto = RTE_BE16(RTE_ETHER_TYPE_IPV4))
			ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_vlan_hdr)+sizeof(struct rte_ether_hdr));
		else{
			return 0;
		}
	}
	else if (eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
        ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_ether_hdr));
	else{
		return 0;
	}
	//dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
    src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
	if(src_ip>=REORDER_IP_MIN&&src_ip<=REORDER_IP_MAX){
		return 1;
	}
	return 0;
}
int c2s_reorder_main_loop(){
	
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST],*m,*tmp;
	struct ts_mbuf *ts_m,*ts_tmp;
	int nb_rcv,i,n,deq_num,status,enq_num,it;
	uint32_t just_send_num=0,reorder_num=0,put_delay_num=0;
	unsigned lcore_id;
    unsigned int count;
	lcore_id = rte_lcore_id();

    count = 0;
	struct rte_ether_hdr *eth_hdr;
	struct rte_vlan_hdr *vhdr;
	struct rte_ipv4_hdr *ip_hdr;
	struct rte_tcp_hdr *tcp_hdr;
	uint32_t dst_ip,src_ip;
	uint16_t dst_port,src_port;
	struct ts_mbuf *ts_m_del,*ts_m_ins;
	struct timespec now;
	//map_elem_t *last;

	uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;
	uint64_t timer_period = 0.3 * rte_get_timer_hz();//0.3 second
	const uint64_t drain_tsc = ((rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S) * BURST_TX_DRAIN_US;
	prev_tsc = 0;
	timer_tsc = 0;

	/*stream table init*/
	/*
	reorder_table->size=HASH_STREAM_TABLE_SIZE;
	reorder_table=(reorder_table_t *)malloc(sizeof(reorder_table_t)+reorder_table->size*sizeof(map_t));
	for(i=0;i<reorder_table->size;i++){
		reorder_table->bucket[i]=map_init(reorder_table->bucket[i],BUCKET_CAPACITY);
	}
	*/
	//reorder_table=(reorder_table_t *)malloc(sizeof(reorder_table_t)+(REORDER_IP_MAX-REORDER_IP_MIN+1)*sizeof(ts_mbuf_stack*));
	reorder_table=(reorder_table_t *)malloc(sizeof(reorder_table_t));
	if(reorder_table==NULL) {
		fprintf(stderr,"%s %d malloc fail!",__func__,__LINE__);
		exit(-1);
	}

	#ifdef TCP_CRR
	uint32_t reorder_counter[65535];
	uint32_t all_counter[65535];
	float reorder_ratio[65535];
	reorder_table->stacks=(ts_mbuf_stack **)malloc((65535)*sizeof(ts_mbuf_stack*));

	if(reorder_table->stacks==NULL){
		fprintf(stderr,"%s %d malloc fail!",__func__,__LINE__);
                exit(-1);
	}
	reorder_table->size=65535;
	for(i=0;i<65535;i++){
		reorder_table->stacks[i]=(ts_mbuf_stack*)malloc(sizeof(ts_mbuf_stack));
		if(reorder_table->stacks[i]==NULL){
			fprintf(stderr,"%s %d malloc fail!",__func__,__LINE__);
                	exit(-1);
		}
		ts_mbuf_stack_init(reorder_table->stacks[i],REORDER_STACK_LEVEL);
		reorder_counter[i]=0;
		all_counter[i]=0;
		reorder_ratio[i]=0;
	}
	#else
	uint16_t reorder_counter[REORDER_IP_MAX-REORDER_IP_MIN+1];
	uint16_t all_counter[REORDER_IP_MAX-REORDER_IP_MIN+1];
	float reorder_ratio[REORDER_IP_MAX-REORDER_IP_MIN+1];
	reorder_table->stacks=(ts_mbuf_stack **)malloc((REORDER_IP_MAX-REORDER_IP_MIN+1)*sizeof(ts_mbuf_stack*));
	
	if(reorder_table->stacks==NULL){
		fprintf(stderr,"%s %d malloc fail!",__func__,__LINE__);
                exit(-1);
	}
	reorder_table->size=REORDER_IP_MAX-REORDER_IP_MIN+1;
	for(i=0;i<=REORDER_IP_MAX-REORDER_IP_MIN;i++){
		reorder_table->stacks[i]=(ts_mbuf_stack*)malloc(sizeof(ts_mbuf_stack));
		if(reorder_table->stacks[i]==NULL){
			fprintf(stderr,"%s %d malloc fail!",__func__,__LINE__);
                	exit(-1);
		}
		ts_mbuf_stack_init(reorder_table->stacks[i],REORDER_STACK_LEVEL);
		reorder_counter[i]=0;
		all_counter[i]=0;
		reorder_ratio[i]=0;
	}
	#endif

	while(!force_quit){
		if(likely(count !=0)) {
			nb_rcv=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_rcv;
			deq_num=rte_ring_mc_dequeue_bulk(c2s_reorder_process_queue, pkts_burst,nb_rcv,NULL);
			if (unlikely(deq_num==0)){//cause deq_num is 0 either nb_rcv,if deq_num==0,then continue
				count=0;
                continue;
			} 
			for(i=0;i<deq_num;i++){
				m = pkts_burst[i];
        		eth_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,0);
				if(eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN)){
					struct rte_vlan_hdr *vhdr=rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,sizeof(struct rte_ether_hdr));
					if(vhdr->eth_proto = RTE_BE16(RTE_ETHER_TYPE_IPV4))
						ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_vlan_hdr)+sizeof(struct rte_ether_hdr));
					else{
						enq_num=rte_ring_mp_enqueue(c2s_send_queue,m);
					}
				}
				else if(eth_hdr->ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
        			ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_ether_hdr));
				else{
					enq_num=rte_ring_mp_enqueue(c2s_send_queue,m);
					just_send_num+=enq_num;
				}
				//dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
        		src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
				//ts_mbuf_stack_push(reorder_table->stacks[src_ip%reorder_table->size],m);
				#ifdef TCP_CRR
				if (ip_hdr->next_proto_id == IPPROTO_TCP){
					tcp_hdr= rte_pktmbuf_mtod_offset(m,struct rte_tcp_hdr *,sizeof(struct rte_vlan_hdr)+sizeof(struct rte_ether_hdr)+sizeof(struct rte_ipv4_hdr));
				}
				else{
					enq_num=rte_ring_mp_enqueue(c2s_send_queue,m);
					just_send_num+=enq_num;
				}
				src_port=rte_be_to_cpu_16(tcp_hdr->src_port);
				it=src_port%reorder_table->size;
				if(reorder_ratio[it]<REORDER_RATIO){
					if(1+ts_mbuf_stack_size(reorder_table->stacks[it])>=reorder_table->stacks[it]->capacity){
						enq_num=rte_ring_mp_enqueue(c2s_send_queue_highpri,m);
						while(ts_mbuf_stack_size(reorder_table->stacks[it])>0){
							ts_tmp=ts_mbuf_stack_pop(reorder_table->stacks[it]);
							if(ts_tmp->mbuf==NULL){
								fprintf(stderr,"%s %d pop fail!",__func__,__LINE__);
								exit(-1);
							}
							enq_num=rte_ring_mp_enqueue(c2s_send_queue_highpri,ts_tmp->mbuf);
							free(ts_tmp);
							reorder_counter[it]+=2;
							all_counter[it]+=2;
							if(all_counter[it]==0){
								reorder_counter[it]=0;
								all_counter[it]=1/REORDER_RATIO;
							}
							reorder_ratio[it]=reorder_counter[it]/all_counter[it];
						}
					}
					else{
						struct ts_mbuf *ts_m_push=(struct ts_mbuf *)malloc(sizeof(struct ts_mbuf));
						//ts_m_push->ts=(struct timespec *)malloc(sizeof(struct timespec));
						clock_gettime(CLOCK_MONOTONIC,&(ts_m_push->ts));
						/*
						struct timespec *tmpspec;
						ts_m_ins->ts->tv_nsec=300;
						ts_m_ins->ts->tv_sec=0;
						*/
						timespec_add_ns(&(ts_m_push->ts),REORDER_STACK_TIMER);
						ts_m_push->mbuf=m;
						ts_mbuf_stack_push(reorder_table->stacks[it],ts_m_push);
					}
				}
				else{
					enq_num=rte_ring_mp_enqueue(c2s_send_queue,m);
					all_counter[it]++;
					if(all_counter[it]==0){
						reorder_counter[it]=0;
						all_counter[it]=1/REORDER_RATIO;
					}
					reorder_ratio[it]=reorder_counter[it]/all_counter[it];
				}
				#else
				it=src_ip%reorder_table->size;
				if(reorder_ratio[it]<REORDER_RATIO){
					if(1+ts_mbuf_stack_size(reorder_table->stacks[it])>=reorder_table->stacks[it]->capacity){
						enq_num=rte_ring_mp_enqueue(c2s_send_queue_highpri,m);
						while(ts_mbuf_stack_size(reorder_table->stacks[it])>0){
							ts_tmp=ts_mbuf_stack_pop(reorder_table->stacks[it]);
							if(ts_tmp->mbuf==NULL){
								fprintf(stderr,"%s %d pop fail!",__func__,__LINE__);
								exit(-1);
							}
							enq_num=rte_ring_mp_enqueue(c2s_send_queue_highpri,ts_tmp->mbuf);
							free(ts_tmp);
							reorder_counter[it]+=2;
							all_counter[it]+=2;
							if(all_counter[it]==0){
								reorder_counter[it]=0;
								all_counter[it]=1/REORDER_RATIO;
							}
							reorder_ratio[it]=reorder_counter[it]/all_counter[it];
						}
					}
					else{
						struct ts_mbuf *ts_m_push=(struct ts_mbuf *)malloc(sizeof(struct ts_mbuf));
						//ts_m_push->ts=(struct timespec *)malloc(sizeof(struct timespec));
						clock_gettime(CLOCK_MONOTONIC,&(ts_m_push->ts));
						/*
						struct timespec *tmpspec;
						ts_m_ins->ts->tv_nsec=300;
						ts_m_ins->ts->tv_sec=0;
						*/
						timespec_add_ns(&(ts_m_push->ts),REORDER_STACK_TIMER);
						ts_m_push->mbuf=m;
						ts_mbuf_stack_push(reorder_table->stacks[it],ts_m_push);
					}
				}
				else{
					enq_num=rte_ring_mp_enqueue(c2s_send_queue,m);
					all_counter[it]++;
					if(all_counter[it]==0){
						reorder_counter[it]=0;
						all_counter[it]=1/REORDER_RATIO;
					}
					reorder_ratio[it]=reorder_counter[it]/all_counter[it];
				}
				#endif
			}
		}
	
		if(unlikely(count==0)){
			count =  rte_ring_count(c2s_reorder_process_queue);
		}
		cur_tsc = rte_rdtsc();
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {
			/* if timer is enabled */
			if (timer_period > 0) {
				/* advance the timer */
				timer_tsc += diff_tsc;
				/* if timer has reached its timeout */
				if (unlikely(timer_tsc >= timer_period)) {
					inspect_stream_table();
					/* reset the timer */
					timer_tsc = 0;	
				}
			}
			prev_tsc = cur_tsc;
		}

	}
}
/*C2S timestamp dump core*/
int c2s_dump_main_loop(){
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST],*m;
	int nb_tx,i,n,enq_num,deq_num,dump_to_sendqueue=0,available=0;
	int pkt_gap,burst_width;//ms
	int times=1;
	unsigned lcore_id;
    unsigned int count;
	struct timeval start,end,last,now;
	struct list_node *node;
	lcore_id = rte_lcore_id();
    count = 0;
	
	fprintf(stderr,"lcore %d——c2s_dumper\n",lcore_id);
	
    FILE *file = fopen("./burst-width.txt", "a");
    if(file == NULL)
    {
        printf("open burst-width file error!\n");
        exit(-1);
    }

	gettimeofday(&start,NULL);
    while (!force_quit) {
		if(likely(count !=0)) {
			nb_tx=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_tx;
			deq_num=rte_ring_sc_dequeue_bulk(c2s_dump_process_queue,pkts_burst,nb_tx,&available);
			if(deq_num==0) {
				deq_num=rte_ring_sc_dequeue_bulk(c2s_dump_process_queue,pkts_burst,available,NULL);
			}
			if(deq_num==0) continue;

			/*process section*/
			last=now;
			gettimeofday(&now,NULL);
			pkt_gap=width_compute(&last,&now);
			if(pkt_gap>=400)
			//if(belong_to_one_burst(&last,&now)!=0)//diffrence greater than 700ms
			{			
				//fprintf(file, "burst %d,width is %d\n", times,burst_width);
				end=last;
				burst_width=width_compute(&start,&end);
				//fprintf(file, "start sec is  %d , usec is %d ,end sec is  %d , usec is %d ,\n", start.tv_sec,start.tv_usec,end.tv_sec,end.tv_usec);
				fprintf(file, "sec %d, burst %d,width is %d\n",now.tv_sec, times,burst_width);
				fflush(file);
				start=now;
				times++;
			}

			enq_num=rte_ring_mp_enqueue_bulk(c2s_send_queue, pkts_burst,deq_num,NULL);
			dump_to_sendqueue+=enq_num;
        }
		if(unlikely(count==0)){
			count =  rte_ring_count(c2s_dump_process_queue);
		}	
	}
	fprintf(stderr,"lcore %d——c2s_dumper:c2s_dump_main_loop,dump_to_sendqueue is %d,now the dumpqueue ringcount is %d\n"
			,lcore_id,dump_to_sendqueue,rte_ring_count(c2s_dump_process_queue));
}

/*C2S policy maker*/
int c2s_policy_main_loop(){

	int current_count,last_count,change_count;
	change_count=last_count=current_count=0;
	timer_t timerid; 
    struct sigevent evp; 
    struct sigaction act; 
	timing=FALSE;//state of if timer start,timing TRUE mean storaging packets,FALSE mean we are sending packet or no packet in
	fprintf(stderr,"lcore %d——c2s policy maker\n",rte_lcore_id());
    memset(&act, 0, sizeof(act)); 
    act.sa_handler = due_timer_handler; 
    act.sa_flags = 0; 
    sigemptyset(&act.sa_mask); 
    if (sigaction(DUE_TIMER_SIG, &act, NULL) == -1) 
    { 
        perror("fail to sigaction"); 
        exit(-1); 
    } 

    memset(&evp, 0, sizeof(struct sigevent)); 
    evp.sigev_signo = DUE_TIMER_SIG; 
    evp.sigev_notify = SIGEV_SIGNAL; 
    if (timer_create(CLOCK_REALTIME, &evp, &timerid) == -1) 
    { 
        perror("fail to timer_create"); 
        exit(-1); 
    } 

    struct itimerspec it; 
    it.it_interval.tv_sec = 0; 
    it.it_interval.tv_nsec = 0; 
    it.it_value.tv_sec = 0; 
    it.it_value.tv_nsec = BUFFER_TIME*1000000;


	#ifndef DIST_MODE //正常模式缓冲
	current_rate=RATE_CONTROL*1.0;
	while (!force_quit) {
		current_count =  rte_ring_count(c2s_send_queue)+rte_ring_count(c2s_send_queue_highpri);
		if(current_count!=0 && timing==FALSE && send_state==FALSE){//here , bursts start get in
			timing=TRUE;
    		if (timer_settime(timerid, 0, &it, 0) == -1) { 
        		perror("fail to timer_settime"); 
        		exit(-1); 
			}
		}

		#ifdef RING_THRESHOLD
		if(current_count>=RING_THRESHOLD ){//here , bursts start get in
			send_state=TRUE;
			if (timer_settime(timerid, 0, &it, 0) == -1) { 
        		perror("fail to get rid of timer"); 
        		exit(-1); 
			}
		}
		#endif
		if(current_count==0 && timing==FALSE  && send_state==TRUE ){
			send_state=FALSE;
		}
	}
	#else//速率变化的模式
	unsigned lcore_id,i=0;
	uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;
	uint64_t timer_period = (uint64_t)(0.0005 * rte_get_timer_hz());//1==1s.0.001==1ms
	const uint64_t drain_tsc = ((rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S) * BURST_TX_DRAIN_US;
	prev_tsc = 0;
	timer_tsc = 0;
	while (!force_quit) {
		if(send_state==TRUE){
			cur_tsc = rte_rdtsc();
			diff_tsc = cur_tsc - prev_tsc;
			if (unlikely(diff_tsc > drain_tsc)) {
				/* if timer is enabled */
				if (timer_period > 0) {
					/* advance the timer */
					timer_tsc += diff_tsc;
					/* if timer has reached its timeout */
					if (unlikely(timer_tsc >= timer_period)) {
						/*update current rate*/

						//current_rate=(shaping_dist->table[i]-shaping_min*1.0)/(shaping_max-shaping_min*1.0)*100.0;//shrink the table's value to 0~10000
						//current_rate=( (double)( (int)( (current_rate+0.005)*100 ) ) )/100;//keep two significant digits
						i++;
						if((6+i*1.10)<100){
							current_rate=(6+i*1.10);
						}
						else if((6+i*1.10)>=100){
							current_rate=(100-((6+i*1.10)-100));
							if((100-((6+i*1.10)-100))<=10){
								send_state=FALSE;
								i=0;
								//fprintf(stderr,"policy maker stop send \n");
							}
						}
						fprintf(stderr,"policy maker i is %d, current_rate was set to %f\n",i,current_rate);
						//fprintf(stderr,"policy maker shaping_dist->table[%d] is %d, current_rate was set to %f\n",i,shaping_dist->table[i],current_rate);
						//i++;
						//if(i>=shaping_dist->size)
						//	i=0;

						/* reset the timer */
						timer_tsc = 0;	
					}
				}
				prev_tsc = cur_tsc;
			}
		}
		current_count =  rte_ring_count(c2s_send_queue);
		if(current_count!=0 && timing==FALSE && send_state==FALSE){//here , bursts start get in
			timing=TRUE;
    		if (timer_settime(timerid, 0, &it, 0) == -1) { 
        		perror("fail to timer_settime"); 
        		exit(-1); 
			}
		}
		if(current_count==0  && timing==FALSE  && send_state==TRUE ){
			send_state=FALSE;
		}
	}
	#endif

	fprintf(stderr,"lcore %d——c2s policy maker:finished\n",rte_lcore_id());
	return 0;

}

void move_all_pkt(struct rte_ring * src,struct rte_ring * dst){
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	uint64_t count=0,deq_num,nb_tx,enq_num,n;
	unsigned available;
	count=rte_ring_count(src);
	while(count){
		nb_tx=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
		count-=nb_tx;
		deq_num=rte_ring_sc_dequeue_bulk(src,pkts_burst,nb_tx,&available);
		if(deq_num==0) {
			deq_num=rte_ring_sc_dequeue_bulk(src,pkts_burst,available,NULL);
		}
		if(deq_num==0) continue;

		n = rte_ring_mp_enqueue_bulk(dst, &pkts_burst,deq_num,NULL);
							
		while (unlikely(n < deq_num)) {
			enq_num=rte_ring_mp_enqueue_bulk(dst, &pkts_burst[n],deq_num-n,NULL);
			n+=enq_num;
		}
		count=rte_ring_count(src);
	}
}


/* C2S ratecontrol sender ,通过掺杂不定长无效包进行控速*/
int c2s_rate_control_send_main_loop(){
	if (GAP_DIST_MODE==0){
		int deq_default=10000,supply_size=1,send_size=10000;
		struct rte_mbuf *valid_array[deq_default];
		struct rte_mbuf *send_burst[send_size];
		double rate_ratio=RATE_CONTROL*1.0;//Reciprocal 
		int deq_num=0,available=0;
		int current_len,total_len,void_len;
		int void_num,last_void_pkt_len,supply_counter;
		int valid_head,valid_tail,array_end;
		int nb_tx,n,tmpn;
		int queue_flag=0;

		unsigned lcore_id= rte_lcore_id();
		fprintf(stderr,"lcore %d——c2s_rate_control_sender,GAP_DIST_MODE==0\n",lcore_id);

		int i,j,k;
		for(i=60;i<=MAX_VOID_PKT_LEN;i++){
			make_void_packs(i,0);
		}
		while (!force_quit) {
			if(send_state==TRUE){
					if(rte_ring_count(c2s_send_queue)!=0||rte_ring_count(c2s_send_queue_highpri)!=0) {
						//dequeue valid pkt
						if(current_rate>=100){
							rate_ratio=100;
							//fprintf(stderr,"1 line %d ,rate_ratio is %f\n",__LINE__,rate_ratio);
						}
						else if(current_rate>=-0.00001 && current_rate<=0.00001){
							
							//fprintf(stderr,"2 line %d ,rate_ratio is %f\n",__LINE__,rate_ratio);
							continue;
						}
						else{
							rate_ratio=current_rate;
							//fprintf(stderr,"3 line %d ,rate_ratio is %f\n",__LINE__,rate_ratio);
						}
					
						if(rte_ring_count(c2s_send_queue_highpri)!=0){
							deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue_highpri,valid_array,deq_default,&available);
							if(deq_num==0) {
								deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue_highpri,valid_array,available,NULL);
							}
							if(deq_num==0) continue;
							queue_flag=1;
						}
						else{
							deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue,valid_array,deq_default,&available);
							if(deq_num==0) {
								deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue,valid_array,available,NULL);
							}
							if(deq_num==0) continue;
							queue_flag=2;
						}
					#ifdef DEBUG 
						fprintf(stderr,"deq_num is %d\n",deq_num);
					#endif
						valid_head=0;  valid_tail=0; array_end=deq_num;
					RELOOP:
						//get enough valid pkt
						for(valid_tail=valid_head,current_len=0;valid_tail<array_end;valid_tail++){
						#ifdef DEBUG
							fprintf(stderr," line %d ,valid_tail is  %d ,valid_head  is  %d  ,valid_array[%d]->pkt_len is %d \n",
								__LINE__,valid_tail,valid_head,valid_tail,valid_array[valid_tail]->pkt_len);
						#endif
							if(valid_array[valid_tail]==NULL){
								fprintf(stderr,"%s %d get NULL mbuf, exit!!,get form %d",__func__,__LINE__,queue_flag);
								exit(-1);
							}
							current_len+=valid_array[valid_tail]->pkt_len;
							//total_len=(int)(current_len/(100-rate_ratio*1.0));
							total_len=current_len/(rate_ratio*0.01)-current_len;
							if(total_len>=60) break;
						}//if the forloop finish , valid_tail is 100;
					
						if(unlikely(valid_head==valid_tail&&valid_tail==array_end)){//send over
							#ifdef DEBUG
							fprintf(stderr," line %d ,valid_tail is %d,send over ,continue \n",__LINE__,valid_tail);
							#endif
							continue;
						}
						if(unlikely(valid_tail==array_end/* && valid_head<valid_tail-1 */&& total_len<60)){//valid_array not enough
							if(rte_ring_count(c2s_send_queue)==0||rate_ratio==100){//reach the end of the ring , just send;
								nb_tx=valid_tail-valid_head;
								#ifdef DEBUG
								for(k=valid_head;k<valid_tail;k++){
									fprintf(stderr," line %d ,valid_tail is  %d ,valid_head  is  %d  ,valid_array[%d]->pkt_len is %d \n",
										__LINE__,valid_tail,valid_head,k,valid_array[i]->pkt_len);
								}
								#endif
								n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&valid_array[valid_head],nb_tx);
								while(n<nb_tx){
									tmpn=rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&valid_array[valid_head+n],nb_tx-n);
									n+=tmpn;
								}
								packet_sent_to_server_with_payload+=n;
								rate_ratio=current_rate;

								
								#ifdef DEBUG
								//fprintf(stderr,"after send, rte_ring_count is %d, packet_sent_to_server_with_payload is %llu,goto reloop \n",
									//rte_ring_count(c2s_send_queue),packet_sent_to_server_with_payload);
								#endif
								continue;
								//break;
							}
							else {//rebulid valid_array
								for(i=0;i<valid_tail-valid_head;i++){
									valid_array[i]=valid_array[valid_head+i];
								}
								deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue,&valid_array[i],deq_default-i,&available);
								#ifdef DEBUG
									fprintf(stderr," line %d ,valid_tail is %d, valid_head is %d ,deq_num is %d,current_len is %d,total_len is %d\n",
									__LINE__,valid_tail,valid_head,deq_num,current_len,total_len);
								#endif
								if(deq_num==0) {
									fprintf(stderr,"%s %d deq fail ,current_rate is%f,rate_ratio is %f,valid_tail-valid_head is %d,deq_default is %d,i is %d, available is %d,ring count is %d\n"
										,__func__,__LINE__,current_rate,rate_ratio,valid_tail-valid_head,deq_default,i,available,rte_ring_count(c2s_send_queue));
									deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue,&valid_array[i],available,NULL);
									if(deq_num==0) {
										fprintf(stderr,"%s %d deq fail , available is %d,ring count is %d\n",__func__,__LINE__,available,rte_ring_count(c2s_send_queue));
										exit(-1);
									}
									valid_tail=valid_tail-valid_head+available;
									valid_head=0;
									nb_tx=valid_tail-valid_head;
									#ifdef DEBUG
									fprintf(stderr,"deq_num is %d\n",deq_num);
									fprintf(stderr," line %d ,valid_tail is  %d ,valid_head  is  %d ,nb_tx  is  %d ,available is %d,ringcount is %d\n",
										__LINE__,valid_tail,valid_head,nb_tx,available,rte_ring_count(c2s_send_queue));
									#endif
									n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&valid_array[valid_head],nb_tx);
									while(n<nb_tx){
										tmpn=rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&valid_array[valid_head+n],nb_tx-n);
										n+=tmpn;
									}
									packet_sent_to_server_with_payload+=n;
									rate_ratio=current_rate;

									#ifdef DEBUG
									fprintf(stderr,"after send, rte_ring_count is %d, packet_sent_to_server_with_payload is %llu,goto reloop \n",rte_ring_count(c2s_send_queue),packet_sent_to_server_with_payload);
									#endif
									continue;
									//break;
								}
								array_end=deq_num+valid_tail-valid_head;
								#ifdef DEBUG
								fprintf(stderr,"array_end is %d,deq_num is %d ,valid_tail-valid_head+1 is %d\n",array_end,deq_num,valid_tail-valid_head);
								#endif
								valid_head=0;  valid_tail=0;
								goto RELOOP;
							}//r -l 1-9 -n 2  -- -P -p 0x3 --config="(0,0,1),(1,0,2)"
						}

						//compute void pkt number
						void_num=total_len/2044;
						last_void_pkt_len=total_len%2044;
						supply_counter=0;
						while(last_void_pkt_len<60){
							supply_counter++;
							last_void_pkt_len+=void_num*supply_size;
						}
						if(last_void_pkt_len>2044){
							supply_counter--;
							last_void_pkt_len=60;
						}
						#ifdef DEBUG
						fprintf(stderr,"line %d before get_void_pkts,pkt first len is %d,void num is %d ,last len is %d,supply_counteris %d\n",
						__LINE__,2044-(supply_counter*supply_size),void_num,last_void_pkt_len,supply_counter);
						#endif
						//get enough void pkt
						get_void_pkts(min(void_num,MAX_VOID_BURST_SIZE),2044-(supply_counter*supply_size));
						get_void_pkts(1,last_void_pkt_len);

						//mix void pkt
						for(i=0;i<=valid_tail-valid_head;i++){
							send_burst[i]=valid_array[valid_head+i];
							#ifdef DEBUG
							fprintf(stderr,"mix valid pkt %d,pktlen is %d\n",i,send_burst[i]->pkt_len);
							#endif
						}

						for(j=i,k=0;j<i+void_num;j++,k++){
							send_burst[j] = void_packs[2044-(supply_counter*supply_size)][k%MAX_VOID_BURST_SIZE];
							#ifdef DEBUG
							fprintf(stderr,"mix void pkt %d,pktlen is %d\n",j,send_burst[j]->pkt_len);
							#endif
						}
						//send
						nb_tx=(valid_tail-valid_head+1)+(void_num+1);
						send_burst[nb_tx-1]=void_packs[last_void_pkt_len][0];

						#ifdef DEBUG
						fprintf(stderr,"mix void last one pkt %d,pktlen is %d\n",nb_tx-1,send_burst[nb_tx-1]->pkt_len);
						#endif

						#ifdef DEBUG
						fprintf(stderr,"valid_tail-valid_head+1 is %d,valid_tail is  %d ,valid_head  is  %d ,nb_tx  is  %d ,current_len  is  %d , total_len   is  %d ,void_num+1  is  %d ,supply_counter is %d,last_void_pkt_len  is  %d \n",
							(valid_tail-valid_head+1),valid_tail,valid_head,nb_tx,current_len,total_len,void_num+1,supply_counter,last_void_pkt_len);
						//for(k=0;k<nb_tx;k++){
							//fprintf(stderr,"pkt %d len is %d\n",k,send_burst[k]->pkt_len);
						//}
						fprintf(stderr,"before send \n");
						#endif
						n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,send_burst,nb_tx);
						while(n<nb_tx){ 
							tmpn=rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&send_burst[n],nb_tx-n);
							n+=tmpn;
						}
						packet_sent_to_server_with_payload+=n-(void_num+1);
						valid_head=valid_tail+1;
						rate_ratio=current_rate;

					#ifdef DEBUG
						fprintf(stderr,"after send, rte_ring_count is %d,valid_head is %d packet_sent_to_server_with_payload is %llu,goto reloop \n",rte_ring_count(c2s_send_queue),valid_head,packet_sent_to_server_with_payload);				
					#endif
						goto RELOOP;
					}
				
					if(unlikely(rte_ring_count(c2s_send_queue)==0)){
						continue;
					}
			}

		}
			fprintf(stderr,"lcore %d——c2s_rate_control_sender:packet_sent_to_server_with_payload num is %llu,now the c2s_send_queue is %d,c2s_send_queue_highpri is %d\n",
		lcore_id,packet_sent_to_server_with_payload,rte_ring_count(c2s_send_queue),rte_ring_count(c2s_send_queue_highpri));
	}
	else if(GAP_DIST_MODE==1){
		int deq_default=10000,supply_size=1,send_size=10000;
		struct rte_mbuf *valid_array[deq_default];
		struct rte_mbuf *send_burst[send_size];
		double rate_ratio=RATE_CONTROL*1.0;//Reciprocal 
		int deq_num=0,available=0;
		int current_len,total_len,void_len;
		double current_gap,total_gap,void_gap;
		int void_num,last_void_pkt_len,supply_counter;
		int valid_head,valid_tail,array_end;
		int nb_tx,n,tmpn;
		srand(0);
		unsigned lcore_id= rte_lcore_id();
		fprintf(stderr,"lcore %d——c2s_rate_control_sender,GAP_DIST_MODE==1\n",lcore_id);

		int i,j,k;
		for(i=60;i<=MAX_VOID_PKT_LEN;i++){
			make_void_packs(i,0);
		}
		
		init_crandom(gap_corr,GAP_CORR);

		while (!force_quit) {
			if(send_state==TRUE){
				//dequeue valid pkt
				if(rte_ring_count(c2s_send_queue)!=0) {
					deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue,valid_array,deq_default,&available);
					if(deq_num==0) {
						deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue,valid_array,available,NULL);
					}
					if(deq_num==0) continue;
				}
				
				valid_head=0;  valid_tail=0; array_end=deq_num;
				for(valid_tail=valid_head,current_len=0;valid_tail<array_end;valid_tail++){
					//get current valid pkt->len
					current_len=valid_array[valid_tail]->pkt_len;

					/*get random number and corresponding pkt gap*/

					//int rand=gap_pool->table[rand()%gap_pool->size];//This code will cause inhomogeneity, which will be improved later @whk 2022.4.18
					
					int rand=get_dist_rand(GAP_MEAN,GAP_JITTER/4,gap_corr,gap_pool)-GAP_ERROR_CORRECTION;//return ns gap

					//get invalid pkt of corresponding length according to the pkt gap
					total_len = rand * 10/8;//10G device(10G = 10  bits/ns)

					void_len = total_len-current_len;

					if(void_len<64){
						send_burst[0]=valid_array[valid_tail];
						nb_tx=1;
						n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,send_burst,nb_tx);
						while(n<nb_tx){ 
							tmpn=rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&send_burst[n],nb_tx-n);
							n+=tmpn;
						}
						packet_sent_to_server_with_payload+=1;
					}
					else{
						void_num=void_len/2044;
						last_void_pkt_len=void_len%2044;
						supply_counter=0;
						while(last_void_pkt_len<60){
							supply_counter++;
							last_void_pkt_len+=void_num*supply_size;
						}
						if(last_void_pkt_len>2044){
							supply_counter--;
							last_void_pkt_len=60;
						}
						get_void_pkts(min(void_num,MAX_VOID_BURST_SIZE),2044-(supply_counter*supply_size));
						get_void_pkts(1,last_void_pkt_len);
						send_burst[0]=valid_array[valid_tail];
						for(j=1,k=0;j<1+void_num;j++,k++){
							send_burst[j] = void_packs[2044-(supply_counter*supply_size)][k%MAX_VOID_BURST_SIZE];
						}
						nb_tx=1+(void_num+1);
						send_burst[nb_tx-1]=void_packs[last_void_pkt_len][0];
						n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,send_burst,nb_tx);
						while(n<nb_tx){ 
							tmpn=rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&send_burst[n],nb_tx-n);
							n+=tmpn;
						}
						packet_sent_to_server_with_payload+=n-(void_num+1);
					}
				}//if the forloop finish , valid_tail is 100;
			}

			if(unlikely(rte_ring_count(c2s_send_queue)==0)){
				continue;
			}
		}
			fprintf(stderr,"lcore %d——c2s_rate_control_sender:packet_sent_to_server_with_payload num is %llu,now the ringcount is %d\n",
		lcore_id,packet_sent_to_server_with_payload,rte_ring_count(c2s_send_queue));
	}
	else{
		fprintf(stderr,"func %s invalid GAP_DIST_MODE,line %d",__func__,__LINE__);
		exit(-1);
	}
}

/* C2S ratecontrol sender ,通过计时器控制包间隔分布*/
int c2s_rate_control_send_main_loop_compare(){

	int deq_default=10000,supply_size=1,send_size=10000;
	struct rte_mbuf *valid_array[deq_default];
	struct rte_mbuf *send_burst[send_size];
	double rate_ratio=RATE_CONTROL*1.0;//Reciprocal 
	int deq_num=0,available=0;
	int current_len,total_len,void_len;
	double current_gap,total_gap,void_gap;
	int void_num,last_void_pkt_len,supply_counter;
	int valid_head,valid_tail,array_end;
	int nb_tx,n,tmpn;
	struct timespec now,send_time;
	srand(0);
	unsigned lcore_id= rte_lcore_id();
	fprintf(stderr,"lcore %d——c2s_rate_control_sender,GAP_DIST_MODE==1\n",lcore_id);

	int i,j,k;
	for(i=60;i<=MAX_VOID_PKT_LEN;i++){
		make_void_packs(i,0);
	}
		
	init_crandom(gap_corr,GAP_CORR);

	while (!force_quit) {
			if(send_state==TRUE){
				//dequeue valid pkt
				if(rte_ring_count(c2s_send_queue)!=0) {
					deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue,valid_array,deq_default,&available);
					if(deq_num==0) {
						deq_num=rte_ring_sc_dequeue_bulk(c2s_send_queue,valid_array,available,NULL);
					}
					if(deq_num==0) continue;
				}
				
				valid_head=0;  valid_tail=0; array_end=deq_num;
				for(valid_tail=valid_head,current_len=0;valid_tail<array_end;valid_tail++){
					//get current valid pkt->len

					/*get random number and corresponding pkt gap*/
					int rand=get_dist_rand(GAP_MEAN,GAP_JITTER/4,gap_corr,gap_pool);//return ns gap
					clock_gettime(CLOCK_MONOTONIC,&send_time);
					timespec_add_ns(&send_time,rand);

					clock_gettime(CLOCK_MONOTONIC,&now);

					while(timespeccmp(&now,&send_time, < )){
						clock_gettime(CLOCK_MONOTONIC,&now);
					}
					send_burst[0]=valid_array[valid_tail];
					nb_tx=1;
					n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,send_burst,nb_tx);
					while(n<nb_tx){ 
						tmpn=rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&send_burst[n],nb_tx-n);
						n+=tmpn;
					}
					packet_sent_to_server_with_payload+=1;
				}//if the forloop finish , valid_tail is 100;
			}

			if(unlikely(rte_ring_count(c2s_send_queue)==0)){
				continue;
			}
		}
			fprintf(stderr,"lcore %d——c2s_rate_control_sender:packet_sent_to_server_with_payload num is %llu,now the ringcount is %d\n",
		lcore_id,packet_sent_to_server_with_payload,rte_ring_count(c2s_send_queue));
}

/*S2C mean Server to Client direction */
/*S2C receiver*/
int s2c_receive_main_loop(){
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	unsigned lcore_id;
	int i, nb_rx;
	uint64_t enq_num;
	uint16_t portid;
	uint8_t queueid;
	struct lcore_conf *qconf;
	int test;

	lcore_id = rte_lcore_id();
	packet_received_from_server=0;

	qconf = &lcore_conf[lcore_id];
	fprintf(stderr,"lcore %d——s2c_receiveer\n",lcore_id);

	if (qconf->n_rx_queue == 0) {//?
		RTE_LOG(INFO, l2shaping, "lcore %u has nothing to do\n", lcore_id);
		return 0;
	}
	RTE_LOG(INFO, l2shaping, "entering main loop on lcore %u\n", lcore_id);
	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		RTE_LOG(INFO, l2shaping,
			" -- lcoreid=%u portid=%u rxqueueid=%hhu\n",
			lcore_id, portid, queueid);
	}
	while (!force_quit) {
		/*
		 * Read packet from RX queues
		 */
		for (i = 0; i < qconf->n_rx_queue; ++i) {
			portid = qconf->rx_queue_list[i].port_id;
			queueid = qconf->rx_queue_list[i].queue_id;
			nb_rx = rte_eth_rx_burst(PORT_TO_SERVER, queueid, pkts_burst,
				MAX_PKT_BURST);		
			if (nb_rx == 0)
				continue;
		#if 0
			struct rte_ipv4_hdr *ip_hdr;
     		struct rte_ether_hdr *eth_hdr;
     		int tmpj=0;
     		uint32_t dst_ip,src_ip;
     		struct rte_ether_addr dst_mac,src_mac;
     		for(;tmpj<nb_rx;tmpj++){
        		struct rte_mbuf *m = pkts_burst[tmpj];
        		eth_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ether_hdr *,0);
        		ip_hdr = rte_pktmbuf_mtod_offset(m, struct rte_ipv4_hdr *,sizeof(struct rte_ether_hdr));
        		char buf[RTE_ETHER_ADDR_FMT_SIZE];
        		dst_mac= eth_hdr->d_addr;
        		src_mac= eth_hdr->s_addr;
        		rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &dst_mac);
	    		printf("dst mac is %s,",buf);
        		rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, &src_mac);
	    		printf("src mac is %s,", buf);        
        		dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);
        		src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
        		printf("dst ip  %d.%d.%d.%d ,src ip  %d.%d.%d.%d from port %d\n",
        		dst_ip >> 24 & 0xff,dst_ip>>16 & 0xff,dst_ip>>8 & 0xff,dst_ip& 0xff,     
        		src_ip >> 24 & 0xff,src_ip>>16 & 0xff,src_ip>>8 & 0xff,src_ip& 0xff,     
        		portid);
     		}
		#endif
            /*put packet in ring*/
            enq_num=rte_ring_mp_enqueue_bulk(s2c_receive_queue, pkts_burst,nb_rx,NULL);
			if(enq_num==0)	
				fprintf(stderr,"[%s] enq s2c_receive_queue fail,enq_num is %llu,nb_rx is %d,[%d]\n",__func__, enq_num,nb_rx, __LINE__);
			packet_received_from_server+=enq_num;
		}
	}
	fprintf(stderr,"lcore %d——s2c_receiveer:packet_received_from_server num is %llu\n",
		lcore_id,packet_received_from_server);

	return 0;
}

/*S2C sender*/
int s2c_send_main_loop(){
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int nb_tx,i,n,enq_num,deq_num,fail_num,available=0;
	unsigned lcore_id;
    unsigned int count;

	packet_sent_to_client_with_payload=0;
	fail_num=0;
	lcore_id = rte_lcore_id();
	fprintf(stderr,"lcore %d——s2c_sender\n",lcore_id);
    count = 0;

    while (!force_quit) {
		count =  rte_ring_count(s2c_send_queue);
		if(likely(count !=0)) {
			nb_tx=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_tx;
			deq_num=rte_ring_sc_dequeue_bulk(s2c_send_queue,pkts_burst,nb_tx,&available);
			if(deq_num==0) {
				deq_num=rte_ring_sc_dequeue_bulk(s2c_send_queue,pkts_burst,available,NULL);
			}
			if(deq_num==0) continue;
			n = rte_eth_tx_burst(PORT_TO_CLIENT, QUEUE_TO_CLIENT_WITH_PAYLOAD, pkts_burst, nb_tx);
			packet_sent_to_client_with_payload+=n;
			if (unlikely(n < nb_tx)) {
				enq_num=rte_ring_mp_enqueue_bulk(s2c_send_queue, &pkts_burst[n],nb_tx-n,NULL);
				fail_num+=enq_num;
				if(unlikely(enq_num!=nb_tx-n))	{
					rte_exit(EXIT_FAILURE, "[%s] enq s2c_send_queue fail,[%d]\n",__func__,  __LINE__);
				}
			}
        }
    }
	fprintf(stderr,"lcore %d——s2c_sender:packet_sent_to_client_with_payload  num is %llu,now the ringcount is %d,fail num is %d\n",
		lcore_id,packet_sent_to_client_with_payload,rte_ring_count(s2c_send_queue),fail_num);
}

/*S2C filter*/
int s2c_filter_main_loop(){
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int nb_trans,i,n,enq_num,deq_num,ret,available;
	unsigned lcore_id;
    int count;
	
	packet_sent_to_client_without_payload=0;
	lcore_id = rte_lcore_id();
    count = 0;
	fprintf(stderr,"lcore %d——s2c_filter\n",lcore_id);
	
    while (!force_quit) {
		if(likely(count !=0)) {
			nb_trans=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_trans;
			deq_num=rte_ring_sc_dequeue_bulk(s2c_receive_queue,pkts_burst,nb_trans,&available);
			if(deq_num==0) {
				deq_num=rte_ring_sc_dequeue_bulk(s2c_receive_queue,pkts_burst,available,NULL);
			}
			else{
				if(pkts_burst[0]->pkt_len>=0){
					enq_num=rte_ring_mp_enqueue_bulk(s2c_send_queue, pkts_burst,nb_trans,NULL);
					if(unlikely(enq_num!=nb_trans))	{
						rte_exit(EXIT_FAILURE, "s2c_filter_main_loop lcore enq c2s_send_queue fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans);
					}
				}
				else{
					n = rte_eth_tx_burst(PORT_TO_CLIENT, QUEUE_TO_CLIENT_WITHOUT_PAYLOAD, pkts_burst, nb_trans);
					//fprintf(stderr,"lcore %d in trans_main_loop，send success!!!!!\n",lcore_id);
					packet_sent_to_client_without_payload+=n;
					if (unlikely(n < nb_trans)) {
						enq_num=rte_ring_mp_enqueue_bulk(s2c_receive_queue, &pkts_burst[n],nb_trans-n,NULL);
						if(unlikely(enq_num!=nb_trans-n))	{
							rte_exit(EXIT_FAILURE, "s2c_filter_main_loop lcore enq c2s_send_queue fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans-n);
						}
					}
				}
			}
        }
		else{
			count =  rte_ring_count(s2c_receive_queue);
		}
    }
	fprintf(stderr,"lcore %d——s2c_filter:packet_sent_to_server_without_payload num is %llu,now the s2c_receive_queue count is %d\n",
		lcore_id,packet_sent_to_client_without_payload,rte_ring_count(s2c_receive_queue));
}

void
setup_lpm(const int socketid)
{
	struct rte_lpm6_config config;
	struct rte_lpm_config config_ipv4;
	unsigned i;
	int ret;
	char s[64];
	char abuf[INET6_ADDRSTRLEN];

	/* create the LPM table */
	config_ipv4.max_rules = IPV4_l2shaping_LPM_MAX_RULES;
	config_ipv4.number_tbl8s = IPV4_l2shaping_LPM_NUMBER_TBL8S;
	config_ipv4.flags = 0;
	snprintf(s, sizeof(s), "IPV4_l2shaping_LPM_%d", socketid);
	ipv4_l2shaping_lpm_lookup_struct[socketid] =
			rte_lpm_create(s, socketid, &config_ipv4);
	if (ipv4_l2shaping_lpm_lookup_struct[socketid] == NULL)
		rte_exit(EXIT_FAILURE,
			"Unable to create the l2shaping LPM table on socket %d\n",
			socketid);

	/* populate the LPM table */
	for (i = 0; i < IPV4_l2shaping_LPM_NUM_ROUTES; i++) {
		struct in_addr in;

		/* skip unused ports */
		if ((1 << ipv4_l2shaping_lpm_route_array[i].if_out &
				enabled_port_mask) == 0)
			continue;

		ret = rte_lpm_add(ipv4_l2shaping_lpm_lookup_struct[socketid],
			ipv4_l2shaping_lpm_route_array[i].ip,
			ipv4_l2shaping_lpm_route_array[i].depth,
			ipv4_l2shaping_lpm_route_array[i].if_out);

		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				"Unable to add entry %u to the l2shaping LPM table on socket %d\n",
				i, socketid);
		}

		in.s_addr = htonl(ipv4_l2shaping_lpm_route_array[i].ip);
		printf("LPM: Adding route %s / %d (%d)\n",
		       inet_ntop(AF_INET, &in, abuf, sizeof(abuf)),
			ipv4_l2shaping_lpm_route_array[i].depth,
			ipv4_l2shaping_lpm_route_array[i].if_out);
	}

	/* create the LPM6 table */
	snprintf(s, sizeof(s), "IPV6_l2shaping_LPM_%d", socketid);

	config.max_rules = IPV6_l2shaping_LPM_MAX_RULES;
	config.number_tbl8s = IPV6_l2shaping_LPM_NUMBER_TBL8S;
	config.flags = 0;
	ipv6_l2shaping_lpm_lookup_struct[socketid] = rte_lpm6_create(s, socketid,
				&config);
	if (ipv6_l2shaping_lpm_lookup_struct[socketid] == NULL)
		rte_exit(EXIT_FAILURE,
			"Unable to create the l2shaping LPM table on socket %d\n",
			socketid);

	/* populate the LPM table */
	for (i = 0; i < IPV6_l2shaping_LPM_NUM_ROUTES; i++) {

		/* skip unused ports */
		if ((1 << ipv6_l2shaping_lpm_route_array[i].if_out &
				enabled_port_mask) == 0)
			continue;

		ret = rte_lpm6_add(ipv6_l2shaping_lpm_lookup_struct[socketid],
			ipv6_l2shaping_lpm_route_array[i].ip,
			ipv6_l2shaping_lpm_route_array[i].depth,
			ipv6_l2shaping_lpm_route_array[i].if_out);

		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				"Unable to add entry %u to the l2shaping LPM table on socket %d\n",
				i, socketid);
		}

		printf("LPM: Adding route %s / %d (%d)\n",
		       inet_ntop(AF_INET6, ipv6_l2shaping_lpm_route_array[i].ip,
				 abuf, sizeof(abuf)),
		       ipv6_l2shaping_lpm_route_array[i].depth,
		       ipv6_l2shaping_lpm_route_array[i].if_out);
	}
}

int
lpm_check_ptype(int portid)
{
	int i, ret;
	int ptype_l3_ipv4 = 0, ptype_l3_ipv6 = 0;
	uint32_t ptype_mask = RTE_PTYPE_L3_MASK;

	ret = rte_eth_dev_get_supported_ptypes(portid, ptype_mask, NULL, 0);
	if (ret <= 0)
		return 0;

	uint32_t ptypes[ret];
 
	ret = rte_eth_dev_get_supported_ptypes(portid, ptype_mask, ptypes, ret);
	for (i = 0; i < ret; ++i) {
		if (ptypes[i] & RTE_PTYPE_L3_IPV4)
			ptype_l3_ipv4 = 1;
		if (ptypes[i] & RTE_PTYPE_L3_IPV6)
			ptype_l3_ipv6 = 1;
	}

	if (ptype_l3_ipv4 == 0)
		printf("port %d cannot parse RTE_PTYPE_L3_IPV4\n", portid);

	if (ptype_l3_ipv6 == 0)
		printf("port %d cannot parse RTE_PTYPE_L3_IPV6\n", portid);

	if (ptype_l3_ipv4 && ptype_l3_ipv6)
		return 1;

	return 0;

}

static inline void
lpm_parse_ptype(struct rte_mbuf *m)
{
	struct rte_ether_hdr *eth_hdr;
	uint32_t packet_type = RTE_PTYPE_UNKNOWN;
	uint16_t ether_type;

	eth_hdr = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
	ether_type = eth_hdr->ether_type;
	if (ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
		packet_type |= RTE_PTYPE_L3_IPV4_EXT_UNKNOWN;
	else if (ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV6))
		packet_type |= RTE_PTYPE_L3_IPV6_EXT_UNKNOWN;

	m->packet_type = packet_type;
}

uint16_t
lpm_cb_parse_ptype(uint16_t port __rte_unused, uint16_t queue __rte_unused,
		   struct rte_mbuf *pkts[], uint16_t nb_pkts,
		   uint16_t max_pkts __rte_unused,
		   void *user_param __rte_unused)
{
	unsigned int i;

	if (unlikely(nb_pkts == 0))
		return nb_pkts;
	rte_prefetch0(rte_pktmbuf_mtod(pkts[0], struct ether_hdr *));
	for (i = 0; i < (unsigned int) (nb_pkts - 1); ++i) {
		rte_prefetch0(rte_pktmbuf_mtod(pkts[i+1],
			struct ether_hdr *));
		lpm_parse_ptype(pkts[i]);
	}
	lpm_parse_ptype(pkts[i]);

	return nb_pkts;
}

/* Return ipv4/ipv6 lpm fwd lookup struct. */
void *
lpm_get_ipv4_l2shaping_lookup_struct(const int socketid)
{
	return ipv4_l2shaping_lpm_lookup_struct[socketid];
}

void *
lpm_get_ipv6_l2shaping_lookup_struct(const int socketid)
{
	return ipv6_l2shaping_lpm_lookup_struct[socketid];
}

/*edit */
int produce_main_loop(){
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int i, nb_rx,enq_num,ret;
 
	sleep(1);
	for(i=0;i<100000;){
		ret = init_ipv4_tcp_traffic(produce_packs_pool, pkts_burst, MAX_PKT_BURST,128);
		if (ret != MAX_PKT_BURST) {
			printf("Line %i: init_ipv4_tcp_traffic has failed!\n",
					__LINE__);
			return -1;
		}
		enq_num=rte_ring_mp_enqueue_bulk(c2s_send_queue, pkts_burst,MAX_PKT_BURST,NULL);
		i+=enq_num;
	}
	fprintf(stderr,"produce over,enq num  is %d\n",i);
	return 0;
}

static void
copy_buf_to_pkt_segs(void* buf, unsigned len, struct rte_mbuf *pkt,
		     unsigned offset)
{
	struct rte_mbuf *seg;
	void *seg_buf;
	unsigned copy_len;

	seg = pkt;
	while (offset >= seg->data_len) {
		offset -= seg->data_len;
		seg = seg->next;
	}
	copy_len = seg->data_len - offset;
	seg_buf = rte_pktmbuf_mtod_offset(seg, char *, offset);
	while (len > copy_len) {
		rte_memcpy(seg_buf, buf, (size_t) copy_len);
		len -= copy_len;
		buf = ((char*) buf + copy_len);
		seg = seg->next;
		seg_buf = rte_pktmbuf_mtod(seg, char *);
		copy_len = seg->data_len;
	}
	rte_memcpy(seg_buf, buf, (size_t) len);
}

static inline void
copy_buf_to_pkt(void* buf, unsigned len, struct rte_mbuf *pkt, unsigned offset)
{
	if (offset + len <= pkt->data_len) {
		rte_memcpy(rte_pktmbuf_mtod_offset(pkt, char *, offset),
			buf, (size_t) len);
		return;
	}
	copy_buf_to_pkt_segs(buf, len, pkt, offset);
}

void initialize_eth_header(struct rte_ether_hdr *eth_hdr,
		struct rte_ether_addr *src_mac,
		struct rte_ether_addr *dst_mac, uint16_t ether_type,
		uint8_t vlan_enabled, uint16_t van_id)
{
	rte_ether_addr_copy(dst_mac, &eth_hdr->d_addr);
	rte_ether_addr_copy(src_mac, &eth_hdr->s_addr);

	if (vlan_enabled) {
		struct rte_vlan_hdr *vhdr = (struct rte_vlan_hdr *)(
			(uint8_t *)eth_hdr + sizeof(struct rte_ether_hdr));

		eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);

		vhdr->eth_proto =  rte_cpu_to_be_16(ether_type);
		vhdr->vlan_tci = van_id;
	} else {
		eth_hdr->ether_type = rte_cpu_to_be_16(ether_type);
	}
}

uint16_t initialize_ipv4_header_proto(struct rte_ipv4_hdr *ip_hdr, uint32_t src_addr,
		uint32_t dst_addr, uint16_t pkt_data_len, uint8_t proto)
{
	uint16_t pkt_len;
	unaligned_uint16_t *ptr16;
	uint32_t ip_cksum;

	/*
	 * Initialize IP header.
	 */

	ip_hdr->version_ihl   = RTE_IPV4_VHL_DEF;
	ip_hdr->type_of_service   = 0;
	ip_hdr->fragment_offset = 0;
	ip_hdr->time_to_live   = IP_DEFTTL;
	ip_hdr->next_proto_id = proto;
	ip_hdr->packet_id = 0;
	ip_hdr->total_length   = rte_cpu_to_be_16(pkt_len);
	ip_hdr->src_addr = rte_cpu_to_be_32(src_addr);
	ip_hdr->dst_addr = rte_cpu_to_be_32(dst_addr);

	/*
	 * Compute IP header checksum.
	 */
	ptr16 = (unaligned_uint16_t *)ip_hdr;
	ip_cksum = 0;
	ip_cksum += ptr16[0]; ip_cksum += ptr16[1];
	ip_cksum += ptr16[2]; ip_cksum += ptr16[3];
	ip_cksum += ptr16[4];
	ip_cksum += ptr16[6]; ip_cksum += ptr16[7];
	ip_cksum += ptr16[8]; ip_cksum += ptr16[9];

	/*
	 * Reduce 32 bit checksum to 16 bits and complement it.
	 */
	ip_cksum = ((ip_cksum & 0xFFFF0000) >> 16) +
		(ip_cksum & 0x0000FFFF);
	ip_cksum %= 65536;
	ip_cksum = (~ip_cksum) & 0x0000FFFF;
	if (ip_cksum == 0)
		ip_cksum = 0xFFFF;
	ip_hdr->hdr_checksum = (uint16_t) ip_cksum;

	pkt_len = (uint16_t) (pkt_data_len - sizeof(struct rte_ipv4_hdr));
	return pkt_len;
}

uint16_t initialize_tcp_header(struct rte_tcp_hdr *tcp_hdr, uint16_t src_port,
		uint16_t dst_port, uint16_t pkt_data_len)
{
	uint16_t pkt_len;
	pkt_len = (uint16_t) (pkt_data_len - sizeof(struct rte_tcp_hdr));
	memset(tcp_hdr, 0, sizeof(struct rte_tcp_hdr));
	tcp_hdr->src_port = rte_cpu_to_be_16(src_port);
	tcp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
	return pkt_len;
}

int generate_packet_burst_proto(struct rte_mempool *mp,
		struct rte_mbuf **pkts_burst, struct rte_ether_hdr *eth_hdr,
		uint8_t vlan_enabled, void *ip_hdr,
		uint8_t ipv4, void *proto_hdr,
		int nb_pkt_per_burst, uint32_t pkt_len, uint8_t nb_pkt_segs)
{
	int i, nb_pkt = 0;
	size_t eth_hdr_size;

	struct rte_mbuf *pkt_seg;
	struct rte_mbuf *pkt;

	for (nb_pkt = 0; nb_pkt < nb_pkt_per_burst; nb_pkt++) {
		pkt = rte_pktmbuf_alloc(mp);
		if (pkt == NULL) {

	nomore_mbuf:
			if (nb_pkt == 0)
				return -1;
			break;
		}

		pkt->data_len = pkt_len;
		pkt_seg = pkt;
		for (i = 1; i < nb_pkt_segs; i++) {
			pkt_seg->next = rte_pktmbuf_alloc(mp);
			if (pkt_seg->next == NULL) {
				pkt->nb_segs = i;
				rte_pktmbuf_free(pkt);
				goto nomore_mbuf;
			}
			pkt_seg = pkt_seg->next;
			pkt_seg->data_len = pkt_len;
		}
		pkt_seg->next = NULL; /* Last segment of packet. */

		/*
		 * Copy headers in first packet segment(s).
		 */
		if (vlan_enabled)
			eth_hdr_size = sizeof(struct rte_ether_hdr) +
				sizeof(struct rte_vlan_hdr);
		else
			eth_hdr_size = sizeof(struct rte_ether_hdr);

		
		copy_buf_to_pkt(eth_hdr, eth_hdr_size, pkt, 0);
		copy_buf_to_pkt(ip_hdr, sizeof(struct rte_ipv4_hdr),pkt, eth_hdr_size);
		copy_buf_to_pkt(proto_hdr,sizeof(struct rte_tcp_hdr), pkt,eth_hdr_size +sizeof(struct rte_ipv4_hdr));
		//copy_buf_to_pkt(&tmp_tag,sizeof(uint32_t), pkt,eth_hdr_size +sizeof(struct rte_ipv4_hdr)+sizeof(struct rte_tcp_hdr));
		/*
		 * Complete first mbuf of packet and append it to the
		 * burst of packets to be transmitted.
		 */
		pkt->nb_segs = nb_pkt_segs;
		pkt->pkt_len = pkt_len;
		pkt->l2_len = eth_hdr_size;
		pkt->vlan_tci  = RTE_ETHER_TYPE_IPV4;
		pkt->l3_len = sizeof(struct rte_ipv4_hdr);
		pkts_burst[nb_pkt] = pkt;
	}
	return nb_pkt;
}

int init_ipv4_tcp_traffic(struct rte_mempool *mp,
	     struct rte_mbuf **pkts_burst, uint32_t burst_size,uint32_t packet_size)
{
	struct rte_ether_hdr pkt_eth_hdr;
	struct rte_ipv4_hdr pkt_ipv4_hdr;
	struct rte_tcp_hdr pkt_tcp_hdr;
	uint32_t src_addr = IPV4_ADDR(192, 168, 140, 100);
	uint32_t dst_addr = IPV4_ADDR(192, 168, 133, 100);
	uint16_t src_port = 16;
	uint16_t dst_port = 17;
	uint16_t pktlen;

	static uint8_t src_mac[] = { 0x00, 0xFF, 0xAA, 0xFF, 0xAA, 0xFF };
	static uint8_t dst_mac[] = { 0xE8, 0x61, 0x1F, 0x2E, 0xFA, 0xA2 };

	initialize_eth_header(&pkt_eth_hdr,
						(struct rte_ether_addr *)src_mac,
						(struct rte_ether_addr *)dst_mac, 
						RTE_ETHER_TYPE_IPV4, 0, 0);

	pktlen = (uint16_t)((packet_size-4)-sizeof(struct rte_ether_hdr));

	pktlen = initialize_ipv4_header_proto(&pkt_ipv4_hdr, src_addr, dst_addr, pktlen, IPPROTO_TCP);

	pktlen = initialize_tcp_header(&pkt_tcp_hdr, src_port, dst_port,pktlen);

	return generate_packet_burst_proto(mp, pkts_burst, &pkt_eth_hdr,
					0, &pkt_ipv4_hdr, 1,
					&pkt_tcp_hdr, burst_size,
					packet_size, 1);
}

int init_ipv4_void_traffic(struct rte_mempool *mp,
	     struct rte_mbuf **pkts_burst, uint32_t burst_size,uint32_t packet_size)
{
	struct rte_ether_hdr pkt_eth_hdr;
	struct rte_ipv4_hdr pkt_ipv4_hdr;
	struct rte_tcp_hdr pkt_tcp_hdr;
	uint32_t src_addr = IPV4_ADDR(11, 11, 11, 11);
	uint32_t dst_addr = IPV4_ADDR(11, 11, 11, 12);
	uint16_t src_port = 16;
	uint16_t dst_port = 17;
	uint16_t pktlen;

	static uint8_t src_mac[] = { 0x90, 0xE2, 0xBA, 0x16, 0x1A, 0x6C };
	static uint8_t dst_mac[] = { 0xAA, 0xAA, 0xBB, 0xBB, 0xCC, 0xCC };


	initialize_eth_header(&pkt_eth_hdr,
						(struct rte_ether_addr *)src_mac,
						(struct rte_ether_addr *)dst_mac, 
						RTE_ETHER_TYPE_IPV4, 0, 0);

	pktlen = (uint16_t)((packet_size)-sizeof(struct rte_ether_hdr));

	pktlen = initialize_ipv4_header_proto(&pkt_ipv4_hdr, src_addr, dst_addr, pktlen, IPPROTO_TCP);

	pktlen = initialize_tcp_header(&pkt_tcp_hdr, src_port, dst_port,pktlen);

	return generate_packet_burst_proto(mp, pkts_burst, &pkt_eth_hdr,
					0, &pkt_ipv4_hdr, 1,
					&pkt_tcp_hdr, burst_size,
					packet_size, 1);
}


int temp_debug_flag1=0;
int temp_debug_prop=0;//proportion of temp_debug_flag1 and

/*if flag==1,log the function call*/
void make_void_packs(uint32_t packet_size,int flag)
{
	if(flag==1){
		fprintf(stderr,"rate_control_lool want make void pack again! size is %lu err!!",packet_size);
	}
    int ret=0; 
    ret=init_ipv4_void_traffic(produce_packs_pool,void_packs[packet_size],MAX_VOID_BURST_SIZE,packet_size);
    if(ret==-1){
        fprintf(stderr,"make_void_packs err, init_ipv4_void_traffic fail!\n");
        exit(-1);
    }
}

void get_void_pkts(uint32_t burst_size,uint32_t packet_size)
{
    int i;
    if(burst_size>MAX_VOID_BURST_SIZE ||burst_size<0){
        fprintf(stderr,"get_void_pkts err, invalid burst_size!,burst_size is %d\n",burst_size);
        exit(-1);
    }
	if(packet_size>MAX_VOID_PKT_LEN ||packet_size<0){
        fprintf(stderr,"get_void_pkts err, invalid packet_size!packet_size is %d\n",packet_size);
        exit(-1);
    }
    if(void_packs[packet_size][0]==NULL){
        make_void_packs(packet_size,1);//加flag看是否调用这部分函数
		fprintf(stderr,"get_void_pkts again !!!!,exit(-1)\n");
		exit(-1);
    }
    for(i=0;i<burst_size;i++){
        void_packs[packet_size][i]->refcnt=32767;//any >1 number .
    }
}

/*计算因400ns导致的速率损失是否会造成实际上的速率*/
uint8_t delay_level(struct rte_mbuf *m){
	/*to be supplemented*/
	return 15;
}


int max(int a,int b){
	return a>=b?a:b;
}
int min(int a,int b){
	return a<=b?a:b;
}
