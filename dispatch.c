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

#include <rte_mbuf.h>

#include <lwip/timers.h>

#include "bridge.h"
#include "dispatch.h"
#include "ethif.h"
#include "kniif.h"
#include "main.h"

/*
 * From lwip/src/include/lwip/opt.h:
 *
 * LWIP_HOOK_IP4_INPUT(pbuf, input_netif):
 * - called from ip_input() (IPv4)
 * - pbuf: received struct pbuf passed to ip_input()
 * - input_netif: struct netif on which the packet has been received
 * Return values:
 * - 0: Hook has not consumed the packet, packet is processed as normal
 * - != 0: Hook has consumed the packet.
 * If the hook consumed the packet, 'pbuf' is in the responsibility of
 * the hook (i.e. free it when done).
 */
int
ip_input_hook(struct pbuf *p, struct netif *inp)
{
	return 0; /* packet is processed as normal */
}

static int
dispatch_to_ethif(struct netif *netif,
		  struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct ethif *ethif = (struct ethif *)netif->state;
	uint32_t i;

	RTE_VERIFY(ethif->rte_port_type == RTE_PORT_TYPE_ETH);

	for (i = 0; i < n_pkts; i++)
		ethif_input(ethif, pkts[i]);

	return n_pkts;
}

static int
dispatch_to_kniif(struct netif *netif,
		  struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct kniif *kniif = (struct kniif *)netif->state;
	uint32_t i;

	RTE_VERIFY(kniif->rte_port_type == RTE_PORT_TYPE_KNI);

	for (i = 0; i < n_pkts; i++)
		kniif_input(kniif, pkts[i]);

	return n_pkts;
}

static int
dispatch_to_bridge(struct net_port *source_port,
		   struct rte_mbuf **pkts, uint32_t n_pkts)
{
	struct bridge_port *bridge_port = source_port->bridge_port;
	struct bridge *bridge = bridge_port->bridge;

	return bridge_input(bridge, bridge_port, pkts, n_pkts);
}

static int
dispatch(struct net_port *ports, int nr_ports,
	 struct rte_mbuf **pkts, int pkt_burst_sz)
{
	struct net_port *net_port;
	struct rte_port *rte_port;
	struct netif *netif;
	int i;
	uint32_t n_pkts;

	/*
	 * From lwip/src/core/timers.c:
	 *
	 * "Must be called periodically from your main loop."
	 */
	sys_check_timeouts();

	for (i = 0; i < nr_ports; i++) {
		net_port = &ports[i];
		rte_port = net_port->rte_port;

		n_pkts = rte_port->ops.rx_burst(rte_port, pkts, pkt_burst_sz);
		if (unlikely(n_pkts > pkt_burst_sz))
			continue;

		if (n_pkts == 0)
			continue;

		netif = net_port->netif;
		if (!netif) {
			dispatch_to_bridge(net_port, pkts, n_pkts);
		} else {
			switch (net_port->rte_port_type) {
			case RTE_PORT_TYPE_ETH:
				dispatch_to_ethif(netif, pkts, n_pkts);
				break;
			case RTE_PORT_TYPE_KNI:
				dispatch_to_kniif(netif, pkts, n_pkts);
				break;
			default:
				rte_panic("Invalid port type\n");
			}
		}
	}
	return 0;
}

int
dispatch_thread(struct net_port *ports, int nr_ports, int pkt_burst_sz)
{
	struct rte_mbuf *pkts[pkt_burst_sz];
	int ret = 0;

	while (!ret) {
		ret = dispatch(ports, nr_ports, pkts, pkt_burst_sz);
	}
	return ret;
}
