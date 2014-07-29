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
#include <getopt.h>
#include <netdb.h>
#include <netif/etharp.h>

#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ring.h>

#include <lwip/init.h>

#include "bridge.h"
#include "dispatch.h"
#include "ethif.h"
#include "kniif.h"
#include "plugif.h"
#include "main.h"
#include "mempool.h"

/* exported in lwipopts.h */
unsigned char debug_flags = LWIP_DBG_OFF;

/* eonfigurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512

/* custom port abstraction (i.e. eth, kni with lwip) */
#define PORT_MAX    8

static struct net_port ports[PORT_MAX];
static int nr_ports = 0;
static int nr_eth_dev = 0;

static int
parse_address(char* addr, struct addrinfo *info, int family) {
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *r;
	int res;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = 0;
	hints.ai_protocol = 0;
	hints.ai_flags = AI_NUMERICSERV;
	res = getaddrinfo(addr, 0, &hints, &result);
	if (res != 0) {
		RTE_LOG(ERR, APP, "Failed getaddrinfo: %s\n",
			gai_strerror(res));
		return -1;
	}
	for (r = result; r != NULL; r = r->ai_next) {
		if (r->ai_family == family) {
			*info = *r;
			return 0;
		}
	}
	return -1;
}

static int
parse_port_pair(void *opts, char* key, char* value)
{
	struct net *net = (struct net *)opts;
	struct addrinfo addr;
	uint8_t* p;

	if (key == 0 || *key == 0)
		return -1;

#define PARSE_IP4(ip_addr)						\
	do {								\
		if (value == 0 || *value == 0)				\
			return -1;					\
		if (parse_address(value, &addr, AF_INET) != 0)		\
			return -1;					\
		p = (uint8_t*)&((struct sockaddr_in*)addr.ai_addr)->sin_addr; \
		IP4_ADDR(&(ip_addr), *p, *(p+1), *(p+2), *(p+3));	\
	} while(0)

	if (!strcmp(key,"port_id")) {
		if (value == 0 || *value == 0)
			return -1;
		net->port_id = rte_str_to_size(value);
		return 0;
	} else if (!strcmp(key,"name")) {
		if (value == 0 || *value == 0)
			return -1;
		net->name = value;
		return 0;
	} else if (!strcmp(key,"addr")) {
		PARSE_IP4(net->ip_addr);
		return 0;
	} else if (!strcmp(key,"netmask")) {
		PARSE_IP4(net->netmask);
		return 0;
	} else if (!strcmp(key,"gw")) {
		PARSE_IP4(net->gw);
		return 0;
	} else {
		return -1;
	}
#undef PARSE_IP4
}

static int
parse_vxlan_pair(void *opts, char* key, char* value)
{
	struct vxlan_peer *peer = (struct vxlan_peer *)opts;
	struct addrinfo addr;
	uint8_t* p;

	if (key == 0 || *key == 0)
		return -1;

#define PARSE_IP4(ip_addr)						\
	do {								\
		if (value == 0 || *value == 0)				\
			return -1;					\
		if (parse_address(value, &addr, AF_INET) != 0)		\
			return -1;					\
		p = (uint8_t*)&((struct sockaddr_in*)addr.ai_addr)->sin_addr; \
		IP4_ADDR(&(ip_addr), *p, *(p+1), *(p+2), *(p+3));	\
	} while(0)

	if (!strcmp(key,"addr")) {
		PARSE_IP4(peer->ip_addr);
		return 0;
	} else if (!strcmp(key,"port")) {
		peer->port = atoi(value);
		return 0;
	} else {
		return -1;
	}
#undef PARSE_IP4
}

static int
parse_pairs(void *opts, char *param, int (*parse_pair)(void *, char*, char*))
{
	enum {
		WAIT_KEY, KEY, WAIT_VALUE, VALUE, END
	};
	int state = WAIT_KEY;
	char* p = param;
	char* key = 0;
	char* value = 0;

	while (state != END) {
		switch (*p) {
		case '\0':
			if (parse_pair(opts, key, value) != 0)
				return -1;
			state = END;
			break;
		case ',':
			if (state != VALUE)
				return -1;
			state = WAIT_KEY;
			*p++ = 0;
			if (parse_pair(opts, key, value) != 0)
				return -1;
			key = value = 0;
			break;
		case '=':
			if (state != KEY)
				return -1;
			state = WAIT_VALUE;
			*p++ = 0;
			break;
		default:
			if (state == WAIT_KEY) {
				state = KEY;
				key = p;
			} else if (state == WAIT_VALUE) {
				state = VALUE;
				value = p;
			}
			p++;
		}
	}
	return 0;
}

static int
parse_port(struct net *net, char *param)
{
	return parse_pairs(net, param, parse_port_pair);
}

static int
parse_vxlan(struct vxlan_peer *peer, char *param)
{
	return parse_pairs(peer, param, parse_vxlan_pair);
}

static int
parse_args(int argc, char **argv)
{
	int ch;
	struct net_port *port;
	struct vxlan_peer peer;

#ifdef LWIP_DEBUG
	while ((ch = getopt(argc, argv, "P:V:e:k:d")) != -1) {
#else
	while ((ch = getopt(argc, argv, "P:V:e:k:")) != -1) {
#endif
	switch (ch) {
		case 'P':
			port = &BR0.plug.net_port;
			if (parse_port(&port->net, optarg))
				return -1;
			port->rte_port_type = RTE_PORT_TYPE_PLUG;
			break;
		case 'V':
			memset(&peer, 0, sizeof(peer));
			if (parse_vxlan(&peer, optarg))
				return -1;
			if (bridge_add_vxlan(&BR0, &peer) != 0)
				return -1;
			break;
		case 'e':
			if (nr_ports >= PORT_MAX)
				break;
			port = &ports[nr_ports];
			if (parse_port(&port->net, optarg))
				return -1;
			port->rte_port_type = RTE_PORT_TYPE_ETH;
			nr_ports++;
			break;
		case 'k':
			if (nr_ports >= PORT_MAX)
				break;
			port = &ports[nr_ports];
			if (parse_port(&port->net, optarg))
				return -1;
			port->rte_port_type = RTE_PORT_TYPE_KNI;
			nr_ports++;
			break;

#ifdef LWIP_DEBUG
		case 'd':
			debug_flags |= (LWIP_DBG_ON|
					LWIP_DBG_TRACE|
					LWIP_DBG_STATE|
					LWIP_DBG_FRESH|
					LWIP_DBG_HALT);
			break;
#endif
		default:
			return -1;
		}
	}
	argc -= optind;
	argv += optind;

	return 0;
}

#define IP4_OR_NULL(ip_addr) ((ip_addr).addr == IPADDR_ANY ? 0 : &(ip_addr))

static int
create_eth_port(struct net_port *net_port, int socket_id)
{
	RTE_VERIFY(net_port->rte_port_type == RTE_PORT_TYPE_ETH);

	struct net *net = &net_port->net;
	struct rte_port_eth_params params = {
		.port_id = net->port_id,
		.nb_rx_desc = RTE_TEST_RX_DESC_DEFAULT,
		.nb_tx_desc = RTE_TEST_TX_DESC_DEFAULT,
		.mempool = pktmbuf_pool,
	};

	if (!IP4_OR_NULL(net_port->net.ip_addr)) {
		struct rte_port_eth *eth_port;

		eth_port = rte_port_eth_create(&params, socket_id, net_port);
		if (!eth_port)
			rte_exit(EXIT_FAILURE, "Cannot alloc kni port\n");

		bridge_add_port(&BR0, net_port);
	} else {
		struct ethif *ethif;
		struct netif *netif;

		ethif = ethif_alloc(socket_id);
		if (ethif == NULL)
			rte_exit(EXIT_FAILURE, "Cannot alloc eth port\n");

		if (ethif_init(ethif, &params, socket_id, net_port) != ERR_OK)
			rte_exit(EXIT_FAILURE, "Cannot init eth port\n");

		netif = &ethif->netif;
		netif_add(netif,
			  IP4_OR_NULL(net->ip_addr),
			  IP4_OR_NULL(net->netmask),
			  IP4_OR_NULL(net->gw),
			  ethif,
			  ethif_added_cb,
			  ethernet_input);
		netif_set_up(netif);
	}

	return 0;
}

static int
create_kni_port(struct net_port *net_port, int socket_id)
{
	RTE_VERIFY(net_port->rte_port_type == RTE_PORT_TYPE_KNI);

	struct net *net = &net_port->net;
	struct rte_port_kni_params params = {
		.name = net->name,
		.mbuf_size = MAX_PACKET_SZ,
		.mempool = pktmbuf_pool,
	};

	if (!IP4_OR_NULL(net_port->net.ip_addr)) {
		struct rte_port_kni *kni_port;

		kni_port = rte_port_kni_create(&params, socket_id, net_port);
		if (!kni_port)
			rte_exit(EXIT_FAILURE, "Cannot alloc kni port\n");

		bridge_add_port(&BR0, net_port);
	} else {
		struct kniif *kniif;
		struct netif *netif;

		kniif = kniif_alloc(socket_id);
		if (kniif == NULL)
			rte_exit(EXIT_FAILURE, "Cannot alloc kni interface\n");

		if (kniif_init(kniif, &params, socket_id, net_port) != ERR_OK)
			rte_exit(EXIT_FAILURE, "Cannot init kni interface\n");

		netif = &kniif->netif;
		netif_add(netif,
			  IP4_OR_NULL(net->ip_addr),
			  IP4_OR_NULL(net->netmask),
			  IP4_OR_NULL(net->gw),
			  kniif,
			  kniif_added_cb,
			  ethernet_input);
		netif_set_up(netif);
	}

	return 0;
}

static int
create_plug_port(struct net_port *net_port, int socket_id)
{
	RTE_VERIFY(net_port->rte_port_type == RTE_PORT_TYPE_PLUG);

	struct net *net = &net_port->net;

	if (!IP4_OR_NULL(net_port->net.ip_addr)) {
		struct rte_port_plug *plug_port;
		struct rte_port_plug_params params = {
			.tx_burst     = bridge_tx_vxlan_burst,
			.private_data = &BR0,
		};

		plug_port = rte_port_plug_create(&params, socket_id, net_port);
		if (!plug_port)
			rte_exit(EXIT_FAILURE, "Cannot alloc plug port\n");

		if (bridge_add_port(&BR0, net_port) != 0)
			rte_exit(EXIT_FAILURE, "Cannot add bridge port\n");
	} else {
		struct rte_port_plug_params params = {
			.rx_burst     = bridge_rx_burst,
			.tx_burst     = bridge_tx_burst,
			.private_data = &BR0,
		};
		struct plugif *plugif;
		struct netif *netif;

		plugif = plugif_alloc(socket_id);
		if (plugif == NULL)
			rte_exit(EXIT_FAILURE, "Cannot alloc plug interface\n");

		if (plugif_init(plugif, &params, socket_id, net_port) != ERR_OK)
			rte_exit(EXIT_FAILURE, "Cannot init plug interface\n");

		netif = &plugif->netif;
		netif_add(netif,
			  IP4_OR_NULL(net->ip_addr),
			  IP4_OR_NULL(net->netmask),
			  IP4_OR_NULL(net->gw),
			  plugif,
			  plugif_added_cb,
			  ethernet_input);
		netif_set_up(netif);

		if (bridge_add_plug(&BR0, net_port, plugif) != 0)
			rte_exit(EXIT_FAILURE, "Cannot add plug port\n");
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	int i, ret;

	ret = rte_eal_init(argc, argv);
        if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	argc -= ret;
        argv += ret;

	lwip_init();

	ret = parse_args(argc, argv);
        if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid arguments\n");

	if (rte_eal_pci_probe() < 0)
                rte_exit(EXIT_FAILURE, "Cannot probe PCI\n");

        nr_eth_dev = rte_eth_dev_count();

	RTE_LOG(INFO, APP, "Found %d ethernet device\n", nr_eth_dev);

	mempool_init(rte_socket_id());

	for (i = 0; i < nr_ports; i++) {
		struct net_port *net_port = &ports[i];

		switch (net_port->rte_port_type) {
		case RTE_PORT_TYPE_ETH:
			if (net_port->net.port_id >= nr_eth_dev)
				rte_exit(EXIT_FAILURE, "No ethernet device\n");

			create_eth_port(net_port, rte_socket_id());

			RTE_LOG(INFO, APP, "Created eth port port_id=%u\n",
				net_port->net.port_id);
			break;
		case RTE_PORT_TYPE_KNI:
			create_kni_port(net_port, rte_socket_id());

			RTE_LOG(INFO, APP, "Created kni port name=%s\n",
				net_port->net.name);
			break;
		default:
			rte_exit(EXIT_FAILURE, "Invalid port type\n");
		}
	}

	if (BR0.plug.net_port.rte_port_type) {
		create_plug_port(&BR0.plug.net_port, rte_socket_id());

		RTE_LOG(INFO, APP, "Created plug port in bridge\n");

		if (BR0.vxlan.nr_peers > 0){
			if (bridge_bind_vxlan(&BR0) != ERR_OK)
				rte_exit(EXIT_FAILURE, "Cannot bind VXLAN\n");

			RTE_LOG(INFO, APP, "Bound VXLAN port\n");
		}
	}

	RTE_LOG(INFO, APP, "Dispatching %d ports\n", nr_ports);

	return dispatch_thread(ports, nr_ports, PKT_BURST_SZ);
}
