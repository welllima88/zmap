#include "recv.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#include "../lib/includes.h"
#include "../lib/logger.h"

#include <pcap.h>
#include <pcap/pcap.h>

#include "recv-internal.h"
#include "state.h"

#include "probe_modules/probe_modules.h"

#define PCAP_PROMISC 1
#define PCAP_TIMEOUT 1000

static pcap_t *pc = NULL;

void packet_cb(u_char __attribute__((__unused__)) *user,
		const struct pcap_pkthdr *p, const u_char *bytes)
{
	if (!p) {
		return;
	}
	if (zrecv.success_unique >= zconf.max_results) {
		// Libpcap can process multiple packets per pcap_dispatch;
		// we need to throw out results once we've
		// gotten our --max-results worth.
		return;
	}
	// length of entire packet captured by libpcap
	uint32_t buflen = (uint32_t) p->caplen;
	handle_packet(buflen, bytes);
}

void recv_init()
{
	char bpftmp[1024];
	char errbuf[PCAP_ERRBUF_SIZE];
	pc = pcap_open_live(zconf.iface, zconf.probe_module->pcap_snaplen,
					PCAP_PROMISC, PCAP_TIMEOUT, errbuf);
	if (pc == NULL) {
		log_fatal("recv", "could not open device %s: %s",
						zconf.iface, errbuf);
	}
	struct bpf_program bpf;

	snprintf(bpftmp, sizeof(bpftmp)-1, "ether src %02x:%02x:%02x:%02x:%02x:%02x and (%s)",
		zconf.gw_mac[0], zconf.gw_mac[1], zconf.gw_mac[2],
		zconf.gw_mac[3], zconf.gw_mac[4], zconf.gw_mac[5],
		zconf.probe_module->pcap_filter);

	if (pcap_compile(pc, &bpf, bpftmp, 1, 0) < 0) {
		log_fatal("recv", "couldn't compile filter");
	}
	if (pcap_setfilter(pc, &bpf) < 0) {
		log_fatal("recv", "couldn't install filter");
	}
	// set pcap_dispatch to not hang if it never receives any packets
	// this could occur if you ever scan a small number of hosts as
	// documented in issue #74.
	if (pcap_setnonblock (pc, 1, errbuf) == -1) {
		log_fatal("recv", "pcap_setnonblock error:%s", errbuf);
	}
}

void recv_packets()
{
	int ret = pcap_dispatch(pc, -1, packet_cb, NULL);
	if (ret == -1) {
		log_fatal("recv", "pcap_dispatch error");
	} else if (ret == 0) {
		usleep(1000);
	}
}

void recv_cleanup()
{
	pcap_close(pc);
	pc = NULL;
}

int recv_update_stats(void)
{
	if (!pc) {
		return EXIT_FAILURE;
	}
	struct pcap_stat pcst;
	if (pcap_stats(pc, &pcst)) {
		log_error("recv", "unable to retrieve pcap statistics: %s",
				pcap_geterr(pc));
		return EXIT_FAILURE;
	} else {
		zrecv.pcap_recv = pcst.ps_recv;
		zrecv.pcap_drop = pcst.ps_drop;
		zrecv.pcap_ifdrop = pcst.ps_ifdrop;
	}
	return EXIT_SUCCESS;
}
