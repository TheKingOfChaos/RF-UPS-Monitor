#pragma once

/**
 * @file snmp_service.h
 * @brief Runs the SNMP v1 agent (lib/snmp_agent) on UDP port 161.
 *
 * All protocol logic is in lib/snmp_agent and lib/access_control; this file
 * moves packets between lwIP and the agent, collects the device state for
 * the MIB, applies the per-source rate limit, logs rejected access and sends
 * traps to the configured destination.
 */

#include <IPAddress.h>
#include <access_control.h>
#include <snmp_agent.h>

/// Standard SNMP agent port
constexpr uint16_t SNMP_PORT = 161;

/// Requests per second each source may send on average, and in a burst
constexpr uint32_t SNMP_RATE_PER_SECOND = 50;
constexpr uint32_t SNMP_RATE_BURST = 100;

/// At most one authenticationFailure trap per interval
constexpr uint32_t AUTH_TRAP_INTERVAL_MS = 10000;

/**
 * @brief Counters of the service itself (the agent keeps its own SnmpStats)
 */
struct SnmpServiceStats {
    uint32_t rateLimited;   ///< Requests dropped by the rate limiter
    uint32_t trapsSent;     ///< Traps sent to the trap destination
    uint32_t trapsFailed;   ///< Traps that could not be encoded or sent
};

/**
 * @brief Open the UDP socket. Call once after Ethernet has started.
 * @return true if the port was bound
 */
bool snmpBegin();

/**
 * @brief Tell the service the device has an address; sends coldStart the first time
 * @param localIP The device's address, used as agent-addr in traps
 */
void snmpNetworkUp(const IPAddress& localIP);

/**
 * @brief Handle pending requests. Call from loop() on core 0.
 */
void snmpPoll();

/**
 * @brief Send mainsPowerLost or mainsPowerRestored for the current mains state
 */
void snmpNotifyMainsChange();

/**
 * @brief Request and error counters for the status screen
 */
const SnmpStats& snmpStats();

/**
 * @brief Rate limiting and trap counters for the status screen
 */
const SnmpServiceStats& snmpServiceStats();

/**
 * @brief Rejected access attempts, newest first
 */
const AccessLog& snmpAccessLog();
