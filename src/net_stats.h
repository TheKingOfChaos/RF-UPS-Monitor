#pragma once

/**
 * @file net_stats.h
 * @brief Ethernet traffic counters and link state for IF-MIB.
 *
 * The W5500 has no traffic counters in the MACRAW mode used by lwIP, so every
 * frame is counted where it passes between lwIP and the driver: the netif
 * input and linkoutput hooks are wrapped. Link, speed and duplex come from
 * the W5500 PHYCFGR register, read under the same lock the driver uses.
 * Frames the driver drops before lwIP sees them (no free receive buffer)
 * are counted by CountingW5500. Counting logic is in lib/if_stats (host tested).
 */

#include <Arduino.h>
#include <W5500lwIP.h>
#include <snmp_mib.h>

struct netif;

/**
 * @brief Count a received frame the driver dropped. Called with the driver lock held.
 */
void netStatsCountDiscard(uint16_t length);

/**
 * @brief The core's W5500 driver, counting the frames it has to drop
 *
 * LwipIntfDev::handlePackets() calls RawDev::discardFrame() exactly when it
 * cannot get a pbuf for a received frame, so hiding discardFrame here sees
 * every such drop (ifInDiscards) without changing the core.
 */
class CountingW5500 : public Wiznet5500 {
public:
    using Wiznet5500::Wiznet5500;

    /// Count the dropped frame, then let the W5500 skip it
    void discardFrame(uint16_t framesize) {
        netStatsCountDiscard(framesize);
        Wiznet5500::discardFrame(framesize);
    }
};

/// Wiznet5500lwIP with ifInDiscards counting
using CountingW5500lwIP = LwipIntfDev<CountingW5500>;

/**
 * @brief Start counting frames on this interface. Call once after Ethernet has started.
 */
void netStatsBegin(netif* nif);

/**
 * @brief Poll the PHY for link changes (every 500 ms). Call from loop() on core 0.
 * @param uptimeTicks Current sysUpTime, recorded as ifLastChange on a change
 */
void netStatsPoll(uint32_t uptimeTicks);

/**
 * @brief Consistent copy of the counters and link state for the MIB
 */
void netStatsCapture(SnmpInterfaceState& out);

/**
 * @brief Latest decoded PHY status (link, 10/100, duplex)
 */
PhyStatus netStatsPhy();
