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
#ifndef _PORT_ETH_H_
#define _PORT_ETH_H_

#include <rte_ethdev.h>

#include "port.h"

struct rte_port_eth_params {
	uint8_t			 port_id;
	uint16_t		 nb_rx_desc;
	uint16_t		 nb_tx_desc;
	struct rte_eth_conf	 eth_conf;
	struct rte_eth_rxconf	 rx_conf;
	struct rte_eth_txconf	 tx_conf;
	struct rte_mempool	*mempool;
};

struct rte_port_eth {
	uint8_t			 port_id;
	struct rte_eth_dev_info	 eth_dev_info;
	struct rte_mempool	*mempool;
	struct rte_port		 rte_port;
};

struct rte_port_eth * rte_port_eth_create
	(struct rte_port_eth_params *conf, int socket_id);
int rte_port_eth_tx_burst
	(struct rte_port *rte_port, struct rte_mbuf **pkts, uint32_t n_pkts);

#endif
