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
#ifndef _PORT_H_
#define _PORT_H_

#include <lwip/ip_addr.h>
#include <lwip/netif.h>

typedef enum {
	RTE_PORT_TYPE_ETH = 1,
	RTE_PORT_TYPE_KNI,
} rte_port_type;

struct rte_port rte_port;

typedef int (*rte_port_op_rx_burst)
	(struct rte_port *rte_port, struct rte_mbuf **pkts, uint32_t n_pkts);
typedef int (*rte_port_op_tx_burst)
	(struct rte_port *rte_port, struct rte_mbuf **pkts, uint32_t n_pkts);

struct rte_port_ops {
	rte_port_op_rx_burst	rx_burst;
	rte_port_op_tx_burst	tx_burst;
};

struct rte_port_stats {
	uint64_t	rx_packets;
	uint64_t	tx_packets;
	uint64_t	rx_dropped;
	uint64_t	tx_dropped;
};

struct rte_port {
	rte_port_type		type;
	struct rte_port_ops	ops;
	struct rte_port_stats	stats;
};

struct net {
	uint8_t		 port_id;
	char		*name;
	ip_addr_t	 ip_addr;
	ip_addr_t	 netmask;
	ip_addr_t	 gw;
};

struct net_port {
	rte_port_type	 rte_port_type;
	struct net	 net;
	struct netif	*netif;
	struct rte_port *rte_port;
};

#ifndef container_of
#define container_of(ptr, type, member)                                 \
        ((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#endif

#endif
