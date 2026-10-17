#include "net_stats.h"

#include <LwipEthernet.h>
#include <SPI.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <string.h>
#include "pins.h"

// W5500 common register with link, speed and duplex
static const uint16_t W5500_PHYCFGR = 0x002E;
static const uint8_t W5500_COMMON_READ = 0x00;  // block 0 (common), read, variable length

static const uint32_t PHY_POLL_MS = 500;

// Updated from lwIP context, read under the lwIP lock
static IfCounters counters = {};
static netif_input_fn originalInput = nullptr;
static netif_linkoutput_fn originalLinkOutput = nullptr;

// Updated by netStatsPoll() on core 0
static PhyStatus phy = {false, false, false};
static uint32_t lastChangeTicks = 0;
static uint8_t mac[6] = {0};
static bool started = false;

/// Received frame: count, then hand to lwIP as before (it drops what it cannot use)
static err_t countingInput(struct pbuf* p, struct netif* nif) {
    // The driver allocates one contiguous pbuf per frame (PBUF_RAM), so payload holds the header
    if (p != nullptr) ifCountReceived(counters, (const uint8_t*)p->payload, p->len, LWIP_IPV6 != 0);
    return originalInput(p, nif);
}

void netStatsCountDiscard(uint16_t length) {
    ifCountDiscard(counters, length);
}

/// Frame to send: hand to the driver, count the result
static err_t countingLinkOutput(struct netif* nif, struct pbuf* p) {
    err_t err = originalLinkOutput(nif, p);
    if (err == ERR_OK) {
        ifCountFrame(counters, (const uint8_t*)p->payload, p->tot_len, true);
    } else {
        counters.outErrors++;
    }
    return err;
}

/**
 * @brief Read PHYCFGR over SPI. The driver keeps this register private, so it
 *        is read directly, holding the same lock the driver uses for its own access.
 */
static uint8_t readPhyCfgr() {
    DriverLock lock;
    digitalWrite(W5500_CS_PIN, LOW);
    SPI.transfer((uint8_t)(W5500_PHYCFGR >> 8));
    SPI.transfer((uint8_t)(W5500_PHYCFGR & 0xFF));
    SPI.transfer(W5500_COMMON_READ);
    uint8_t value = SPI.transfer(0x00);
    digitalWrite(W5500_CS_PIN, HIGH);
    return value;
}

void netStatsBegin(netif* nif) {
    if (nif == nullptr || started) return;
    DriverLock lock;
    originalInput = nif->input;
    originalLinkOutput = nif->linkoutput;
    nif->input = countingInput;
    nif->linkoutput = countingLinkOutput;
    memcpy(mac, nif->hwaddr, 6);
    started = true;
}

void netStatsPoll(uint32_t uptimeTicks) {
    static uint32_t lastPoll = 0;
    if (!started || millis() - lastPoll < PHY_POLL_MS) return;
    lastPoll = millis();

    PhyStatus now = decodePhyCfgr(readPhyCfgr());
    if (now.linkUp != phy.linkUp) {
        lastChangeTicks = uptimeTicks;
        Serial.printf("\nEthernet link %s", now.linkUp ? "up" : "down");
        if (now.linkUp) Serial.printf(", %s Mbit/s %s duplex", now.speed100 ? "100" : "10",
                                      now.fullDuplex ? "full" : "half");
        Serial.println();
    }
    phy = now;
}

void netStatsCapture(SnmpInterfaceState& out) {
    {
        DriverLock lock;  // counters are written from lwIP context
        out.counters = counters;
    }
    out.linkUp = phy.linkUp;
    out.speedBps = phySpeedBps(phy);
    memcpy(out.mac, mac, 6);
    out.lastChangeTicks = lastChangeTicks;
}

PhyStatus netStatsPhy() {
    return phy;
}
