#include "snmp_trap.h"

#include <string.h>

/// sysObjectID / enterprise of this device: 1.3.6.1.4.1.63050
static Oid enterpriseOid() {
    Oid o = {{1, 3, 6, 1, 4, 1, ENTERPRISE_NUMBER}, 7};
    return o;
}

size_t snmpBuildTrap(const SnmpTrap& trap, uint8_t* out, size_t capacity) {
    if (trap.community == nullptr || trap.varbindCount > SNMP_TRAP_MAX_VARBINDS) return 0;

    BerWriter w(out, capacity);
    w.beginSequence();
    w.writeInteger(SNMP_VERSION_1);
    w.writeOctetString(trap.community);
    w.beginSequence(BerTag::Trap);
    w.writeOid(trap.enterprise);
    w.writeIpAddress(trap.agentAddr);
    w.writeInteger((int32_t)trap.generic);
    w.writeInteger(trap.specific);
    w.writeUnsigned(BerTag::TimeTicks, trap.timestamp);
    w.beginSequence();
    for (uint8_t i = 0; i < trap.varbindCount; i++) {
        w.beginSequence();
        w.writeOid(trap.names[i]);
        if (!snmpWriteValue(w, trap.values[i])) return 0;
        w.endSequence();
    }
    w.endSequence();
    w.endSequence();
    w.endSequence();
    return w.failed() || w.hasOpenSequence() ? 0 : w.length();
}

/// A trap with no variable bindings
static size_t buildGenericTrap(SnmpGenericTrap generic, const char* community, const uint8_t agentAddr[4],
                               uint32_t uptimeTicks, uint8_t* out, size_t capacity) {
    SnmpTrap t;
    memset(&t, 0, sizeof(t));
    t.community = community;
    t.enterprise = enterpriseOid();
    memcpy(t.agentAddr, agentAddr, 4);
    t.generic = generic;
    t.specific = 0;
    t.timestamp = uptimeTicks;
    return snmpBuildTrap(t, out, capacity);
}

size_t snmpBuildColdStartTrap(const char* community, const uint8_t agentAddr[4], uint32_t uptimeTicks,
                              uint8_t* out, size_t capacity) {
    return buildGenericTrap(SnmpGenericTrap::ColdStart, community, agentAddr, uptimeTicks, out, capacity);
}

size_t snmpBuildAuthFailureTrap(const char* community, const uint8_t agentAddr[4], uint32_t uptimeTicks,
                                uint8_t* out, size_t capacity) {
    return buildGenericTrap(SnmpGenericTrap::AuthenticationFailure, community, agentAddr, uptimeTicks, out,
                            capacity);
}

size_t snmpBuildMainsTrap(const char* community, const uint8_t agentAddr[4], const SnmpDeviceState& state,
                          uint8_t* out, size_t capacity) {
    SnmpTrap t;
    memset(&t, 0, sizeof(t));
    t.community = community;
    t.enterprise = enterpriseOid();
    memcpy(t.agentAddr, agentAddr, 4);
    t.generic = SnmpGenericTrap::EnterpriseSpecific;
    t.specific = (int32_t)(state.mainsPresent ? MainsTrap::PowerRestored : MainsTrap::PowerLost);
    t.timestamp = state.mainsLastChangeTicks;

    // mainsPowerStatus, mainsPowerChanges, mainsPowerLastChange, read through the MIB
    for (uint32_t n = 1; n <= 3; n++) {
        Oid name = {{1, 3, 6, 1, 4, 1, ENTERPRISE_NUMBER, n, 0}, 9};
        const MibEntry* e = mibFind(name);
        if (e == nullptr) return 0;
        t.names[t.varbindCount] = name;
        t.values[t.varbindCount] = e->get(state);
        t.varbindCount++;
    }
    return snmpBuildTrap(t, out, capacity);
}

bool TrapThrottle::allow(uint32_t nowMs) {
    if (_sentOnce && nowMs - _lastMs < _intervalMs) {
        _suppressed++;
        return false;
    }
    _sentOnce = true;
    _lastMs = nowMs;
    _suppressed = 0;
    return true;
}
