#include "snmp_mib.h"

// ---- getters ----

static SnmpValue getSysDescr(const SnmpDeviceState& s) { return SnmpValue::makeString(s.sysDescr); }
static SnmpValue getSysContact(const SnmpDeviceState& s) { return SnmpValue::makeString(s.sysContact); }
static SnmpValue getSysName(const SnmpDeviceState& s) { return SnmpValue::makeString(s.sysName); }
static SnmpValue getSysLocation(const SnmpDeviceState& s) { return SnmpValue::makeString(s.sysLocation); }

static SnmpValue getSysObjectId(const SnmpDeviceState&) {
    Oid o = {{1, 3, 6, 1, 4, 1, ENTERPRISE_NUMBER}, 7};
    return SnmpValue::makeOid(o);
}

static SnmpValue getSysUpTime(const SnmpDeviceState& s) {
    return SnmpValue::makeUnsigned(BerTag::TimeTicks, s.uptimeTicks);
}

static SnmpValue getSysServices(const SnmpDeviceState&) {
    // RFC 1213: layers 4 (end-to-end, 8) + 7 (applications, 64)
    return SnmpValue::makeInteger(72);
}

static SnmpValue getMainsStatus(const SnmpDeviceState& s) {
    return SnmpValue::makeInteger(s.mainsPresent ? 1 : 0);
}

static SnmpValue getMainsChanges(const SnmpDeviceState& s) {
    return SnmpValue::makeUnsigned(BerTag::Counter32, s.mainsChanges);
}

static SnmpValue getMainsLastChange(const SnmpDeviceState& s) {
    return SnmpValue::makeUnsigned(BerTag::TimeTicks, s.mainsLastChangeTicks);
}

// ---- table, must be sorted by OID (checked by tests) ----

#define SYSTEM(n) {{1, 3, 6, 1, 2, 1, 1, n, 0}, 9}
#define PRIVATE(n) {{1, 3, 6, 1, 4, 1, ENTERPRISE_NUMBER, n, 0}, 9}

static const MibEntry MIB[] = {
    {SYSTEM(1), getSysDescr},
    {SYSTEM(2), getSysObjectId},
    {SYSTEM(3), getSysUpTime},
    {SYSTEM(4), getSysContact},
    {SYSTEM(5), getSysName},
    {SYSTEM(6), getSysLocation},
    {SYSTEM(7), getSysServices},
    {PRIVATE(1), getMainsStatus},
    {PRIVATE(2), getMainsChanges},
    {PRIVATE(3), getMainsLastChange},
};

static const uint8_t MIB_SIZE = sizeof(MIB) / sizeof(MIB[0]);

uint8_t mibSize() {
    return MIB_SIZE;
}

const MibEntry& mibEntry(uint8_t index) {
    return MIB[index < MIB_SIZE ? index : MIB_SIZE - 1];
}

const MibEntry* mibFind(const Oid& oid) {
    for (uint8_t i = 0; i < MIB_SIZE; i++) {
        if (oidCompare(MIB[i].oid, oid) == 0) return &MIB[i];
    }
    return nullptr;
}

bool mibHasObject(const Oid& oid) {
    for (uint8_t i = 0; i < MIB_SIZE; i++) {
        // All objects are scalars: the object OID is the entry without its last arc (.0)
        Oid object = MIB[i].oid;
        object.length--;
        if (oidStartsWith(oid, object)) return true;
    }
    return false;
}

const MibEntry* mibNext(const Oid& oid) {
    for (uint8_t i = 0; i < MIB_SIZE; i++) {
        if (oidCompare(MIB[i].oid, oid) > 0) return &MIB[i];
    }
    return nullptr;
}
