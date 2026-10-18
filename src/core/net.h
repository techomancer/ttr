/*
 * A snapshot of the network interfaces for the notification area: state,
 * addresses, traffic counters, and the access point on Wi-Fi. Read from
 * the kernel directly (no tools are run), so it is cheap to poll.
 *
 * Linux: /proc/net/dev, /sys/class/net, /proc/net/route, /proc/net/if_inet6,
 * /proc/net/wireless, SIOCGIFCONF and the wireless extensions ioctls.
 * Other systems (IRIX) get their own implementation of net_snapshot().
 */
#ifndef TTR_CORE_NET_H
#define TTR_CORE_NET_H

#include "core/str.h"
#include "core/vector.h"

namespace ttr {

struct NetIface {
    Str name;
    bool up;            /* administratively up and running */
    bool carrier;       /* cable plugged in / associated */
    bool wireless;
    bool is_default;    /* carries the default route */
    Str ipv4;           /* dotted quad, or empty */
    Str ipv6;           /* first global address, or empty */
    Str mac;
    /* Counters as double: exact far beyond 2^32 without long long. */
    double rx_packets, tx_packets, rx_bytes, tx_bytes;
    /* Wi-Fi, when associated */
    Str ssid;
    Str bssid;
    int quality;        /* 0..100, or -1 if unknown */

    NetIface();
};

/* Fill 'out' with every interface except loopback, the default route's
 * first. Returns false if this system has no implementation. The caller
 * owns the entries. */
bool net_snapshot(Vector<NetIface*>* out);

void net_free(Vector<NetIface*>* list);

/* A network configuration program found on this system. */
struct NetTool {
    Str label;
    Str command;
    bool daemon_running;    /* its network daemon is running */
};

/* Installed network configuration tools, those whose daemon is running
 * first. Terminal programs are run with 'terminal'. */
void find_net_tools(const char* terminal, Vector<NetTool*>* out);

/* "1.2 MB" style, for the status dialog. */
Str format_bytes(double bytes);

}

#endif
