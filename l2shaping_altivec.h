/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2016 Intel Corporation.
 * Copyright(c) 2017 IBM Corporation.
 * All rights reserved.
 */

#ifndef _l2shaping_ALTIVEC_H_
#define _l2shaping_ALTIVEC_H_

#include "l2shaping.h"
#include "l2shaping_common.h"

/*
 * Group consecutive packets with the same destination port in bursts of 4.
 * Suppose we have array of destination ports:
 * dst_port[] = {a, b, c, d,, e, ... }
 * dp1 should contain: <a, b, c, d>, dp2: <b, c, d, e>.
 * We doing 4 comparisons at once and the result is 4 bit mask.
 * This mask is used as an index into prebuild array of pnum values.
 */
static inline uint16_t *
port_groupx4(uint16_t pn[FWDSTEP + 1], uint16_t *lp, vector unsigned short dp1,
	vector unsigned short dp2)
{
	union {
		uint16_t u16[FWDSTEP + 1];
		uint64_t u64;
	} *pnum = (void *)pn;

	int32_t v;

	v = vec_any_eq(dp1, dp2);


	/* update last port counter. */
	lp[0] += gptbl[v].lpv;

	/* if dest port value has changed. */
	if (v != GRPMSK) {
		pnum->u64 = gptbl[v].pnum;
		pnum->u16[FWDSTEP] = 1;
		lp = pnum->u16 + gptbl[v].idx;
	}

	return lp;
}

/**
 * Send packets burst from pkts_burst to the ports in dst_port array
 */
static __rte_always_inline void
send_packets_multi(struct lcore_conf *qconf, struct rte_mbuf **pkts_burst,
		uint16_t dst_port[MAX_PKT_BURST], int nb_rx)
{
	int32_t k;
	int j = 0;
	uint16_t dlp;
	uint16_t *lp;
	uint16_t pnum[MAX_PKT_BURST + 1];

	/*
	 * Finish packet processing and group consecutive
	 * packets with the same destination port.
	 */
	k = RTE_ALIGN_FLOOR(nb_rx, FWDSTEP);
	if (k != 0) {
		vector unsigned short dp1, dp2;

		lp = pnum;
		lp[0] = 1;

		

		/* dp1: <d[0], d[1], d[2], d[3], ... > */
		dp1 = *(vector unsigned short *)dst_port;

		for (j = FWDSTEP; j != k; j += FWDSTEP) {
			

			/*
			 * dp2:
			 * <d[j-3], d[j-2], d[j-1], d[j], ... >
			 */
			dp2 = *((vector unsigned short *)
					&dst_port[j - FWDSTEP + 1]);
			lp  = port_groupx4(&pnum[j - FWDSTEP], lp, dp1, dp2);

			/*
			 * dp1:
			 * <d[j], d[j+1], d[j+2], d[j+3], ... >
			 */
			dp1 = vec_sro(dp2, (vector unsigned char) {
				0, 0, 0, 0, 0, 0, 0, 0,
				0, 0, 0, (FWDSTEP - 1) * sizeof(dst_port[0])});
		}

		/*
		 * dp2: <d[j-3], d[j-2], d[j-1], d[j-1], ... >
		 */
		dp2 = vec_perm(dp1, (vector unsigned short){},
				(vector unsigned char){0xf9});
		lp  = port_groupx4(&pnum[j - FWDSTEP], lp, dp1, dp2);

		/*
		 * remove values added by the last repeated
		 * dst port.
		 */
		lp[0]--;
		dlp = dst_port[j - 1];
	} else {
		/* set dlp and lp to the never used values. */
		dlp = BAD_PORT - 1;
		lp = pnum + MAX_PKT_BURST;
	}

	/* Process up to last 3 packets one by one. */
	switch (nb_rx % FWDSTEP) {
	case 3:
		GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
		j++;
		/* fall-through */
	case 2:
		GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
		j++;
		/* fall-through */
	case 1:
		GROUP_PORT_STEP(dlp, dst_port, lp, pnum, j);
		j++;
	}

	/*
	 * Send packets out, through destination port.
	 * Consecutive packets with the same destination port
	 * are already grouped together.
	 * If destination port for the packet equals BAD_PORT,
	 * then free the packet without sending it out.
	 */
	for (j = 0; j < nb_rx; j += k) {

		int32_t m;
		uint16_t pn;

		pn = dst_port[j];
		k = pnum[j];

		if (likely(pn != BAD_PORT))
			send_packetsx4(qconf, pn, pkts_burst + j, k);
		else{
			fprintf(stderr,"send_packets_multi free packet\n");
			for (m = j; m != j + k; m++)
				rte_pktmbuf_free(pkts_burst[m]);
		}
	}
}

#endif /* _l2shaping_ALTIVEC_H_ */
