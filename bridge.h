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
#ifndef _BRIDGE_H_
#define _BRIDGE_H_

#include "port-plug.h"

#define BRIDGE_PORT_MAX		8

struct bridge;

struct bridge_port {
	int		 port_id;
	struct bridge	*bridge;
	struct net_port *net_port;
};

struct bridge_plug {
	struct plugif	*plugif;
	struct net_port	 net_port;
};

struct bridge {
	struct bridge_port	ports[BRIDGE_PORT_MAX];
	int			nr_ports;
	struct bridge_plug	plug;
};

extern struct bridge BR0;

int bridge_add_port(struct bridge *bridge, struct net_port *net_port);
int bridge_add_plug(struct bridge *bridge, struct net_port *net_port,
		    struct plugif *plugif);
int bridge_input(struct bridge *bridge, struct bridge_port *ingress,
		 struct rte_mbuf **pkts, int n_pkts);
int bridge_rx_burst(struct rte_port_plug *plug_port,
		    struct rte_mbuf **pkts, uint32_t n_pkts);
int bridge_tx_burst(struct rte_port_plug *plug_port,
		    struct rte_mbuf **pkts, uint32_t n_pkts);

#endif
