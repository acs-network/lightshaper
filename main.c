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
#include <signal.h>
#include <stdbool.h>

#include <rte_common.h>
#include <rte_vect.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>
#include <rte_cpuflags.h>

#include <cmdline_parse.h>
#include <cmdline_parse_etheraddr.h>

#include "l2shaping.h"
#include "l2shaping_policy.h"
#include <unistd.h>
#include <execinfo.h>
#include <time.h>

#define BACKTRACE_SIZE 16
/*
 * Configurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 1024
#define RTE_TEST_TX_DESC_DEFAULT 1024

#define MAX_TX_QUEUE_PER_PORT RTE_MAX_ETHPORTS
#define MAX_RX_QUEUE_PER_PORT 128

#define MAX_LCORE_PARAMS 1024


/* Static global variables used within this file. */
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/**< Ports set in promiscuous mode off by default. */
static int promiscuous_on;

/* Select Longest-Prefix or Exact match. */
static int l2shaping_lpm_on;

/* Global variables. */

static int numa_on = 1; /**< NUMA is enabled by default. */
static int parse_ptype; /**< Parse packet type using rx callback, and */
			/**< disabled by default */
static int per_port_pool; /**< Use separate buffer pools per port; disabled */
			  /**< by default */

volatile bool force_quit;

/* ethernet addresses of ports */
struct rte_ether_addr ports_eth_addr[RTE_MAX_ETHPORTS];


/* mask of enabled ports */
uint32_t enabled_port_mask;

/* Used only in exact match mode. */
int ipv6; /**< ipv6 is false by default. */
uint32_t hash_entry_number = HASH_ENTRY_NUMBER_DEFAULT;

struct lcore_conf lcore_conf[RTE_MAX_LCORE];

struct lcore_params {
	uint16_t port_id;
	uint8_t queue_id;
	uint8_t lcore_id;
} __rte_cache_aligned;

static struct lcore_params lcore_params_array[MAX_LCORE_PARAMS];
static struct lcore_params lcore_params_array_default[] = {
	{0, 0, 2},
	{0, 1, 2},
	{0, 2, 2},
	{1, 0, 2},
	{1, 1, 2},
	{1, 2, 2},
	{2, 0, 2},
	{3, 0, 3},
	{3, 1, 3},
};

static struct lcore_params * lcore_params = lcore_params_array_default;
static uint16_t nb_lcore_params = sizeof(lcore_params_array_default) /
				sizeof(lcore_params_array_default[0]);

static struct rte_eth_conf port_conf = {
	.rxmode = {
		.mq_mode = ETH_MQ_RX_RSS,
		.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.offloads = DEV_RX_OFFLOAD_CHECKSUM,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_IP,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

static struct rte_mempool *pktmbuf_pool[RTE_MAX_ETHPORTS][NB_SOCKETS];
static uint8_t lkp_per_socket[NB_SOCKETS];

struct l2shaping_lkp_mode {
	void  (*setup)(int);
	int   (*check_ptype)(int);
	rte_rx_callback_fn cb_parse_ptype;
	int   (*main_loop)(void *);
	void* (*get_ipv4_lookup_struct)(int);
	void* (*get_ipv6_lookup_struct)(int);
};

static struct l2shaping_lkp_mode l2shaping_lkp;

static struct l2shaping_lkp_mode l2shaping_lpm_lkp = {
	.setup                  = setup_lpm,
	.check_ptype		= lpm_check_ptype,
	.cb_parse_ptype		= lpm_cb_parse_ptype,
	.main_loop              = lpm_main_loop,
	.get_ipv4_lookup_struct = lpm_get_ipv4_l2shaping_lookup_struct,
	.get_ipv6_lookup_struct = lpm_get_ipv6_l2shaping_lookup_struct,
};

/*
 * Setup lookup methods for forwarding.
 * Currently exact-match and longest-prefix-match
 * are supported ones.
 */
static void
setup_l2shaping_lookup_tables(void)
{
	/* Setup HASH lookup functions. */
		l2shaping_lkp = l2shaping_lpm_lkp;
}

static int
check_lcore_params(void)
{
	uint8_t queue, lcore;
	uint16_t i;
	int socketid;

	for (i = 0; i < nb_lcore_params; ++i) {
		queue = lcore_params[i].queue_id;
		if (queue >= MAX_RX_QUEUE_PER_PORT) {
			printf("invalid queue number: %hhu\n", queue);
			return -1;
		}
		lcore = lcore_params[i].lcore_id;
		if (!rte_lcore_is_enabled(lcore)) {
			printf("error: lcore %hhu is not enabled in lcore mask\n", lcore);
			return -1;
		}
		if ((socketid = rte_lcore_to_socket_id(lcore) != 0) &&
			(numa_on == 0)) {
			printf("warning: lcore %hhu is on socket %d with numa off \n",
				lcore, socketid);
		}
	}
	return 0;
}

static int
check_port_config(void)
{
	uint16_t portid;
	uint16_t i;

	for (i = 0; i < nb_lcore_params; ++i) {
		portid = lcore_params[i].port_id;
		if ((enabled_port_mask & (1 << portid)) == 0) {
			printf("port %u is not enabled in port mask\n", portid);
			return -1;
		}
		if (!rte_eth_dev_is_valid_port(portid)) {
			printf("port %u is not present on the board\n", portid);
			return -1;
		}
	}
	return 0;
}

static uint8_t
get_port_n_rx_queues(const uint16_t port)
{
	int queue = -1;
	uint16_t i;

	for (i = 0; i < nb_lcore_params; ++i) {
		if (lcore_params[i].port_id == port) {
			if (lcore_params[i].queue_id == queue+1)
				queue = lcore_params[i].queue_id;
			else
				rte_exit(EXIT_FAILURE, "queue ids of the port %d must be"
						" in sequence and must start with 0\n",
						lcore_params[i].port_id);
		}
	}
	return (uint8_t)(++queue);
}

static int
init_lcore_rx_queues(void)
{
	uint16_t i, nb_rx_queue;
	uint8_t lcore;

	for (i = 0; i < nb_lcore_params; ++i) {
		lcore = lcore_params[i].lcore_id;
		nb_rx_queue = lcore_conf[lcore].n_rx_queue;
		if (nb_rx_queue >= MAX_RX_QUEUE_PER_LCORE) {
			printf("error: too many queues (%u) for lcore: %u\n",
				(unsigned)nb_rx_queue + 1, (unsigned)lcore);
			return -1;
		} else {
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].port_id =
				lcore_params[i].port_id;
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].queue_id =
				lcore_params[i].queue_id;
			lcore_conf[lcore].n_rx_queue++;
		}
	}
	return 0;
}

/* display usage */
static void
print_usage(const char *prgname)
{
	fprintf(stderr, "%s [EAL options] --"
		" -p PORTMASK"
		" [-P]"
		" [-E]"
		" [-L]"
		" --config (port,queue,lcore)[,(port,queue,lcore)]"
		" [--eth-dest=X,MM:MM:MM:MM:MM:MM]"
		" [--enable-jumbo [--max-pkt-len PKTLEN]]"
		" [--no-numa]"
		" [--hash-entry-num]"
		" [--ipv6]"
		" [--parse-ptype]"
		" [--per-port-pool]\n\n"

		"  -p PORTMASK: Hexadecimal bitmask of ports to configure\n"
		"  -P : Enable promiscuous mode\n"
		"  -E : Enable exact match\n"
		"  -L : Enable longest prefix match (default)\n"
		"  --config (port,queue,lcore): Rx queue configuration\n"
		"  --eth-dest=X,MM:MM:MM:MM:MM:MM: Ethernet destination for port X\n"
		"  --enable-jumbo: Enable jumbo frames\n"
		"  --max-pkt-len: Under the premise of enabling jumbo,\n"
		"                 maximum packet length in decimal (64-9600)\n"
		"  --no-numa: Disable numa awareness\n"
		"  --hash-entry-num: Specify the hash entry number in hexadecimal to be setup\n"
		"  --ipv6: Set if running ipv6 packets\n"
		"  --parse-ptype: Set to use software to analyze packet type\n"
		"  --per-port-pool: Use separate buffer pool per port\n\n",
		prgname);
}

static int
parse_max_pkt_len(const char *pktlen)
{
	char *end = NULL;
	unsigned long len;

	/* parse decimal string */
	len = strtoul(pktlen, &end, 10);
	if ((pktlen[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (len == 0)
		return -1;

	return len;
}

static int
parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	/* parse hexadecimal string */
	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}

static int
parse_hash_entry_number(const char *hash_entry_num)
{
	char *end = NULL;
	unsigned long hash_en;
	/* parse hexadecimal string */
	hash_en = strtoul(hash_entry_num, &end, 16);
	if ((hash_entry_num[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (hash_en == 0)
		return -1;

	return hash_en;
}

static int
parse_config(const char *q_arg)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	enum fieldnames {
		FLD_PORT = 0,
		FLD_QUEUE,
		FLD_LCORE,
		_NUM_FLD
	};
	unsigned long int_fld[_NUM_FLD];
	char *str_fld[_NUM_FLD];
	int i;
	unsigned size;

	nb_lcore_params = 0;

	while ((p = strchr(p0,'(')) != NULL) {
		++p;
		if((p0 = strchr(p,')')) == NULL)
			return -1;

		size = p0 - p;
		if(size >= sizeof(s))
			return -1;

		snprintf(s, sizeof(s), "%.*s", size, p);
		if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') != _NUM_FLD)
			return -1;
		for (i = 0; i < _NUM_FLD; i++){
			errno = 0;
			int_fld[i] = strtoul(str_fld[i], &end, 0);
			if (errno != 0 || end == str_fld[i] || int_fld[i] > 255)
				return -1;
		}
		if (nb_lcore_params >= MAX_LCORE_PARAMS) {
			printf("exceeded max number of lcore params: %hu\n",
				nb_lcore_params);
			return -1;
		}
		lcore_params_array[nb_lcore_params].port_id =
			(uint8_t)int_fld[FLD_PORT];
		lcore_params_array[nb_lcore_params].queue_id =
			(uint8_t)int_fld[FLD_QUEUE];
		lcore_params_array[nb_lcore_params].lcore_id =
			(uint8_t)int_fld[FLD_LCORE];
		++nb_lcore_params;
	}
	lcore_params = lcore_params_array;
	return 0;
}

static int
dist_trans(struct disttable *src,
			struct disttable *dst,
			int mu, int sigma)
{
	if(src->size!=dst->size||src==NULL||dst==NULL){
		return -1;
	}
	int i=0;
	int64_t t,x;
	for(i=0;i<src->size;i++){
    	t = src->table[i];
    	x = (sigma % NETEM_DIST_SCALE) * t;

		if (x >= 0)
			x += NETEM_DIST_SCALE/2;
		else
			x -= NETEM_DIST_SCALE/2;
    
		dst->table[i] = x / NETEM_DIST_SCALE + (sigma / NETEM_DIST_SCALE) * t + mu;
	}
	if(DELAY_PREC){
		for(i=0;i<src->size;i++){
			dst->table[i]=(dst->table[i]/DELAY_PREC)*DELAY_PREC;
			
			fprintf(stderr,"%s,src->%d is %lld,dst->%d is %lld\n",__func__,i,src->table[i],i,dst->table[i]);
			src->table[i]=0;	
		}
	}
	dst->size=src->size;
	fprintf(stderr,"%lld\n",dst->table[IPV4_ADDR(192, 168, 112, 0)%dst->size]);
	return 0;
}

static int 
parse_dist_table(const char *dist_file,int flag){
	if(flag==1){
		FILE *file;
		char buf[1024] = "";  
		char *iscomment,*next,*tmp;
    	short i=0,num,length=0;
		shaping_max=0;
		shaping_min=0;

		file=fopen(dist_file,"r");

		if (file == NULL)
    	{
    	    fprintf(stderr,"dist_file open fail!\n");
    	    exit(-1);
    	}
		int a=0;
		while (fgets(buf, 1024, file)){
			iscomment = strchr(buf, '#');
			if (iscomment == buf) 
   	        	continue;
			if (iscomment != NULL)  
    	        *iscomment = 0;
    	    next=strtok_r(buf," ",&tmp);
    	    while(next!=NULL){
    	        next=strtok_r(NULL," ",&tmp);
    	        length++;
    	    }
		}
    	shaping_dist=(struct disttable*)malloc(sizeof(struct disttable)+length*sizeof(int64_t));
		if(shaping_dist==NULL){
			fprintf(stderr,"shaping_dist malloc fail!\n");
			exit(-1);
		}
    	shaping_dist->size=length;
    	fprintf(stderr,"shaping_dist->size is %d\n",shaping_dist->size);

    	int j=0;
    	fseek(file, 0L, SEEK_SET);
		fprintf(stderr,"\n");
		while (fgets(buf, 1024, file)){

			iscomment = strchr(buf, '#');
			if (iscomment == buf) 
        	    continue;
			if (iscomment != NULL)  
        	    *iscomment = 0;
        	next=strtok_r(buf," ",&tmp);
        	while(next!=NULL){
            	num= strtol(next, (char**)NULL, 10);
            	shaping_dist->table[j]=num;
            	j++;
            	fprintf(stderr,"%d",num);
            	next=strtok_r(NULL," ",&tmp);

				if(num>shaping_max) 
					shaping_max=num;
				if(num<shaping_min) 
					shaping_min=num;
				fprintf(stderr,"\n");
        	}
		}
		fseek(file, 0L, SEEK_SET);
		fprintf(stderr,"\n");
		double aaa;//check the rate in dist
		while (fgets(buf, 1024, file)){

			iscomment = strchr(buf, '#');
			if (iscomment == buf) 
            	continue;
			if (iscomment != NULL)  
            	*iscomment = 0;
        	next=strtok_r(buf," ",&tmp);
        	while(next!=NULL){
            	num= strtol(next, (char**)NULL, 10);
				aaa=(num-shaping_min*1.0)/(shaping_max-shaping_min*1.0)*100.0;
				aaa=( (double)( (int)( (aaa+0.005)*100 ) ) )/100;
            	fprintf(stderr,"%f",aaa);
            	next=strtok_r(NULL," ",&tmp);
				fprintf(stderr,"\n");
        	}
		}

    	fclose(file);
		fprintf(stderr,"shaping_max is %d, shaping_min is %d \n",shaping_max,shaping_min);
		return 0;
	}
	else if(flag==2){

		FILE *file;
		char buf[1024] = "";  
		char *iscomment,*next,*tmp;
    	int i=0,num,length=0;

		file=fopen(dist_file,"r");

		if (file == NULL)
    	{
    	    fprintf(stderr,"dist_file open fail!\n");
    	    exit(-1);
    	}
		int a=0;
		while (fgets(buf, 1024, file)){
			iscomment = strchr(buf, '#');
			if (iscomment == buf) 
   	        	continue;
			if (iscomment != NULL)  
    	        *iscomment = 0;
    	    next=strtok_r(buf," ",&tmp);
    	    while(next!=NULL){
    	        next=strtok_r(NULL," ",&tmp);
    	        length++;
    	    }
        
		}
    	gap_pool=(struct disttable*)malloc(sizeof(struct disttable)+length*sizeof(int64_t));
		if(gap_pool==NULL){
			fprintf(stderr,"gap_pool malloc fail!\n");
			exit(-1);
		}
    	gap_pool->size=length;
    	//fprintf(stderr,"gap_pool->size is %d\n",gap_pool->size);

    	int j=0;
    	fseek(file, 0L, SEEK_SET);
		//fprintf(stderr,"\n");
		while (fgets(buf, 1024, file)){ 
			iscomment = strchr(buf, '#');
			if (iscomment == buf) 
        	    continue;
			if (iscomment != NULL)  
        	    *iscomment = 0;
        	next=strtok_r(buf," ",&tmp);
        	while(next!=NULL){
            	num= strtol(next, (char**)NULL, 10);
            	gap_pool->table[j]=num;
            	j++;
            	//fprintf(stderr,"%d\n",num);
                next=strtok_r(NULL," ",&tmp);
        	}
		}

    	fclose(file);
		return 0;  
	}
	else if(flag==3){

		FILE *file;
		char buf[1024] = "";  
		char *iscomment,*next,*tmp;
    	int i=0,num,length=0;

		file=fopen(dist_file,"r");

		if (file == NULL)
    	{
    	    fprintf(stderr,"dist_file open fail!\n");
    	    exit(-1);
    	}
		int a=0;
		while (fgets(buf, 1024, file)){
			iscomment = strchr(buf, '#');
			if (iscomment == buf) 
   	        	continue;
			if (iscomment != NULL)  
    	        *iscomment = 0;
    	    next=strtok_r(buf," ",&tmp);
    	    while(next!=NULL){
    	        next=strtok_r(NULL," ",&tmp);
    	        length++;
    	    }
        
		}
    	delay_dist=(struct disttable*)malloc(sizeof(struct disttable)+length*sizeof(int64_t));
		if(delay_dist==NULL){
			fprintf(stderr,"delay_dist malloc fail!\n");
			exit(-1);
		}
    	delay_dist->size=length;
    	fprintf(stderr,"delay_dist->size is %d\n",delay_dist->size);

    	int j=0;
    	fseek(file, 0L, SEEK_SET);
		//fprintf(stderr,"\n");
		while (fgets(buf, 1024, file)){ 
			iscomment = strchr(buf, '#');
			if (iscomment == buf) 
        	    continue;
			if (iscomment != NULL)  
        	    *iscomment = 0;
        	next=strtok_r(buf," ",&tmp);
        	while(next!=NULL){
            	num= strtol(next, (char**)NULL, 10);
            	delay_dist->table[j]=num;
            	//fprintf(stderr,"%lld\n",delay_dist->table[j]);
                j++;
		next=strtok_r(NULL," ",&tmp);
        	}
		}
		delay_pool=(struct disttable*)malloc(sizeof(struct disttable)+delay_dist->size*sizeof(int64_t));
		delay_pool->size=delay_dist->size;
		dist_trans(delay_dist,delay_pool,DELAY_MEAN,DELAY_JITTER/4);/*
		if(!dist_trans(delay_dist,delay_pool,DELAY_MEAN,DELAY_JITTER/4)){
			fprintf(stderr," %s %dfail!!!!\n\n",__func__,__LINE__);
			exit(-1);
		}*/
    	fclose(file);
		return 0;
	}
	else{
		fprintf(stderr,"func %s invalid parameter,line %d",__func__,__LINE__);
		exit(-1);
	}
}

#define MAX_JUMBO_PKT_LEN  9600
#define MEMPOOL_CACHE_SIZE 256

static const char short_options[] =
	"p:"  /* portmask */
	"P"   /* promiscuous */
	"L"   /* enable long prefix match */
	"E"   /* enable exact match */
	;

#define CMD_LINE_OPT_CONFIG "config"
#define CMD_LINE_OPT_ETH_DEST "eth-dest"
#define CMD_LINE_OPT_DIST_TABLE "dist-table"
#define CMD_LINE_OPT_NO_NUMA "no-numa"
#define CMD_LINE_OPT_IPV6 "ipv6"
#define CMD_LINE_OPT_ENABLE_JUMBO "enable-jumbo"
#define CMD_LINE_OPT_HASH_ENTRY_NUM "hash-entry-num"
#define CMD_LINE_OPT_PARSE_PTYPE "parse-ptype"
#define CMD_LINE_OPT_PER_PORT_POOL "per-port-pool"

enum {
	/* long options mapped to a short option */

	/* first long only option value must be >= 256, so that we won't
	 * conflict with short options */
	CMD_LINE_OPT_MIN_NUM = 256,
	CMD_LINE_OPT_CONFIG_NUM,
	CMD_LINE_OPT_DIST_TABLE_NUM,
	CMD_LINE_OPT_NO_NUMA_NUM,
	CMD_LINE_OPT_IPV6_NUM,
	CMD_LINE_OPT_ENABLE_JUMBO_NUM,
	CMD_LINE_OPT_HASH_ENTRY_NUM_NUM,
	CMD_LINE_OPT_PARSE_PTYPE_NUM,
	CMD_LINE_OPT_PARSE_PER_PORT_POOL,
};

static const struct option lgopts[] = {
	{CMD_LINE_OPT_CONFIG, 1, 0, CMD_LINE_OPT_CONFIG_NUM},
	{CMD_LINE_OPT_DIST_TABLE, 1, 0, CMD_LINE_OPT_DIST_TABLE_NUM},
	{CMD_LINE_OPT_NO_NUMA, 0, 0, CMD_LINE_OPT_NO_NUMA_NUM},
	{CMD_LINE_OPT_IPV6, 0, 0, CMD_LINE_OPT_IPV6_NUM},
	{CMD_LINE_OPT_ENABLE_JUMBO, 0, 0, CMD_LINE_OPT_ENABLE_JUMBO_NUM},
	{CMD_LINE_OPT_HASH_ENTRY_NUM, 1, 0, CMD_LINE_OPT_HASH_ENTRY_NUM_NUM},
	{CMD_LINE_OPT_PARSE_PTYPE, 0, 0, CMD_LINE_OPT_PARSE_PTYPE_NUM},
	{CMD_LINE_OPT_PER_PORT_POOL, 0, 0, CMD_LINE_OPT_PARSE_PER_PORT_POOL},
	{NULL, 0, 0, 0}
};

/*
 * This expression is used to calculate the number of mbufs needed
 * depending on user input, taking  into account memory for rx and
 * tx hardware rings, cache per lcore and mtable per port per lcore.
 * RTE_MAX is used to ensure that NB_MBUF never goes below a minimum
 * value of 8192
 */
#define NB_MBUF(nports) RTE_MAX(	\
	(nports*nb_rx_queue*nb_rxd +		\
	nports*nb_lcores*MAX_PKT_BURST +	\
	nports*n_tx_queue*nb_txd +		\
	nb_lcores*MEMPOOL_CACHE_SIZE),		\
	(unsigned)8192)

/* Parse the argument given in the command line of the application */
static int
parse_args(int argc, char **argv)
{
	int opt, ret;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];

	argvopt = argv;

	/* Error or normal output strings. */
	while ((opt = getopt_long(argc, argvopt, short_options,
				lgopts, &option_index)) != EOF) {

		switch (opt) {
		/* portmask */
		case 'p':
			enabled_port_mask = parse_portmask(optarg);
			if (enabled_port_mask == 0) {
				fprintf(stderr, "Invalid portmask\n");
				print_usage(prgname);
				return -1;
			}
			break;

		case 'P':
			promiscuous_on = 1;
			break;

		case 'L':
			l2shaping_lpm_on = 1;
			break;

		/* long options */
		case CMD_LINE_OPT_CONFIG_NUM:
			ret = parse_config(optarg);
			if (ret) {
				fprintf(stderr, "Invalid config\n");
				print_usage(prgname);
				return -1;
			}
			break;

		case CMD_LINE_OPT_DIST_TABLE_NUM:
			ret=parse_dist_table(optarg,DIST_FLAG);
			if(ret){
				fprintf(stderr, "Invalid dist_table\n");
				return -1;
			}
			break;

		case CMD_LINE_OPT_NO_NUMA_NUM:
			numa_on = 0;
			break;

		case CMD_LINE_OPT_IPV6_NUM:
			ipv6 = 1;
			break;

		case CMD_LINE_OPT_ENABLE_JUMBO_NUM: {
			const struct option lenopts = {
				"max-pkt-len", required_argument, 0, 0
			};

			port_conf.rxmode.offloads |= DEV_RX_OFFLOAD_JUMBO_FRAME;
			port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MULTI_SEGS;

			/*
			 * if no max-pkt-len set, use the default
			 * value RTE_ETHER_MAX_LEN.
			 */
			if (getopt_long(argc, argvopt, "",
					&lenopts, &option_index) == 0) {
				ret = parse_max_pkt_len(optarg);
				if (ret < 64 || ret > MAX_JUMBO_PKT_LEN) {
					fprintf(stderr,
						"invalid maximum packet length\n");
					print_usage(prgname);
					return -1;
				}
				port_conf.rxmode.max_rx_pkt_len = ret;
			}
			break;
		}

		case CMD_LINE_OPT_HASH_ENTRY_NUM_NUM:
			ret = parse_hash_entry_number(optarg);
			if ((ret > 0) && (ret <= l2shaping_HASH_ENTRIES)) {
				hash_entry_number = ret;
			} else {
				fprintf(stderr, "invalid hash entry number\n");
				print_usage(prgname);
				return -1;
			}
			break;

		case CMD_LINE_OPT_PARSE_PTYPE_NUM:
			printf("soft parse-ptype is enabled\n");
			parse_ptype = 1;
			break;

		case CMD_LINE_OPT_PARSE_PER_PORT_POOL:
			printf("per port buffer pool is enabled\n");
			per_port_pool = 1;
			break;

		default:
			print_usage(prgname);
			return -1;
		}
	}

	l2shaping_lpm_on = 1;
	

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 1; /* reset getopt lib */
	return ret;
}

static void
print_ethaddr(const char *name, const struct rte_ether_addr *eth_addr)
{
	char buf[RTE_ETHER_ADDR_FMT_SIZE];
	rte_ether_format_addr(buf, RTE_ETHER_ADDR_FMT_SIZE, eth_addr);
	printf("%s%s", name, buf);
}

static int
init_mem(uint16_t portid, unsigned int nb_mbuf)
{
	struct lcore_conf *qconf;
	int socketid;
	unsigned lcore_id;
	char s[64];

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;

		if (numa_on)
			socketid = rte_lcore_to_socket_id(lcore_id);
		else
			socketid = 0;

		if (socketid >= NB_SOCKETS) {
			rte_exit(EXIT_FAILURE,
				"Socket %d of lcore %u is out of range %d\n",
				socketid, lcore_id, NB_SOCKETS);
		}

		if (pktmbuf_pool[portid][socketid] == NULL) {
			snprintf(s, sizeof(s), "mbuf_pool_%d:%d",
				 portid, socketid);
			pktmbuf_pool[portid][socketid] =
				rte_pktmbuf_pool_create(s, nb_mbuf,
					MEMPOOL_CACHE_SIZE, 0,
					RTE_MBUF_DEFAULT_BUF_SIZE, socketid);
			if (pktmbuf_pool[portid][socketid] == NULL)
				rte_exit(EXIT_FAILURE,
					"Cannot init mbuf pool on socket %d\n",
					socketid);
			else
				printf("Allocated mbuf pool on socket %d\n",
					socketid);

			/* Setup either LPM or EM(f.e Hash). But, only once per
			 * available socket.
			 */
			if (!lkp_per_socket[socketid]) {
				l2shaping_lkp.setup(socketid);
				lkp_per_socket[socketid] = 1;
			}
		}
		qconf = &lcore_conf[lcore_id];
		qconf->ipv4_lookup_struct =
			l2shaping_lkp.get_ipv4_lookup_struct(socketid);
		qconf->ipv6_lookup_struct =
			l2shaping_lkp.get_ipv6_lookup_struct(socketid);
	}
	return 0;
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_all_ports_link_status(uint32_t port_mask)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint16_t portid;
	uint8_t count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;
	int ret;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		if (force_quit)
			return;
		all_ports_up = 1;
		RTE_ETH_FOREACH_DEV(portid) {
			if (force_quit)
				return;
			if ((port_mask & (1 << portid)) == 0)
				continue;
			memset(&link, 0, sizeof(link));
			ret = rte_eth_link_get_nowait(portid, &link);
			if (ret < 0) {
				all_ports_up = 0;
				if (print_flag == 1)
					printf("Port %u link get failed: %s\n",
						portid, rte_strerror(-ret));
				continue;
			}
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf(
					"Port%d Link Up. Speed %u Mbps -%s\n",
						portid, link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n", portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == ETH_LINK_DOWN) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf("done\n");
		}
	}
}

static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		printf("\n\nSignal %d received, preparing to exit...\n",
				signum);
		force_quit = true;
	}
}

static int
prepare_ptype_parser(uint16_t portid, uint16_t queueid)
{
	if (parse_ptype) {
		printf("Port %d: softly parse packet type info\n", portid);
		if (rte_eth_add_rx_callback(portid, queueid,
					    l2shaping_lkp.cb_parse_ptype,
					    NULL))
			return 1;

		printf("Failed to add rx callback: port=%d\n", portid);
		return 0;
	}

	if (l2shaping_lkp.check_ptype(portid))
		return 1;

	printf("port %d cannot parse packet type, please add --%s\n",
	       portid, CMD_LINE_OPT_PARSE_PTYPE);
	return 0;
}


/*eidt*/
void ShowStack(void)
{
	int i;
	void *buffer[BACKTRACE_SIZE];
 
	int n = backtrace(buffer, BACKTRACE_SIZE);
	printf("[%s]:[%d] n = %d\n", __func__, __LINE__, n);
	char **symbols = backtrace_symbols(buffer, n);
	
	if(NULL == symbols){
		perror("backtrace symbols");
		exit(EXIT_FAILURE);
	}
	printf("[%s]:[%d]\n", __func__, __LINE__);
	for (i = 0; i < n; i++) {
		printf("%d: %s\n", i, symbols[i]);
	}
 
	free(symbols);
}

int nano_delay(long delay)
{
    struct timespec req, rem;
    long nano_delay = delay;
    int ret = 0;
    while(nano_delay > 0)
    {
            rem.tv_sec = 0;
            rem.tv_nsec = 0;
            req.tv_sec = 0;
            req.tv_nsec = nano_delay;
            if(ret = (nanosleep(&req, &rem) == -1))
            {
                printf("nanosleep failed.\n");                
            }
            nano_delay = rem.tv_nsec;
    }
    return ret;
}

static uint16_t
rate_control_to_client(uint16_t port, uint16_t qidx __rte_unused,
		struct rte_mbuf **pkts, uint16_t nb_pkts, void *_ __rte_unused)
{
	nano_delay(10*32);
	return nb_pkts;
}

static uint16_t
rate_control_to_server(uint16_t port, uint16_t qidx __rte_unused,
		struct rte_mbuf **pkts, uint16_t nb_pkts, void *_ __rte_unused)
{
	nano_delay(80*32);
	return nb_pkts;
}

int
main(int argc, char **argv)
{
	struct lcore_conf *qconf;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf *txconf;
	int ret;
	unsigned nb_ports;
	uint16_t queueid, portid;
	unsigned lcore_id;
	uint32_t n_tx_queue, nb_lcores;
	uint8_t nb_rx_queue, queue, socketid;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	argc -= ret;
	argv += ret;

	force_quit = false;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	
	//signal(SIGSEGV, sigsegv_handler);

	/* parse application arguments (after the EAL ones) */
	ret = parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid l2shaping parameters\n");

	if (check_lcore_params() < 0)
		rte_exit(EXIT_FAILURE, "check_lcore_params failed\n");

	ret = init_lcore_rx_queues();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_lcore_rx_queues failed\n");

	nb_ports = rte_eth_dev_count_avail();

	if (check_port_config() < 0)
		rte_exit(EXIT_FAILURE, "check_port_config failed\n");

	nb_lcores = rte_lcore_count();

	/* Setup function pointers for lookup method. */
	setup_l2shaping_lookup_tables();

	/*edit */
	produce_packs_pool=rte_pktmbuf_pool_create("produce_packs_pool", 2097152, 0, 0,RTE_MBUF_DEFAULT_BUF_SIZE, SOCKET_ID_ANY);
	init_void_packets();
	c2s_send_queue = rte_ring_create("Buffer_Ring0", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_send_queue_highpri= rte_ring_create("Buffer_Ring01", RING_SIZE, SOCKET_ID_ANY,0);
	s2c_send_queue = rte_ring_create("Buffer_Ring1", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_receive_queue= rte_ring_create("Buffer_Ring2", RING_SIZE, SOCKET_ID_ANY,0);
	s2c_receive_queue= rte_ring_create("Buffer_Ring3", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_drop_process_queue= rte_ring_create("Buffer_Ring4", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_delay_process_queue= rte_ring_create("Buffer_Ring5", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_dump_process_queue= rte_ring_create("Buffer_Ring6", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_reorder_process_queue= rte_ring_create("Buffer_Ring7", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_reframe_queue= rte_ring_create("Buffer_Ring  8", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_reframe_queue0= rte_ring_create("Buffer_Ring  80", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_reframe_queue1= rte_ring_create("Buffer_Ring  81", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_reframe_queue2= rte_ring_create("Buffer_Ring  82", RING_SIZE, SOCKET_ID_ANY,0);
	c2s_reframe_queue3= rte_ring_create("Buffer_Ring  83", RING_SIZE, SOCKET_ID_ANY,0);
	/*edit over*/


	/* initialize all ports */
	RTE_ETH_FOREACH_DEV(portid) {
		struct rte_eth_conf local_port_conf = port_conf;

		/* skip ports that are not enabled */
		if ((enabled_port_mask & (1 << portid)) == 0) {
			printf("\nSkipping disabled port %d\n", portid);
			continue;
		}

		/* init port */
		printf("Initializing port %d ... ", portid );
		fflush(stdout);

		nb_rx_queue = get_port_n_rx_queues(portid);
		n_tx_queue = nb_lcores;
		if (n_tx_queue > MAX_TX_QUEUE_PER_PORT)
			n_tx_queue = MAX_TX_QUEUE_PER_PORT;
		printf("Creating queues: nb_rxq=%d nb_txq=%u... ",
			nb_rx_queue, (unsigned)n_tx_queue );

		ret = rte_eth_dev_info_get(portid, &dev_info);
		if (ret != 0)
			rte_exit(EXIT_FAILURE,
				"Error during getting device (port %u) info: %s\n",
				portid, strerror(-ret));

		if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
			local_port_conf.txmode.offloads |=
				DEV_TX_OFFLOAD_MBUF_FAST_FREE;

		local_port_conf.rx_adv_conf.rss_conf.rss_hf &=
			dev_info.flow_type_rss_offloads;
		if (local_port_conf.rx_adv_conf.rss_conf.rss_hf !=
				port_conf.rx_adv_conf.rss_conf.rss_hf) {
			printf("Port %u modified RSS hash function based on hardware support,"
				"requested:%#"PRIx64" configured:%#"PRIx64"\n",
				portid,
				port_conf.rx_adv_conf.rss_conf.rss_hf,
				local_port_conf.rx_adv_conf.rss_conf.rss_hf);
		}

		ret = rte_eth_dev_configure(portid, nb_rx_queue,
					(uint16_t)n_tx_queue, &local_port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				"Cannot configure device: err=%d, port=%d\n",
				ret, portid);

		ret = rte_eth_dev_adjust_nb_rx_tx_desc(portid, &nb_rxd,
						       &nb_txd);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot adjust number of descriptors: err=%d, "
				 "port=%d\n", ret, portid);

		ret = rte_eth_macaddr_get(portid, &ports_eth_addr[portid]);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				 "Cannot get MAC address: err=%d, port=%d\n",
				 ret, portid);

		print_ethaddr(" Address:", &ports_eth_addr[portid]);
		printf(", ");

		/* init memory */
		if (!per_port_pool) {
			/* portid = 0; this is *not* signifying the first port,
			 * rather, it signifies that portid is ignored.
			 */
			//ret = init_mem(0, NB_MBUF(nb_ports));
			ret = init_mem(0, 524288);
		} else {
			//ret = init_mem(portid, NB_MBUF(1));
			ret = init_mem(0, 524288);
		}
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "init_mem failed\n");

		/* init one TX queue per couple (lcore,port) */
		queueid = 0;
		for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
			if (rte_lcore_is_enabled(lcore_id) == 0)
				continue;

			if (numa_on)
				socketid =
				(uint8_t)rte_lcore_to_socket_id(lcore_id);
			else
				socketid = 0;

			printf("txq=%u,%d,%d ", lcore_id, queueid, socketid);
			fflush(stdout);

			txconf = &dev_info.default_txconf;
			txconf->offloads = local_port_conf.txmode.offloads;
			ret = rte_eth_tx_queue_setup(portid, queueid, nb_txd,
						     socketid, txconf);
			if (ret < 0)
				rte_exit(EXIT_FAILURE,
					"rte_eth_tx_queue_setup: err=%d, "
					"port=%d\n", ret, portid);

			qconf = &lcore_conf[lcore_id];
			qconf->tx_queue_id[portid] = queueid;
			queueid++;

			qconf->tx_port_id[qconf->n_tx_port] = portid;
			qconf->n_tx_port++;
		}
		printf("\n");
	}

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;
		qconf = &lcore_conf[lcore_id];
		printf("\nInitializing rx queues on lcore %u ... ", lcore_id );
		fflush(stdout);
		/* init RX queues */
		for(queue = 0; queue < qconf->n_rx_queue; ++queue) {
			struct rte_eth_rxconf rxq_conf;

			portid = qconf->rx_queue_list[queue].port_id;
			queueid = qconf->rx_queue_list[queue].queue_id;

			if (numa_on)
				socketid =
				(uint8_t)rte_lcore_to_socket_id(lcore_id);
			else
				socketid = 0;

			printf("rxq=%d,%d,%d ", portid, queueid, socketid);
			fflush(stdout);

			ret = rte_eth_dev_info_get(portid, &dev_info);
			if (ret != 0)
				rte_exit(EXIT_FAILURE,
					"Error during getting device (port %u) info: %s\n",
					portid, strerror(-ret));

			rxq_conf = dev_info.default_rxconf;
			rxq_conf.offloads = port_conf.rxmode.offloads;
			if (!per_port_pool)
				ret = rte_eth_rx_queue_setup(portid, queueid,
						nb_rxd, socketid,
						&rxq_conf,
						pktmbuf_pool[0][socketid]);
			else
				ret = rte_eth_rx_queue_setup(portid, queueid,
						nb_rxd, socketid,
						&rxq_conf,
						pktmbuf_pool[portid][socketid]);
			if (ret < 0)
				rte_exit(EXIT_FAILURE,
				"rte_eth_rx_queue_setup: err=%d, port=%d\n",
				ret, portid);
		}
	}

	printf("\n");

	/* start ports */
	RTE_ETH_FOREACH_DEV(portid) {
		if ((enabled_port_mask & (1 << portid)) == 0) {
			continue;
		}
		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE,
				"rte_eth_dev_start: err=%d, port=%d\n",
				ret, portid);

		/*
		 * If enabled, put device in promiscuous mode.
		 * This allows IO forwarding mode to forward packets
		 * to itself through 2 cross-connected  ports of the
		 * target machine.
		 */
		if (promiscuous_on) {
			ret = rte_eth_promiscuous_enable(portid);
			if (ret != 0)
				rte_exit(EXIT_FAILURE,
					"rte_eth_promiscuous_enable: err=%s, port=%u\n",
					rte_strerror(-ret), portid);
		}
	}

	printf("\n");

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;
		qconf = &lcore_conf[lcore_id];
		for (queue = 0; queue < qconf->n_rx_queue; ++queue) {
			portid = qconf->rx_queue_list[queue].port_id;
			queueid = qconf->rx_queue_list[queue].queue_id;
			if (prepare_ptype_parser(portid, queueid) == 0)
				rte_exit(EXIT_FAILURE, "ptype check fails\n");
		}
	}


	check_all_ports_link_status(enabled_port_mask);

	/*edit*/
	//rte_eth_add_tx_callback(PORT_TO_CLIENT, QUEUE_TO_CLIENT_WITH_PAYLOAD, rate_control_to_client, NULL);
	//rte_eth_add_tx_callback(PORT_TO_SERVER, QUEUE_TO_SERVER_WITHOUT_PAYLOAD, rate_control_to_server, NULL);
	void_pack_pool=rte_pktmbuf_pool_create("void_pack_pool", 128, 0, 0,RTE_MBUF_DEFAULT_BUF_SIZE, SOCKET_ID_ANY);
	//make_void_packs();
	//make_a_void_pack();
	/*edit over */

	ret = 0;
	/* launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(lpm_main_loop, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0) {
			ret = -1;
			break;
		}
	}

	/* stop ports */
	RTE_ETH_FOREACH_DEV(portid) {
		if ((enabled_port_mask & (1 << portid)) == 0)
			continue;
		printf("Closing port %d...", portid);
		rte_eth_dev_stop(portid);
		rte_eth_dev_close(portid);
		printf(" Done\n");
	}
	printf("Bye...\n");
	return ret;
}

void init_void_packets(){
    int i,j;

    for(i=0;i<MAX_VOID_PKT_LEN;i++){
		for(j=0;j<MAX_VOID_BURST_SIZE;j++){
        	void_packs[i][j]=NULL;
    	}
	}
}
