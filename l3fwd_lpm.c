/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2016 Intel Corporation
 */

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

#include "l3fwd.h"
#include <rte_ring.h>

#include "policy.h"

struct ipv4_l3fwd_lpm_route {
	uint32_t ip;
	uint8_t  depth;
	uint8_t  if_out;
};

struct ipv6_l3fwd_lpm_route {
	uint8_t  ip[16];
	uint8_t  depth;
	uint8_t  if_out;
};

/* 198.18.0.0/16 are set aside for RFC2544 benchmarking (RFC5735). */
static struct ipv4_l3fwd_lpm_route ipv4_l3fwd_lpm_route_array[] = {
	{RTE_IPV4(192, 168, 133, 0), 24, 0},
	{RTE_IPV4(192, 168, 140, 0), 24, 1},
	{RTE_IPV4(192, 168, 137, 0), 24, 1},
	{RTE_IPV4(192, 168, 136, 0), 24, 1},
	{RTE_IPV4(192, 168, 139, 0), 24, 1},
};

/* 2001:0200::/48 is IANA reserved range for IPv6 benchmarking (RFC5180) */
static struct ipv6_l3fwd_lpm_route ipv6_l3fwd_lpm_route_array[] = {
	{{32, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 32, 1},
};

#define IPV4_L3FWD_LPM_NUM_ROUTES \
	(sizeof(ipv4_l3fwd_lpm_route_array) / sizeof(ipv4_l3fwd_lpm_route_array[0]))
#define IPV6_L3FWD_LPM_NUM_ROUTES \
	(sizeof(ipv6_l3fwd_lpm_route_array) / sizeof(ipv6_l3fwd_lpm_route_array[0]))

#define IPV4_L3FWD_LPM_MAX_RULES         1024
#define IPV4_L3FWD_LPM_NUMBER_TBL8S (1 << 8)
#define IPV6_L3FWD_LPM_MAX_RULES         1024
#define IPV6_L3FWD_LPM_NUMBER_TBL8S (1 << 16)

struct rte_lpm *ipv4_l3fwd_lpm_lookup_struct[NB_SOCKETS];
struct rte_lpm6 *ipv6_l3fwd_lpm_lookup_struct[NB_SOCKETS];

static inline uint16_t
lpm_get_ipv4_dst_port(void *ipv4_hdr, uint16_t portid, void *lookup_struct)
{
	uint32_t next_hop;
	struct rte_lpm *ipv4_l3fwd_lookup_struct =
		(struct rte_lpm *)lookup_struct;

	return (uint16_t) ((rte_lpm_lookup(ipv4_l3fwd_lookup_struct,
		rte_be_to_cpu_32(((struct rte_ipv4_hdr *)ipv4_hdr)->dst_addr),
		&next_hop) == 0) ? next_hop : portid);
}

static inline uint16_t
lpm_get_ipv6_dst_port(void *ipv6_hdr, uint16_t portid, void *lookup_struct)
{
	uint32_t next_hop;
	struct rte_lpm6 *ipv6_l3fwd_lookup_struct =
		(struct rte_lpm6 *)lookup_struct;

	return (uint16_t) ((rte_lpm6_lookup(ipv6_l3fwd_lookup_struct,
			((struct rte_ipv6_hdr *)ipv6_hdr)->dst_addr,
			&next_hop) == 0) ?  next_hop : portid);
}

static __rte_always_inline uint16_t
lpm_get_dst_port(const struct lcore_conf *qconf, struct rte_mbuf *pkt,
		uint16_t portid)
{
	struct rte_ipv6_hdr *ipv6_hdr;
	struct rte_ipv4_hdr *ipv4_hdr;
	struct rte_ether_hdr *eth_hdr;

	if (RTE_ETH_IS_IPV4_HDR(pkt->packet_type)) {

		eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
		ipv4_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);

		return lpm_get_ipv4_dst_port(ipv4_hdr, portid,
					     qconf->ipv4_lookup_struct);
	} else if (RTE_ETH_IS_IPV6_HDR(pkt->packet_type)) {

		eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
		ipv6_hdr = (struct rte_ipv6_hdr *)(eth_hdr + 1);

		return lpm_get_ipv6_dst_port(ipv6_hdr, portid,
					     qconf->ipv6_lookup_struct);
	}

	return portid;
}

/*
 * lpm_get_dst_port optimized routine for packets where dst_ipv4 is already
 * precalculated. If packet is ipv6 dst_addr is taken directly from packet
 * header and dst_ipv4 value is not used.
 */
static __rte_always_inline uint16_t
lpm_get_dst_port_with_ipv4(const struct lcore_conf *qconf, struct rte_mbuf *pkt,
	uint32_t dst_ipv4, uint16_t portid)
{
	uint32_t next_hop;
	struct rte_ipv6_hdr *ipv6_hdr;
	struct rte_ether_hdr *eth_hdr;

	if (RTE_ETH_IS_IPV4_HDR(pkt->packet_type)) {
		return (uint16_t) ((rte_lpm_lookup(qconf->ipv4_lookup_struct,
						   dst_ipv4, &next_hop) == 0)
				   ? next_hop : portid);

	} else if (RTE_ETH_IS_IPV6_HDR(pkt->packet_type)) {

		eth_hdr = rte_pktmbuf_mtod(pkt, struct rte_ether_hdr *);
		ipv6_hdr = (struct rte_ipv6_hdr *)(eth_hdr + 1);

		return (uint16_t) ((rte_lpm6_lookup(qconf->ipv6_lookup_struct,
				ipv6_hdr->dst_addr, &next_hop) == 0)
				? next_hop : portid);

	}

	return portid;
}

#include "l3fwd_lpm.h"

unsigned int packet_received_from_client;
unsigned int packet_received_from_server;
unsigned int packet_sent_to_server_with_payload;
unsigned int packet_sent_to_server_with_payload_from_client;
unsigned int packet_sent_to_server_high_pri;
unsigned int packet_sent_to_server_low_pri;
unsigned int packet_sent_to_client_with_payload;
unsigned int packet_sent_to_server_without_payload;
unsigned int packet_sent_to_client_without_payload;

void get_void_packets(uint32_t burst_size,uint32_t packet_size);

static void
due_timer_handler(int sig)
{
	if (sig == DUE_TIMER_SIG) {
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
	printf("packet_in from client total: %d\n",packet_received_from_client);
	printf("packet_out to server total: %d\n",packet_sent_to_server_without_payload+packet_sent_to_server_with_payload);
	printf("packet_out to server without payload: %d\n",packet_sent_to_server_without_payload);
	printf("packet_out to server with payload: %d\n",packet_sent_to_server_with_payload);
	printf("packet_out to server high pri: %d\n",packet_sent_to_server_high_pri);
	printf("packet_out to server low pri : %d\n",packet_sent_to_server_low_pri);
	printf("\n\n\n");
	printf("====server  to client ====\n");
	printf("packet_in from server total: %d\n",packet_received_from_server);
	printf("packet_out to client total: %d\n",packet_sent_to_client_without_payload+packet_sent_to_client_with_payload);
	printf("packet_out to client without payload: %d\n",packet_sent_to_client_without_payload);
	printf("packet_out to client with payload: %d\n",packet_sent_to_client_with_payload);
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

/* main processing loop */
int lpm_main_loop(__attribute__((unused)) void *dummy)
{
	send_state=FALSE;
	if(IS_POLICY_LCORE)
		policy_main_loop();
    if(IS_RECEIVE_LCORE_FOR_CLIENT)
        receive_main_loop_from_client();
	if (IS_SEND_LCORE_TO_SERVER)
        send_main_loop_to_server();
    if (IS_RECEIVE_LCORE_FOR_SERVER)
        receive_main_loop_from_server();
	if (IS_SEND_LCORE_TO_CLIENT)
        send_main_loop_to_client();

	if (IS_TRANS_TO_SERVER)
		trans_main_loop_to_server();
	if (IS_TRANS_TO_CLIENT)
		trans_main_loop_to_client();
	if (IS_PRINT_LCORE)
		print_main_loop();
    return 0;
}

int print_main_loop_old(){
	unsigned lcore_id;
	uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;
	uint64_t timer_period = 10;//10 second

	uint64_t cycles=rte_rdtsc_precise();
	prev_tsc = rte_rdtsc();
	timer_tsc = 0;

	while (!force_quit) {
		cur_tsc = rte_rdtsc();
		diff_tsc = cur_tsc - prev_tsc;
		timer_tsc += diff_tsc;
		if (timer_tsc >= timer_period * cycles) {
			print_stats();
			timer_tsc = 0;
			prev_tsc = cur_tsc;
		}
	}
}

#define MAX_TIMER_PERIOD 86400 
#define US_PER_S 1000000
#define BURST_TX_DRAIN_US 100

int print_main_loop(){
	unsigned lcore_id;
	uint64_t prev_tsc, diff_tsc, cur_tsc, timer_tsc;
	uint64_t timer_period = 10 * rte_get_timer_hz();//10 second
	const uint64_t drain_tsc = ((rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S) * BURST_TX_DRAIN_US;
	prev_tsc = 0;
	timer_tsc = 0;
	

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
}

int trans_main_loop_to_server()
{
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int nb_trans,i,i2,n,enq_num,deq_num;
	unsigned lcore_id;
    int count;
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
	fprintf(stderr,"lcore %d in trans_main_loop_to_server\n",lcore_id);
    count = 0;

    while (!force_quit) {
		if(likely(count !=0)) {
			nb_trans=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_trans;

			deq_num=rte_ring_mc_dequeue_bulk(rte_list_trans_c2s, pkts_burst,nb_trans,NULL);
			if (unlikely(deq_num==0)){//cause deq_num is 0 either nb_trans,if deq_num==0,then continue
				count=0;
                continue;
			}
			else{
				if(pkts_burst[0]->pkt_len>=0){
					for(i2=0;i2<deq_num;i2++){
						eth_hdr= rte_pktmbuf_mtod_offset(pkts_burst[i2], struct rte_ether_hdr * ,0 );
						ether_type=rte_be_to_cpu_16(eth_hdr->ether_type);
						if(ether_type==RTE_ETHER_TYPE_VLAN){
							vhdr = (struct rte_vlan_hdr *)((uint8_t *)eth_hdr + sizeof(struct rte_ether_hdr));
							if(rte_be_to_cpu_16(vhdr->eth_proto)==RTE_ETHER_TYPE_IPV4){
								tcp_hdr = (struct rte_tcp_hdr *)((uint8_t *)eth_hdr + sizeof(struct rte_ether_hdr)+sizeof(struct rte_vlan_hdr)+sizeof(struct rte_ipv4_hdr));
								pri_check(tcp_hdr);
							}
						}
						else if(ether_type==RTE_ETHER_TYPE_IPV4){
							tcp_hdr = (struct rte_tcp_hdr *)((uint8_t *)eth_hdr + sizeof(struct rte_ether_hdr)+sizeof(struct rte_ipv4_hdr));
							pri_check(tcp_hdr);
						}
					}
					
					enq_num=rte_ring_mp_enqueue_bulk(rte_list_c2s, pkts_burst,nb_trans,NULL);
					if(unlikely(enq_num!=nb_trans))	{
						rte_exit(EXIT_FAILURE, "trans_main_loop_to_server lcore enq rte_list_c2s fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans);
					}
				}
				else{
					n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_SERVER_WITHOUT_PAYLOAD, pkts_burst, nb_trans);
					packet_sent_to_server_without_payload+=n;
					if (unlikely(n < nb_trans)) {
						enq_num=rte_ring_mp_enqueue_bulk(rte_list_trans_c2s, &pkts_burst[n],nb_trans-n,NULL);
						if(unlikely(enq_num!=nb_trans-n))	{
							rte_exit(EXIT_FAILURE, "trans_main_loop_to_server lcore enq rte_list_c2s fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans-n);
						}
					}
				}
			}
        }
		else{
			count =  rte_ring_count(rte_list_trans_c2s);
		}
    }
	
	fprintf(stderr,"lcore %d ,packet_sent_to_server_without_payload num is %d,now the ringcount is %d\n",lcore_id,packet_sent_to_server_without_payload,rte_ring_count(rte_list_c2s));
}

int trans_main_loop_to_client(){
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int nb_trans,i,n,enq_num,deq_num;
	unsigned lcore_id;
    int count;
	
	packet_sent_to_client_without_payload=0;
	lcore_id = rte_lcore_id();
	fprintf(stderr,"lcore %d in trans_main_loop_to_client\n",lcore_id);
    count = 0;

    while (!force_quit) {
		if(likely(count !=0)) {
			nb_trans=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_trans;

			deq_num=rte_ring_mc_dequeue_bulk(rte_list_trans_s2c, pkts_burst,nb_trans,NULL);
			if (unlikely(deq_num==0)){//cause deq_num is 0 either nb_trans,if deq_num==0,then continue
				count=0;
                continue;
			}
			else{
				if(pkts_burst[0]->pkt_len>=0){
					enq_num=rte_ring_mp_enqueue_bulk(rte_list_s2c, pkts_burst,nb_trans,NULL);
					if(unlikely(enq_num!=nb_trans))	{
						rte_exit(EXIT_FAILURE, "trans_main_loop_to_client lcore enq rte_list_c2s fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans);
					}
				}
				else{
					n = rte_eth_tx_burst(PORT_TO_CLIENT, QUEUE_TO_CLIENT_WITHOUT_PAYLOAD, pkts_burst, nb_trans);
					//fprintf(stderr,"lcore %d in trans_main_loop，send success!!!!!\n",lcore_id);
					packet_sent_to_client_without_payload+=n;
					if (unlikely(n < nb_trans)) {
						enq_num=rte_ring_mp_enqueue_bulk(rte_list_trans_s2c, &pkts_burst[n],nb_trans-n,NULL);
						if(unlikely(enq_num!=nb_trans-n))	{
							rte_exit(EXIT_FAILURE, "trans_main_loop_to_client lcore enq rte_list_c2s fail,enq_num is %d,nb_rx is %d\n",enq_num,nb_trans-n);
						}
					}
				}
			}

        }
		else{
			count =  rte_ring_count(rte_list_trans_s2c);
		}
    }
	
	fprintf(stderr,"lcore %d ,packet_sent_to_client_without_payload num is %d,now the ringcount is %d\n",lcore_id,packet_sent_to_client_without_payload,rte_ring_count(rte_list_c2s));
}

/* receiver processing loop */
int receive_main_loop_from_client(){
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
	fprintf(stderr,"lcore %d in receive_main_loop_from_client \n",lcore_id);

	if (qconf->n_rx_queue == 0) {
		RTE_LOG(INFO, L3FWD, "lcore %u has nothing to do\n", lcore_id);
		return 0;
	}

	RTE_LOG(INFO, L3FWD, "entering main loop on lcore %u\n", lcore_id);
	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		RTE_LOG(INFO, L3FWD,
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
			nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst,
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
            enq_num=rte_ring_mp_enqueue_bulk(rte_list_trans_c2s, pkts_burst,nb_rx,NULL);
			if(enq_num==0)	
				fprintf(stderr,"!!!!!!!!![%s] enq rte_list_trans_c2s fail,enq_num is %d,nb_rx is %d，[%d]!!!!!!!\n",__func__, enq_num,nb_rx, __LINE__);
			packet_received_from_client+=enq_num;
		}
	}
	fprintf(stderr,"lcore %d,packet_received_from_client num is %d\n",lcore_id,packet_received_from_client);

	return 0;
}

int receive_main_loop_from_server(){
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	unsigned lcore_id;
	int i, nb_rx,enq_num;
	uint16_t portid;
	uint8_t queueid;
	struct lcore_conf *qconf;
	int test;

	lcore_id = rte_lcore_id();
	packet_received_from_server=0;

	qconf = &lcore_conf[lcore_id];
	fprintf(stderr,"lcore %d in receive loop\n",lcore_id);
	if (qconf->n_rx_queue == 0) {//?
		RTE_LOG(INFO, L3FWD, "lcore %u has nothing to do\n", lcore_id);
		return 0;
	}
	RTE_LOG(INFO, L3FWD, "entering main loop on lcore %u\n", lcore_id);
	for (i = 0; i < qconf->n_rx_queue; i++) {
		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		RTE_LOG(INFO, L3FWD,
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
			nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst,
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
            enq_num=rte_ring_mp_enqueue_bulk(rte_list_trans_s2c, pkts_burst,nb_rx,NULL);
			if(enq_num==0)	
				fprintf(stderr,"[%s] enq rte_list_trans_s2c fail,enq_num is %d,nb_rx is %d，[%d]\n",__func__, enq_num,nb_rx, __LINE__);
			packet_received_from_server+=enq_num;
		}
	}
	fprintf(stderr,"lcore %d,packet_received_from_server num is %d\n",lcore_id,packet_received_from_server);

	return 0;
}

int send_main_loop_to_server(){
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int nb_tx,i,n,enq_num,deq_num;
	unsigned lcore_id;
    unsigned int count;
	packet_sent_to_server_with_payload=0;

	lcore_id = rte_lcore_id();
	fprintf(stderr,"lcore %d in send_main_loop_to_server\n",lcore_id);
    count = 0;

    while (!force_quit) {
		if(send_state==TRUE){

			if(likely(count !=0)) {
				nb_tx=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
				count-=nb_tx;
				deq_num=rte_ring_mc_dequeue_bulk(rte_list_c2s, pkts_burst,nb_tx,NULL);
				if (unlikely(deq_num==0)){//cause deq_num is 0 either nb_tx,if deq_num==0,then continue
					count=0;
                    continue;
				}
				n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_SERVER_WITH_PAYLOAD, pkts_burst, nb_tx);
				packet_sent_to_server_with_payload+=n;
				if (unlikely(n < nb_tx)) {
					enq_num=rte_ring_mp_enqueue_bulk(rte_list_c2s, &pkts_burst[n],nb_tx-n,NULL);
					if(unlikely(enq_num!=nb_tx-n))	{
						rte_exit(EXIT_FAILURE, "[%s] enq rte_list_c2s fail,[%d]\n",__func__,  __LINE__);	
					}
				}
        	}
			if(unlikely(count==0))
				count =  rte_ring_count(rte_list_c2s);
		}
    }
	fprintf(stderr,"lcore %d ,packet_sent_to_server_with_payload num is %d,now the ringcount is %d\n",lcore_id,packet_sent_to_server_with_payload,rte_ring_count(rte_list_c2s));
}

/*sender processing loop */
int rate_control_send_main_loop(){
    struct rte_mbuf *pkts_burst[(RATE_CONTROL*MAX_PKT_BURST/100)];//(RATE_CONTROL*MAX_PKT_BURST/100)
	struct rte_mbuf *send_burst[MAX_PKT_BURST];
	int nb_tx,n,n2,tmpn,enq_num,deq_num;
	unsigned lcore_id;
	uint16_t portid;
	uint8_t queueid;
	struct lcore_conf *qconf;
    unsigned int count;
	int sent_void_num=0;
	int i,j,k;
	uint32_t tmp_pkt_len=220;
	int count1,count2,send_over_count,send_again_count,enq_again_count;
	int c2,c4,ca;
	c2=0;c4=0;ca=0;
	send_again_count=0;
	enq_again_count=0;
	packet_sent_to_server_with_payload=0;
	lcore_id = rte_lcore_id();
	qconf = &lcore_conf[lcore_id];
	fprintf(stderr,"lcore %d in send loop\n",lcore_id);
    count = 0;

    while (!force_quit) {
		if(send_state==TRUE){
			if(likely(count !=0)) {
				nb_tx=(RATE_CONTROL*MAX_PKT_BURST/100);
				count-=nb_tx;
				deq_num=rte_ring_mc_dequeue_bulk(rte_list_c2s, pkts_burst,nb_tx,NULL);
				
				tmp_pkt_len=pkts_burst[0]->pkt_len;
				get_void_packets(MAX_PKT_BURST,tmp_pkt_len);

				i=0;j=0;k=0;
				for(;i<MAX_PKT_BURST;i++){
					if((i%10)>=(RATE_CONTROL/10)){
						send_burst[i]=void_packs[tmp_pkt_len][k];
						k++;
					}
					else{
						send_burst[i]=pkts_burst[j];
						j++;
					}
				}
				n = rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,send_burst,MAX_PKT_BURST);

				while(n<MAX_PKT_BURST){
					tmpn=rte_eth_tx_burst(PORT_TO_SERVER, QUEUE_TO_CLIENT_WITH_PAYLOAD,&send_burst[n],MAX_PKT_BURST-n);
					n+=tmpn;
				}
				packet_sent_to_server_with_payload+=nb_tx;

        	}
			if(unlikely(count==0))
				count =  rte_ring_count(rte_list_c2s);

		}
    }
	
	fprintf(stderr,"lcore %d ,packet_sent_to_server_with_payload num is %d,now the ringcount is %d\n",lcore_id,packet_sent_to_server_with_payload,rte_ring_count(rte_list_c2s));
}

int send_main_loop_to_client(){
    struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	int nb_tx,i,n,enq_num,deq_num,fail_num;
	unsigned lcore_id;
    unsigned int count;

	packet_sent_to_client_with_payload=0;
	fail_num=0;
	lcore_id = rte_lcore_id();
	fprintf(stderr,"lcore %d in send_main_loop_to_client\n",lcore_id);
    count = 0;

    while (!force_quit) {
		count =  rte_ring_count(rte_list_s2c);
		if(likely(count !=0)) {
			nb_tx=(count>MAX_PKT_BURST)?MAX_PKT_BURST:count;
			count-=nb_tx;
			deq_num=rte_ring_mc_dequeue_bulk(rte_list_s2c, pkts_burst,nb_tx,NULL);
			if (unlikely(deq_num==0)){//cause deq_num is 0 either nb_tx,if deq_num==0,then continue
				count=0;
                continue;
			}
			n = rte_eth_tx_burst(PORT_TO_CLIENT, QUEUE_TO_CLIENT_WITH_PAYLOAD, pkts_burst, nb_tx);
			packet_sent_to_client_with_payload+=n;
			if (unlikely(n < nb_tx)) {
				enq_num=rte_ring_mp_enqueue_bulk(rte_list_s2c, &pkts_burst[n],nb_tx-n,NULL);
				fail_num+=enq_num;
				if(unlikely(enq_num!=nb_tx-n))	{
					rte_exit(EXIT_FAILURE, "[%s] enq rte_list_s2c fail,[%d]\n",__func__,  __LINE__);
				}
			}
        }
    }
	
	fprintf(stderr,"lcore %d ,packet_sent_to_client_with_payload  num is %d,now the ringcount is %d,fail num is %d\n",
	lcore_id,packet_sent_to_client_with_payload,rte_ring_count(rte_list_s2c),fail_num);
}

/* policy maker processing loop */
int policy_main_loop(){
	int current_count,last_count,change_count;
	change_count=last_count=current_count=0;
	timer_t timerid; 
    struct sigevent evp; 
    struct sigaction act; 
	timing=FALSE;//state of if timer start,timing TRUE mean storaging packets,FALSE mean we are sending packet or no packet in
	fprintf(stderr,"lcore %d in receive loop\n",rte_lcore_id());
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
    it.it_value.tv_nsec = 20000000; //20ms
	
	
	while (!force_quit) {
		
		current_count =  rte_ring_count(rte_list_c2s);

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
		#endif

		if(current_count==0  && timing==FALSE  && send_state==TRUE ){
			send_state=FALSE;
		}

	}	
	return 0;
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
	config_ipv4.max_rules = IPV4_L3FWD_LPM_MAX_RULES;
	config_ipv4.number_tbl8s = IPV4_L3FWD_LPM_NUMBER_TBL8S;
	config_ipv4.flags = 0;
	snprintf(s, sizeof(s), "IPV4_L3FWD_LPM_%d", socketid);
	ipv4_l3fwd_lpm_lookup_struct[socketid] =
			rte_lpm_create(s, socketid, &config_ipv4);
	if (ipv4_l3fwd_lpm_lookup_struct[socketid] == NULL)
		rte_exit(EXIT_FAILURE,
			"Unable to create the l3fwd LPM table on socket %d\n",
			socketid);

	/* populate the LPM table */
	for (i = 0; i < IPV4_L3FWD_LPM_NUM_ROUTES; i++) {
		struct in_addr in;

		/* skip unused ports */
		if ((1 << ipv4_l3fwd_lpm_route_array[i].if_out &
				enabled_port_mask) == 0)
			continue;

		ret = rte_lpm_add(ipv4_l3fwd_lpm_lookup_struct[socketid],
			ipv4_l3fwd_lpm_route_array[i].ip,
			ipv4_l3fwd_lpm_route_array[i].depth,
			ipv4_l3fwd_lpm_route_array[i].if_out);

		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				"Unable to add entry %u to the l3fwd LPM table on socket %d\n",
				i, socketid);
		}

		in.s_addr = htonl(ipv4_l3fwd_lpm_route_array[i].ip);
		printf("LPM: Adding route %s / %d (%d)\n",
		       inet_ntop(AF_INET, &in, abuf, sizeof(abuf)),
			ipv4_l3fwd_lpm_route_array[i].depth,
			ipv4_l3fwd_lpm_route_array[i].if_out);
	}

	/* create the LPM6 table */
	snprintf(s, sizeof(s), "IPV6_L3FWD_LPM_%d", socketid);

	config.max_rules = IPV6_L3FWD_LPM_MAX_RULES;
	config.number_tbl8s = IPV6_L3FWD_LPM_NUMBER_TBL8S;
	config.flags = 0;
	ipv6_l3fwd_lpm_lookup_struct[socketid] = rte_lpm6_create(s, socketid,
				&config);
	if (ipv6_l3fwd_lpm_lookup_struct[socketid] == NULL)
		rte_exit(EXIT_FAILURE,
			"Unable to create the l3fwd LPM table on socket %d\n",
			socketid);

	/* populate the LPM table */
	for (i = 0; i < IPV6_L3FWD_LPM_NUM_ROUTES; i++) {

		/* skip unused ports */
		if ((1 << ipv6_l3fwd_lpm_route_array[i].if_out &
				enabled_port_mask) == 0)
			continue;

		ret = rte_lpm6_add(ipv6_l3fwd_lpm_lookup_struct[socketid],
			ipv6_l3fwd_lpm_route_array[i].ip,
			ipv6_l3fwd_lpm_route_array[i].depth,
			ipv6_l3fwd_lpm_route_array[i].if_out);

		if (ret < 0) {
			rte_exit(EXIT_FAILURE,
				"Unable to add entry %u to the l3fwd LPM table on socket %d\n",
				i, socketid);
		}

		printf("LPM: Adding route %s / %d (%d)\n",
		       inet_ntop(AF_INET6, ipv6_l3fwd_lpm_route_array[i].ip,
				 abuf, sizeof(abuf)),
		       ipv6_l3fwd_lpm_route_array[i].depth,
		       ipv6_l3fwd_lpm_route_array[i].if_out);
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
lpm_get_ipv4_l3fwd_lookup_struct(const int socketid)
{
	return ipv4_l3fwd_lpm_lookup_struct[socketid];
}

void *
lpm_get_ipv6_l3fwd_lookup_struct(const int socketid)
{
	return ipv6_l3fwd_lpm_lookup_struct[socketid];
}




/*edit */
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
		int nb_pkt_per_burst, uint8_t pkt_len, uint8_t nb_pkt_segs,uint32_t tag)
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
		uint32_t tmp_tag=tag+nb_pkt;
		copy_buf_to_pkt(&tmp_tag,sizeof(uint32_t), pkt,eth_hdr_size +sizeof(struct rte_ipv4_hdr)+sizeof(struct rte_tcp_hdr));
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
	     struct rte_mbuf **pkts_burst, uint32_t burst_size,uint32_t packet_size,uint32_t tag)
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
					(uint8_t)(packet_size-4), 1,tag);
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

	pktlen = (uint16_t)((packet_size-4)-sizeof(struct rte_ether_hdr));

	pktlen = initialize_ipv4_header_proto(&pkt_ipv4_hdr, src_addr, dst_addr, pktlen, IPPROTO_TCP);

	pktlen = initialize_tcp_header(&pkt_tcp_hdr, src_port, dst_port,pktlen);

	return generate_packet_burst_proto(mp, pkts_burst, &pkt_eth_hdr,
					0, &pkt_ipv4_hdr, 1,
					&pkt_tcp_hdr, burst_size,
					(uint8_t)(packet_size-4), 1,0);
}

/*
suggestion:
nb_pkts should equal or less than 100;
rate_percentage should is the power of 10;

*/

int rate_control_tx_burst(uint16_t port_id,uint16_t queue_id,struct rte_mbuf ** tx_pkts,uint16_t nb_pkts,uint16_t rate_percentage)
{
	struct rte_mbuf *send_burst[100];
	uint16_t nb_tx,tmp_pkt_len,n,tmpn;
	uint16_t i,j,k;

	if(rate_percentage>=100){
		rate_percentage=100;
	}

	tmp_pkt_len=tx_pkts[0]->pkt_len;

	j=0;
	while(j<nb_pkts){
		
		get_void_packets(100,tmp_pkt_len);
		
		k=0;
		for(i=0;i<100;i++){
			if((i%10)<(rate_percentage/10)&&j<nb_pkts){
				send_burst[i]=tx_pkts[j];
				j++;
			}
			else{
				send_burst[i]=void_packs[tmp_pkt_len][k];
				k++;
			}
		}
		n = rte_eth_tx_burst(port_id, queue_id,send_burst,100);
		
		while(n<100){
			tmpn=rte_eth_tx_burst(port_id, queue_id,&send_burst[n],100-n);
			n+=tmpn;
		}
	}
	return 0;
}



void make_void_packs(int packet_size)
{
    int ret=0; 
    if(packet_size>MAX_VOID_PKT_LEN ||packet_size<0){
        fprintf(stderr,"make_void_packs err, invalid packet_size!\n");
        exit(-1);
    }
    ret=init_ipv4_void_traffic(produce_packs_pool,void_packs[packet_size],MAX_VOID_BURST_SIZE,packet_size);
    if(ret==-1){
        fprintf(stderr,"make_void_packs err, init_ipv4_void_traffic fail!\n");
        exit(-1);
    }
}


void get_void_packets(uint32_t burst_size,uint32_t packet_size)
{
    int i;
    if(burst_size>MAX_VOID_BURST_SIZE ||burst_size<0){
        fprintf(stderr,"make_void_packs err, invalid packet_size!\n");
        return;
    }
    if(void_packs[packet_size][0]==NULL){
        make_void_packs(packet_size);
    }
    for(i=0;i<burst_size;i++){
        void_packs[packet_size][i]->refcnt=16;//anyway,  >1 is fine.
    }
}