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

#include <rte_malloc.h>

#include "port-kni.h"

static struct rte_port_ops rte_port_kni_ops;

struct rte_port_kni *
rte_port_kni_create(struct rte_port_kni_params *conf,
		    int socket_id,
		    struct net_port *net_port)

{
	struct rte_port_kni *port;
        struct rte_kni_conf kni_conf;
        struct rte_kni_ops kni_ops;

	port = rte_zmalloc_socket("PORT", sizeof(*port), CACHE_LINE_SIZE,
				  socket_id);
        if (port == NULL) {
                RTE_LOG(ERR, PORT, "Cannot allocate kni port\n");
		return NULL;
	}

	memset(&kni_conf, 0, sizeof(kni_conf));
	snprintf(kni_conf.name, RTE_KNI_NAMESIZE, "%s", conf->name);
	kni_conf.mbuf_size = conf->mbuf_size;

	memset(&kni_ops, 0, sizeof(kni_ops));

	port->kni = rte_kni_alloc(conf->mempool, &kni_conf, &kni_ops);
	if (port->kni == NULL) {
                RTE_LOG(ERR, PORT, "Cannot allocate kni instance\n");
		rte_free(port);
		return NULL;
	}

	port->rte_port.type = RTE_PORT_TYPE_KNI;
	port->rte_port.ops = rte_port_kni_ops;

	net_port->rte_port = &port->rte_port;

	return port;
}

int
rte_port_kni_rx_burst(struct rte_port *rte_port,
		      struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct rte_port_kni *p;
	int rx;

	RTE_VERIFY(rte_port->type == RTE_PORT_TYPE_KNI);

	p = container_of(rte_port, struct rte_port_kni, rte_port);

	rx = rte_kni_rx_burst(p->kni, pkts, n_pkts);
	if (unlikely(rx > n_pkts)) {
                RTE_LOG(ERR, PORT, "Failed to rx kni burst\n");
		return rx;
	}

	p->rte_port.stats.rx_packets += rx;

	rte_kni_handle_request(p->kni);

	return rx;
}

/* buffer ownership and responsivity [tx_burst]
 *   mbuf: transfer the ownership of all mbuf sent successfully to
 *         the underlying device, otherwise free all here
 */
int
rte_port_kni_tx_burst(struct rte_port *rte_port,
		      struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct rte_port_kni *p;
	int tx;

	RTE_VERIFY(rte_port->type == RTE_PORT_TYPE_KNI);

	p = container_of(rte_port, struct rte_port_kni, rte_port);

	tx = rte_kni_tx_burst(p->kni, pkts, n_pkts);
	p->rte_port.stats.tx_packets += tx;

	if (unlikely(tx < n_pkts)) {
		for (; tx < n_pkts; tx++) {
			rte_pktmbuf_free(pkts[tx]);
			p->rte_port.stats.tx_dropped += 1;
		}
        }
	return tx;
}

static struct rte_port_ops rte_port_kni_ops = {
	.rx_burst = rte_port_kni_rx_burst,
	.tx_burst = rte_port_kni_tx_burst
};
