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

#include "dispatch.h"
#include "ethif.h"
#include "kniif.h"
#include "main.h"

/* exported in lwipopts.h */
unsigned char debug_flags = LWIP_DBG_OFF;

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
parse_pair(struct net *net, char* key, char* value)
{
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
}

static int
parse_port(struct net *net, char *param)
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
			if (parse_pair(net, key, value) != 0)
				return -1;
			state = END;
			break;
		case ',':
			if (state != VALUE)
				return -1;
			state = WAIT_KEY;
			*p++ = 0;
			if (parse_pair(net, key, value) != 0)
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
parse_args(int argc, char **argv)
{
	int ch;
	struct net_port *port;

#ifdef LWIP_DEBUG
	while ((ch = getopt(argc, argv, "e:k:d")) != -1) {
#else
	while ((ch = getopt(argc, argv, "e:k:")) != -1) {
#endif
	switch (ch) {
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
create_eth_port(struct net_port *net_port, struct rte_mempool *mempool)
{
	RTE_VERIFY(net_port->rte_port_type == RTE_PORT_TYPE_ETH);

	struct net *net = &net_port->net;
	struct rte_port_eth_params params = {
		.port_id = net->port_id,
		.mempool = mempool
	};
	struct ethif *ethif;
	struct netif *netif;

	ethif = ethif_alloc(rte_socket_id());
	if (ethif == NULL)
		rte_exit(EXIT_FAILURE, "Cannot alloc eth port\n");

	if (ethif_init(ethif, &params, rte_socket_id(), net_port) != ERR_OK)
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

	return 0;
}

static int
create_kni_port(struct net_port *net_port, struct rte_mempool *mempool)
{
	RTE_VERIFY(net_port->rte_port_type == RTE_PORT_TYPE_KNI);

	struct net *net = &net_port->net;
	struct rte_port_kni_params params = {
		.name = net->name,
		.mbuf_size = MAX_PACKET_SZ,
		.mempool = mempool
	};
	struct kniif *kniif;
	struct netif *netif;

	kniif = kniif_alloc(rte_socket_id());
	if (kniif == NULL)
		rte_exit(EXIT_FAILURE, "Cannot alloc kni port\n");

	if (kniif_init(kniif, &params, rte_socket_id(), net_port) != ERR_OK)
		rte_exit(EXIT_FAILURE, "Cannot init kni port\n");

	netif = &kniif->netif;
	netif_add(netif,
		  IP4_OR_NULL(net->ip_addr),
		  IP4_OR_NULL(net->netmask),
		  IP4_OR_NULL(net->gw),
		  kniif,
		  kniif_added_cb,
		  ethernet_input);
	netif_set_up(netif);

	return 0;
}

int
main(int argc, char *argv[])
{
	struct rte_mempool *mempool;
	int i, ret;

	ret = rte_eal_init(argc, argv);
        if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL arguments\n");
	argc -= ret;
        argv += ret;

	ret = parse_args(argc, argv);
        if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid arguments\n");

	mempool = rte_mempool_create("mbuf_pool", NB_MBUF, MBUF_SZ,
				     MEMPOOL_CACHE_SZ,
				     sizeof(struct rte_pktmbuf_pool_private),
				     rte_pktmbuf_pool_init, NULL,
				     rte_pktmbuf_init, NULL,
				     rte_socket_id(), 0);
	if (!mempool)
		rte_panic("Cannot init mbuf pool\n");

	if (rte_eal_pci_probe() < 0)
                rte_exit(EXIT_FAILURE, "Cannot probe PCI\n");

        nr_eth_dev = rte_eth_dev_count();

	RTE_LOG(INFO, APP, "Found %d ethernet device\n", nr_eth_dev);

	lwip_init();

	for (i = 0; i < nr_ports; i++) {
		struct net_port *net_port = &ports[i];

		switch (net_port->rte_port_type) {
		case RTE_PORT_TYPE_ETH:
			if (net_port->net.port_id >= nr_eth_dev)
				rte_exit(EXIT_FAILURE, "No ethernet device\n");
			create_eth_port(net_port, mempool);

			RTE_LOG(INFO, APP, "Created eth port port_id=%u\n",
				net_port->net.port_id);
			break;
		case RTE_PORT_TYPE_KNI:
			create_kni_port(net_port, mempool);

			RTE_LOG(INFO, APP, "Created kni port name=%s\n",
				net_port->net.name);
			break;
		default:
			rte_exit(EXIT_FAILURE, "Invalid port type\n");
		}
	}

	RTE_LOG(INFO, APP, "Dispatching %d ports\n", nr_ports);

	return dispatch_thread(ports, nr_ports, PKT_BURST_SZ);
}
