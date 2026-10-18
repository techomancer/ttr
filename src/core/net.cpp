#include "core/net.h"
#include "core/audio.h"     /* in_path() */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/wireless.h>
#endif

namespace ttr {

NetIface::NetIface()
    : up(false), carrier(false), wireless(false), is_default(false),
      rx_packets(0), tx_packets(0), rx_bytes(0), tx_bytes(0), quality(-1)
{
}

void net_free(Vector<NetIface*>* list)
{
    int i;
    for (i = 0; i < list->size(); i++)
        delete (*list)[i];
    list->clear();
}

Str format_bytes(double b)
{
    static const char* const units[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    int u = 0;
    while (b >= 1024.0 && u < 5) {
        b /= 1024.0;
        u++;
    }
    char buf[32];
    if (u == 0)
        sprintf(buf, "%.0f %s", b, units[u]);
    else
        sprintf(buf, "%.1f %s", b, units[u]);
    return Str(buf);
}

#if defined(__linux__)

/* First line of a small file, trimmed, into buf. */
static bool read_line(const char* path, char* buf, int size)
{
    FILE* f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fgets(buf, size, f) != 0;
    fclose(f);
    if (!ok)
        return false;
    int n = (int)strlen(buf);
    while (n > 0 && isspace((unsigned char)buf[n - 1]))
        buf[--n] = 0;
    return true;
}

static bool sys_flag(const char* iface, const char* file, const char* want)
{
    char path[256], buf[64];
    sprintf(path, "/sys/class/net/%.64s/%s", iface, file);
    return read_line(path, buf, sizeof(buf)) && strcmp(buf, want) == 0;
}

static bool sys_exists(const char* iface, const char* file)
{
    char path[256];
    sprintf(path, "/sys/class/net/%.64s/%s", iface, file);
    DIR* d = opendir(path);
    if (d) {
        closedir(d);
        return true;
    }
    return false;
}

static NetIface* find(Vector<NetIface*>* out, const char* name)
{
    int i;
    for (i = 0; i < out->size(); i++)
        if (strcmp((*out)[i]->name.c_str(), name) == 0)
            return (*out)[i];
    return 0;
}

/* /proc/net/dev: "  eth0: rxbytes rxpackets ... txbytes txpackets ..." */
static void read_dev(Vector<NetIface*>* out)
{
    FILE* f = fopen("/proc/net/dev", "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = 0;
        char* name = line;
        while (*name == ' ')
            name++;
        if (strcmp(name, "lo") == 0)
            continue;
        double v[16];
        int n = 0;
        char* p = colon + 1;
        while (n < 16) {
            char* end;
            double d = strtod(p, &end);
            if (end == p)
                break;
            v[n++] = d;
            p = end;
        }
        if (n < 10)
            continue;
        NetIface* it = new NetIface;
        it->name = name;
        it->rx_bytes = v[0];
        it->rx_packets = v[1];
        it->tx_bytes = v[8];
        it->tx_packets = v[9];
        out->push_back(it);
    }
    fclose(f);
}

static void read_default_route(Vector<NetIface*>* out)
{
    FILE* f = fopen("/proc/net/route", "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char iface[64];
        unsigned long dest = 1, mask = 1;
        if (sscanf(line, "%63s %lx %*s %*s %*s %*s %*s %lx", iface, &dest,
                   &mask) == 3 && dest == 0 && mask == 0) {
            NetIface* it = find(out, iface);
            if (it)
                it->is_default = true;
        }
    }
    fclose(f);
}

static void read_ipv4(Vector<NetIface*>* out)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return;
    struct ifreq reqs[64];
    struct ifconf ifc;
    ifc.ifc_len = sizeof(reqs);
    ifc.ifc_req = reqs;
    if (ioctl(s, SIOCGIFCONF, &ifc) == 0) {
        int n = ifc.ifc_len / (int)sizeof(struct ifreq), i;
        for (i = 0; i < n; i++) {
            NetIface* it = find(out, reqs[i].ifr_name);
            if (!it || !it->ipv4.empty())
                continue;
            struct sockaddr_in* sa = (struct sockaddr_in*)&reqs[i].ifr_addr;
            it->ipv4 = inet_ntoa(sa->sin_addr);
        }
    }
    close(s);
}

/* /proc/net/if_inet6: hex address, index, prefix, scope, flags, name.
 * Keep the first global (scope 00) address of each interface. */
static void read_ipv6(Vector<NetIface*>* out)
{
    FILE* f = fopen("/proc/net/if_inet6", "r");
    if (!f)
        return;
    char hex[64], name[64];
    unsigned int idx, plen, scope, flags;
    while (fscanf(f, "%40s %x %x %x %x %63s", hex, &idx, &plen, &scope,
                  &flags, name) == 6) {
        NetIface* it = find(out, name);
        if (!it || scope != 0 || !it->ipv6.empty() || strlen(hex) != 32)
            continue;
        struct in6_addr a;
        int i;
        for (i = 0; i < 16; i++) {
            unsigned int byte;
            sscanf(hex + 2 * i, "%2x", &byte);
            a.s6_addr[i] = (unsigned char)byte;
        }
        char buf[64];
        if (inet_ntop(AF_INET6, &a, buf, sizeof(buf)))
            it->ipv6 = buf;
    }
    fclose(f);
}

/* /proc/net/wireless: "wlan0: 0000   54.  -56.  -256 ..." link quality is
 * out of 70 on most drivers. */
static void read_quality(Vector<NetIface*>* out)
{
    FILE* f = fopen("/proc/net/wireless", "r");
    if (!f)
        return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char* colon = strchr(line, ':');
        if (!colon)
            continue;
        *colon = 0;
        char* name = line;
        while (*name == ' ')
            name++;
        NetIface* it = find(out, name);
        if (!it)
            continue;
        unsigned int status;
        double link;
        if (sscanf(colon + 1, "%x %lf", &status, &link) == 2) {
            int q = (int)(link * 100.0 / 70.0 + 0.5);
            it->quality = q > 100 ? 100 : q < 0 ? 0 : q;
        }
    }
    fclose(f);
}

/* SSID and BSSID through the wireless extensions; cfg80211 drivers still
 * answer these. */
static void read_wifi(NetIface* it)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return;
    struct iwreq req;
    char essid[IW_ESSID_MAX_SIZE + 1];

    memset(&req, 0, sizeof(req));
    strncpy(req.ifr_name, it->name.c_str(), IFNAMSIZ - 1);
    memset(essid, 0, sizeof(essid));
    req.u.essid.pointer = essid;
    req.u.essid.length = IW_ESSID_MAX_SIZE;
    if (ioctl(s, SIOCGIWESSID, &req) == 0 && essid[0])
        it->ssid = essid;

    memset(&req, 0, sizeof(req));
    strncpy(req.ifr_name, it->name.c_str(), IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIWAP, &req) == 0) {
        const unsigned char* m = (const unsigned char*)req.u.ap_addr.sa_data;
        bool zero = true;
        int i;
        for (i = 0; i < 6; i++)
            if (m[i] && m[i] != 0xff)
                zero = false;
        if (!zero) {
            char buf[32];
            sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2],
                    m[3], m[4], m[5]);
            it->bssid = buf;
        }
    }
    close(s);
}

bool net_snapshot(Vector<NetIface*>* out)
{
    read_dev(out);
    int i;
    for (i = 0; i < out->size(); i++) {
        NetIface* it = (*out)[i];
        const char* n = it->name.c_str();
        it->up = !sys_flag(n, "operstate", "down");
        it->carrier = sys_flag(n, "carrier", "1");
        it->wireless = sys_exists(n, "wireless") || sys_exists(n, "phy80211");
        char path[256], buf[64];
        sprintf(path, "/sys/class/net/%.64s/address", n);
        if (read_line(path, buf, sizeof(buf)))
            it->mac = buf;
    }
    read_default_route(out);
    read_ipv4(out);
    read_ipv6(out);
    read_quality(out);
    for (i = 0; i < out->size(); i++)
        if ((*out)[i]->wireless)
            read_wifi((*out)[i]);

    /* The default route's interface first, then the rest as listed. */
    for (i = 1; i < out->size(); i++) {
        if ((*out)[i]->is_default) {
            NetIface* d = (*out)[i];
            int j;
            for (j = i; j > 0; j--)
                (*out)[j] = (*out)[j - 1];
            (*out)[0] = d;
            break;
        }
    }
    return true;
}

#else

bool net_snapshot(Vector<NetIface*>* out)
{
    (void)out;
    return false;
}

#endif

/* Running daemons, by process name, from /proc. */
static bool daemon_running(const char* comm)
{
#if defined(__linux__)
    DIR* d = opendir("/proc");
    if (!d)
        return false;
    struct dirent* e;
    bool found = false;
    while (!found && (e = readdir(d)) != 0) {
        if (!isdigit((unsigned char)e->d_name[0]))
            continue;
        char path[64], buf[64];
        sprintf(path, "/proc/%.20s/comm", e->d_name);
        if (read_line(path, buf, sizeof(buf)) && strcmp(buf, comm) == 0)
            found = true;
    }
    closedir(d);
    return found;
#else
    (void)comm;
    return false;
#endif
}

struct KnownTool {
    const char* program;
    const char* label;
    const char* command;    /* empty: the program itself */
    bool terminal;
    const char* daemon;     /* process name of the network daemon it drives */
};

static const KnownTool k_tools[] = {
    { "nm-connection-editor", "Network Connections (NetworkManager)", "",
      false, "NetworkManager" },
    { "gnome-control-center", "GNOME Network Settings",
      "gnome-control-center network", false, "NetworkManager" },
    { "systemsettings", "KDE Network Settings",
      "systemsettings kcm_networkmanagement", false, "NetworkManager" },
    { "nmtui", "NetworkManager (nmtui)", "", true, "NetworkManager" },
    { "cmst", "ConnMan Settings (cmst)", "", false, "connmand" },
    { "connman-gtk", "ConnMan Settings (connman-gtk)", "", false,
      "connmand" },
    { "iwgtk", "Wi-Fi Settings (iwgtk)", "", false, "iwd" },
    { "iwctl", "Wi-Fi (iwctl)", "", true, "iwd" },
    { "wpa_gui", "Wi-Fi Settings (wpa_gui)", "", false, "wpa_supplicant" },
    { "wicd-gtk", "Wicd Network Manager", "", false, "wicd" }
};

void find_net_tools(const char* terminal, Vector<NetTool*>* out)
{
    const int n = (int)(sizeof(k_tools) / sizeof(k_tools[0]));
    int pass, i;
    /* Two passes: tools whose daemon runs, then the others. */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < n; i++) {
            const KnownTool& k = k_tools[i];
            if (!in_path(k.program))
                continue;
            bool running = daemon_running(k.daemon);
            if (running != (pass == 0))
                continue;
            NetTool* t = new NetTool;
            t->label = k.label;
            t->daemon_running = running;
            const char* cmd = k.command[0] ? k.command : k.program;
            if (k.terminal && terminal && *terminal) {
                char buf[512];
                sprintf(buf, "%.300s %.200s", terminal, cmd);
                t->command = buf;
            } else {
                t->command = cmd;
            }
            out->push_back(t);
        }
    }
}

}
