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

#include <rte_errno.h>
#include <rte_malloc.h>

#include "port-eth.h"

static struct rte_port_ops rte_port_eth_ops;

struct rte_port_eth *
rte_port_eth_create(struct rte_port_eth_params *conf, int socket_id)
{
	struct rte_port_eth *port;
	uint8_t port_id = conf->port_id;
	int ret;

	port = rte_zmalloc_socket("PORT", sizeof(*port), CACHE_LINE_SIZE,
				  socket_id);
        if (port == NULL) {
                RTE_LOG(ERR, PORT, "Cannot allocate eth port\n");
		return NULL;
	}

	port->port_id = port_id;
	port->rte_port.type = RTE_PORT_TYPE_ETH;
	port->rte_port.ops = rte_port_eth_ops;
	port->mempool = conf->mempool;

	ret = rte_eth_dev_configure(port_id, 1, 1, &conf->eth_conf);
	if (ret < 0) {
		RTE_LOG(ERR, PORT, "Cannot config eth dev: %s\n",
			rte_strerror(-ret));
		rte_free(port);
		return NULL;
	}

	ret = rte_eth_rx_queue_setup(port_id, 0, conf->nb_rx_desc, socket_id,
				     &conf->rx_conf, conf->mempool);
	if (ret < 0) {
		RTE_LOG(ERR, PORT, "Cannot setup rx queue: %s\n",
			rte_strerror(-ret));
		rte_free(port);
		return NULL;
	}

	ret = rte_eth_tx_queue_setup(port_id, 0, conf->nb_tx_desc, socket_id,
				     &conf->tx_conf);
	if (ret < 0) {
		RTE_LOG(ERR, PORT, "Cannot setup tx queue: %s\n",
			rte_strerror(-ret));
		rte_free(port);
		return NULL;
	}

	ret = rte_eth_dev_start(port_id);
	if (ret < 0) {
		RTE_LOG(ERR, PORT, "Cannot start eth dev: %s\n",
			rte_strerror(-ret));
		rte_free(port);
		return NULL;
	}

	rte_eth_promiscuous_enable(port_id);

	rte_eth_dev_info_get(port_id, &port->eth_dev_info);

	return port;
}

int
rte_port_eth_rx_burst(struct rte_port *rte_port,
		      struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct rte_port_eth *p;
	int rx;

	RTE_VERIFY(rte_port->type == RTE_PORT_TYPE_ETH);

	p = container_of(rte_port, struct rte_port_eth, rte_port);

	rx = rte_eth_rx_burst(p->port_id, 0, pkts, n_pkts);
	if (unlikely(rx > n_pkts)) {
                RTE_LOG(ERR, PORT, "Failed to rx eth burst\n");
		return rx;
	}

	p->rte_port.stats.rx_packets += rx;

	return rx;
}

int
rte_port_eth_tx_burst(struct rte_port *rte_port,
		      struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct rte_port_eth *p;
	int tx;

	RTE_VERIFY(rte_port->type == RTE_PORT_TYPE_ETH);

	p = container_of(rte_port, struct rte_port_eth, rte_port);

	tx = rte_eth_tx_burst(p->port_id, 0, pkts, n_pkts);

	p->rte_port.stats.tx_packets += tx;

	if (unlikely(tx < n_pkts)) {
		for (; tx < n_pkts; tx++) {
			rte_pktmbuf_free(pkts[tx]);
			p->rte_port.stats.tx_dropped += 1;
		}
        }
	return tx;
}

static struct rte_port_ops rte_port_eth_ops = {
	.rx_burst = rte_port_eth_rx_burst,
	.tx_burst = rte_port_eth_tx_burst
};
