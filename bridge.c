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

#include <rte_byteorder.h>
#include <rte_debug.h>
#include <rte_memcpy.h>

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

int
bridge_add_vxlan(struct bridge *bridge, struct vxlan_peer *peer)
{
	struct udp_pcb *pcb;

	if (bridge->vxlan.nr_peers >= VXLAN_DST_MAX)
		return -1;

	pcb = udp_new();
	if (!pcb)
		return -1;

	if (udp_connect(pcb, &peer->ip_addr, peer->port) != ERR_OK) {
		udp_remove(pcb);
		return -1;
	}

	bridge->vxlan.peers[bridge->vxlan.nr_peers++] = pcb;

	return 0;
}

static void
vxlan_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
	   ip_addr_t *addr, u16_t port)
{
	struct bridge *bridge = (struct bridge *)arg;
	struct rte_port *rte_port = bridge->plug.net_port.rte_port;
	struct rte_port_plug *plug =
		container_of(rte_port, struct rte_port_plug, rte_port);
	struct rte_mbuf *m;
	struct pbuf *q;

	m = rte_pktmbuf_alloc(pktmbuf_pool);
	if (m == NULL)
		goto free_pbuf;

	if (pbuf_header(p, -(int)(sizeof(struct vxlanhdr))) != 0)
		goto free_mbuf;

	for(q = p; q != NULL; q = q->next) {
		char *data = rte_pktmbuf_append(m, q->len);
		if (data == NULL)
			goto free_mbuf;
		rte_memcpy(data, q->payload, q->len);
	}

	bridge_rx_burst(plug, &m, 1);

	goto free_pbuf;

free_mbuf:
	rte_pktmbuf_free(m);

free_pbuf:
	pbuf_free(p);
}

int
bridge_bind_vxlan(struct bridge *bridge)
{
	struct udp_pcb *pcb;
	err_t ret;

	pcb = udp_new();
	if (!pcb)
		return ERR_MEM;

	ret = udp_bind(pcb, IP_ADDR_ANY, VXLAN_DST_PORT);
	if (ret != ERR_OK) {
		udp_remove(pcb);
		return ret;
	}

	udp_recv(pcb, vxlan_recv, bridge);

	bridge->vxlan.local = pcb;

	return ERR_OK;
}

static int
bridge_flood(struct bridge *bridge, struct bridge_port *ingress,
	     struct rte_mbuf **pkts, int n_pkts)
{
	struct net_port *net_port;
	struct rte_port *rte_port;
	struct rte_mbuf *pkts_clone[n_pkts], *clone;
	int egress;
	int i, j, k;

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

		for (j = 0; j < n_pkts; j++) {
			clone = rte_pktmbuf_clone(pkts[j], pktmbuf_pool);
			if (!clone) {
				for (k = 0; k < j; k++)
					rte_pktmbuf_free(pkts_clone[k]);
				return -1;
			}
			pkts_clone[j] = clone;
		}

		rte_port->ops.tx_burst(rte_port, pkts_clone, n_pkts);
	}
	return 0;
}

int
bridge_input(struct bridge *bridge, struct bridge_port *ingress,
	     struct rte_mbuf **pkts, int n_pkts)
{
	if (bridge_flood(bridge, ingress, pkts, n_pkts) != 0)
		return 0;

	return n_pkts;
}

int
bridge_rx_burst(struct rte_port_plug *plug_port,
		struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct bridge *bridge = (struct bridge *)plug_port->private_data;
	struct bridge_port *bridge_port = bridge->plug.net_port.bridge_port;

	return bridge_input(bridge, bridge_port, pkts, n_pkts);
}

/* buffer ownership and responsivity [tx_burst]
 *   mbuf: transfer the ownership of all mbuf sent successfully to
 *         the underlying device, otherwise free all here
 */
int
bridge_tx_burst(struct rte_port_plug *plug_port,
		struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct bridge *bridge = (struct bridge *)plug_port->private_data;
	struct plugif *plugif = bridge->plug.plugif;
	uint32_t i, j;

	for (i = 0; i < n_pkts; i++) {
		if (plugif_input(plugif, pkts[i]) != ERR_OK) {
			for (j = i + 1; j < n_pkts; j++)
				rte_pktmbuf_free(pkts[j]);
			plug_port->rte_port.stats.rx_dropped += n_pkts - i;
			return i;
		}
	}
	return n_pkts;
}

/* buffer ownership and responsivity [udp_send]
 *   pbuf: free a newly allocated pbuf here
 *   mbuf: free all here
 */
static err_t
bridge_tx_vxlan(struct bridge *bridge, struct rte_mbuf *m)
{
	struct vxlanhdr *header;
	struct pbuf *p, *q;
	err_t ret;
	int len, i;
	char *dat;

	header = (struct vxlanhdr *)rte_pktmbuf_prepend(m, sizeof(*header));
	if (!header)
		return ERR_MEM;

	header->vx_flags = rte_cpu_to_be_32(0x08000000);
	header->vx_vni =  rte_cpu_to_be_32(0x100);

	len = rte_pktmbuf_pkt_len(m);
	dat = rte_pktmbuf_mtod(m, char *);

	for (i = 0; i < bridge->vxlan.nr_peers; i++) {
		p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
		if (p == 0) {
			rte_pktmbuf_free(m);
			return ERR_MEM;
		}

		for(q = p; q != NULL; q = q->next) {
			rte_memcpy(q->payload, dat, q->len);
			dat += q->len;
		}
		ret = udp_send(bridge->vxlan.peers[i], p);
		pbuf_free(p);
		if (ret != ERR_OK) {
			rte_pktmbuf_free(m);
			return ret;
		}
	}

	rte_pktmbuf_free(m);
	return ERR_OK;
}

/* buffer ownership and responsivity [tx_burst]
 *   mbuf: transfer the ownership of all mbuf sent successfully to
 *         the underlying device, otherwise free all here
 */
int
bridge_tx_vxlan_burst(struct rte_port_plug *plug_port,
		      struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct bridge *bridge = (struct bridge *)plug_port->private_data;
	uint32_t i, j;

	for (i = 0; i < n_pkts; i++) {
		if (bridge_tx_vxlan(bridge, pkts[i]) != ERR_OK) {
			for (j = i + 1; j < n_pkts; j++)
				rte_pktmbuf_free(pkts[j]);
			plug_port->rte_port.stats.rx_dropped += n_pkts - i;
			return i;
		}
	}
	return n_pkts;
}
