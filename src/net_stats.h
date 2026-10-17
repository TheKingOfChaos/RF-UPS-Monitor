#pragma once

/**
 * @file net_stats.h
 * @brief Ethernet traffic counters and link state for IF-MIB.
 *
 * The W5500 has no traffic counters in the MACRAW mode used by lwIP, so every
 * frame is counted where it passes between lwIP and the driver: the netif
 * input and linkoutput hooks are wrapped. Link, speed and duplex come from
 * the W5500 PHYCFGR register, read under the same lock the driver uses.
 * Counting logic is in lib/if_stats (host tested).
 */

#include <Arduino.h>
#include <snmp_mib.h>

struct netif;

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
