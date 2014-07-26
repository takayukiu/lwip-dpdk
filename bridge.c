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

#include <rte_debug.h>

#include "bridge.h"
#include "plugif.h"
#include "mempool.h"

struct bridge BR0;

int
bridge_add_port(struct bridge *bridge, struct net_port *net_port)
{
	RTE_VERIFY(net_port->rte_port_type == RTE_PORT_TYPE_PLUG ||
		   !net_port->netif);

	if (bridge->nr_ports >= BRIDGE_PORT_MAX)
		return -1;

	bridge->ports[bridge->nr_ports] = (struct bridge_port) {
		.port_id = bridge->nr_ports,
		.bridge = bridge,
		.net_port = net_port,
	};
	net_port->bridge_port = &bridge->ports[bridge->nr_ports];

	bridge->nr_ports++;

	return 0;
}

int
bridge_add_plug(struct bridge *bridge, struct net_port *net_port,
		struct plugif *plugif)
{
	int ret;

	ret = bridge_add_port(bridge, net_port);
	if (ret != 0)
		return ret;

	bridge->plug.plugif = plugif;

	return  0;
}

static int
bridge_flood(struct bridge *bridge, struct bridge_port *ingress,
	     struct rte_mbuf **pkts, int n_pkts)
{
	struct net_port *net_port;
	struct rte_port *rte_port;
	struct rte_mbuf *pkts_clone[n_pkts];
	int egress;
	int i, j;

	if (bridge->nr_ports <= 1) {
		for (j = 0; j < n_pkts; j++)
			rte_pktmbuf_free(pkts[j]);
		return 0;
	}

	for (i = 1; i < bridge->nr_ports; i++) {
		egress = (ingress->port_id + i) % bridge->nr_ports;
		net_port = bridge->ports[egress].net_port;
		rte_port = net_port->rte_port;

		if (i >= (bridge->nr_ports - 1)) {
			rte_port->ops.tx_burst(rte_port, pkts, n_pkts);
			break;
		}

		for (j = 0; j < n_pkts; j++)
			pkts_clone[j] = rte_pktmbuf_clone(pkts[j], pktmbuf_pool);

		rte_port->ops.tx_burst(rte_port, pkts_clone, n_pkts);
	}

	return 0;
}

int
bridge_input(struct bridge *bridge, struct bridge_port *ingress,
	     struct rte_mbuf **pkts, int n_pkts)
{
	return bridge_flood(bridge, ingress, pkts, n_pkts);
}

int
bridge_rx_burst(struct rte_port_plug *plug_port,
		struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct bridge *bridge = (struct bridge *)plug_port->private_data;
	struct bridge_port *bridge_port = bridge->plug.net_port.bridge_port;

	return bridge_input(bridge, bridge_port, pkts, n_pkts);
}

int
bridge_tx_burst(struct rte_port_plug *plug_port,
		struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct bridge *bridge = (struct bridge *)plug_port->private_data;
	struct plugif *plugif = bridge->plug.plugif;
	uint32_t i;

	for (i = 0; i < n_pkts; i++)
		plugif_input(plugif, pkts[i]);

	return n_pkts;
}
