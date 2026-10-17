#include "hostname.h"

#include <lwip/apps/mdns.h>
#include <netconfig.h>
#include <settings.h>
#include "net_stats.h"

extern CountingW5500lwIP eth;  // main.cpp

static char current[HOSTNAME_MAX_LENGTH + 1] = "";
static bool mdnsStarted = false;

void hostnameBegin() {
    hostnameFromText(currentSettings.sysName, current);
    DriverLock lock;
    eth.setHostname(current);  // used by DHCP once Ethernet starts
}

void hostnameNetworkUp() {
    netif* nif = eth.getNetIf();
    DriverLock lock;
    if (!mdnsStarted) {
        mdns_resp_init();
        if (mdns_resp_add_netif(nif, current) == ERR_OK) {
            mdns_resp_add_service(nif, "RF-UPS-Monitor", "_http", DNSSD_PROTO_TCP, 80, nullptr, nullptr);
            mdnsStarted = true;
            Serial.printf("\nmDNS: %s.local\n", current);
        } else {
            Serial.println("\nmDNS: could not start");
        }
    } else {
        mdns_resp_announce(nif);  // new address: tell the network
    }
}

void hostnameApply() {
    char next[HOSTNAME_MAX_LENGTH + 1];
    hostnameFromText(currentSettings.sysName, next);
    if (strcmp(next, current) == 0) return;
    strcpy(current, next);
    DriverLock lock;
    eth.setHostname(current);  // also renews a DHCP lease, so the server learns the new name
    if (mdnsStarted) mdns_resp_rename_netif(eth.getNetIf(), current);
    Serial.printf("\nHostname: %s (DHCP, mDNS %s.local)\n", current, current);
}

const char* hostnameCurrent() {
    return current;
}
