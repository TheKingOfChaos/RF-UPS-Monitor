#pragma once

/**
 * @file snmp_mib.h
 * @brief The device MIB: a sorted table of OIDs and how to read each one.
 *
 * Objects (see docs/MIB.md and mibs/RF-UPS-MONITOR-MIB.txt):
 *
 *   1.3.6.1.2.1.1.1.0      sysDescr              OCTET STRING
 *   1.3.6.1.2.1.1.2.0      sysObjectID           OBJECT IDENTIFIER (1.3.6.1.4.1.63050)
 *   1.3.6.1.2.1.1.3.0      sysUpTime             TimeTicks
 *   1.3.6.1.2.1.1.4.0      sysContact            OCTET STRING
 *   1.3.6.1.2.1.1.5.0      sysName               OCTET STRING
 *   1.3.6.1.2.1.1.6.0      sysLocation           OCTET STRING
 *   1.3.6.1.2.1.1.7.0      sysServices           INTEGER (72)
 *   1.3.6.1.4.1.63050.1.0  mainsPowerStatus      INTEGER 0 = lost, 1 = present
 *   1.3.6.1.4.1.63050.2.0  mainsPowerChanges     Counter32
 *   1.3.6.1.4.1.63050.3.0  mainsPowerLastChange  TimeTicks (sysUpTime at the last change)
 *
 * Pure C++: values come from an SnmpDeviceState snapshot that the firmware
 * fills in, so the MIB is unit tested on the host.
 */

#include "snmp_message.h"

/// Enterprise number assigned to this project
constexpr uint32_t ENTERPRISE_NUMBER = 63050;

/**
 * @brief Everything the MIB reports, captured once per request
 */
struct SnmpDeviceState {
    uint32_t uptimeTicks;          ///< Hundredths of a second since boot
    bool mainsPresent;             ///< Debounced mains state
    uint32_t mainsChanges;         ///< Accepted mains changes since boot
    uint32_t mainsLastChangeTicks; ///< uptimeTicks at the last change, 0 if none
    const char* sysDescr;
    const char* sysContact;
    const char* sysName;
    const char* sysLocation;
};

/**
 * @brief One MIB object
 */
struct MibEntry {
    Oid oid;
    SnmpValue (*get)(const SnmpDeviceState& state);
};

/**
 * @brief Number of objects in the MIB
 */
uint8_t mibSize();

/**
 * @brief Object by index, in OID order (0 .. mibSize()-1)
 */
const MibEntry& mibEntry(uint8_t index);

/**
 * @brief Exact match for GetRequest
 * @return nullptr if no object has this OID
 */
const MibEntry* mibFind(const Oid& oid);

/**
 * @brief First object whose OID sorts after the given one, for GetNextRequest
 * @return nullptr past the end of the MIB
 */
const MibEntry* mibNext(const Oid& oid);

/**
 * @brief true if oid lies under an object of the MIB (a scalar's OID without
 *        its ".0" instance), e.g. sysDescr.5 or sysDescr itself
 *
 * Distinguishes v2c noSuchInstance (object exists, wrong instance) from
 * noSuchObject (no such object at all).
 */
bool mibHasObject(const Oid& oid);
