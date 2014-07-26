/*
 *   BSD LICENSE
 *
 *   Copyright(c) 2014 Midokura SARL.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Midokura SARL nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <lwip/mem.h>
#include <lwip/netif.h>
#include <netif/etharp.h>

#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_memcpy.h>

#include "kniif.h"
#include "mempool.h"

struct kniif *
kniif_alloc(int socket_id)
{
	struct kniif *kniif;

	kniif = rte_zmalloc_socket("KNIIF", sizeof(kniif), CACHE_LINE_SIZE,
				   socket_id);
	return kniif;
}

err_t
kniif_init(struct kniif *kniif, struct rte_port_kni_params *params,
	   int socket_id, struct net_port *net_port)
{
	kniif->rte_port_type = RTE_PORT_TYPE_KNI;

	kniif->kni_port = rte_port_kni_create(params, socket_id, net_port);
	if (!kniif->kni_port)
		return ERR_MEM;

	memset(&kniif->netif, 0, sizeof(kniif->netif));

	net_port->netif = &kniif->netif;
	RTE_VERIFY(net_port->rte_port == &kniif->kni_port->rte_port);

	return ERR_OK;
}

err_t
kniif_input(struct kniif *kniif, struct rte_mbuf *m)
{
	int len = rte_pktmbuf_pkt_len(m);
	char *dat = rte_pktmbuf_mtod(m, char *);
	struct pbuf *p, *q;

	RTE_VERIFY(kniif->rte_port_type == RTE_PORT_TYPE_KNI);

	p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	if (p == 0) {
		kniif->kni_port->rte_port.stats.rx_dropped += 1;
		return ERR_OK;
	}

	for(q = p; q != NULL; q = q->next) {
		rte_memcpy(q->payload, dat, q->len);
		dat += q->len;
	}

	rte_pktmbuf_free(m);

	return kniif->netif.input(p, &kniif->netif);
}

static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
	struct kniif *kniif = (struct kniif *)netif->state;
	struct rte_port_kni *kni_port;
	struct rte_mbuf *m;
	struct pbuf *q;

	RTE_VERIFY(kniif->rte_port_type == RTE_PORT_TYPE_KNI);

	kni_port = kniif->kni_port;

	m = rte_pktmbuf_alloc(pktmbuf_pool);
	if (m == NULL)
		return ERR_MEM;

	for(q = p; q != NULL; q = q->next) {
		char *data = rte_pktmbuf_append(m, q->len);
		if (data == NULL) {
			rte_pktmbuf_free(m);
			return ERR_MEM;
		}
		rte_memcpy(data, q->payload, q->len);
	}

	rte_port_kni_tx_burst(&kni_port->rte_port, &m, 1);

	return ERR_OK;
}

err_t
kniif_added_cb(struct netif *netif)
{
	struct kniif *kniif = (struct kniif *)netif->state;

	RTE_VERIFY(kniif->rte_port_type == RTE_PORT_TYPE_KNI);

	netif->name[0] = 'k';
	netif->name[1] = 'n';
	netif->output = etharp_output;
	netif->linkoutput = low_level_output;
	netif->mtu = 1500;
	eth_random_addr(netif->hwaddr);
	netif->hwaddr_len = 6;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
	return ERR_OK;
}
