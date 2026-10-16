#include "snmp_message.h"

#include <string.h>

// ---- SnmpValue ----

static SnmpValue blankValue(uint8_t type) {
    SnmpValue v;
    memset(&v, 0, sizeof(v));
    v.type = type;
    return v;
}

SnmpValue SnmpValue::makeInteger(int32_t i) {
    SnmpValue v = blankValue(BerTag::Integer);
    v.integer = i;
    return v;
}

SnmpValue SnmpValue::makeUnsigned(uint8_t type, uint32_t u) {
    SnmpValue v = blankValue(type);
    v.unsigned32 = u;
    return v;
}

SnmpValue SnmpValue::makeString(const char* s) {
    SnmpValue v = blankValue(BerTag::OctetString);
    v.data = (const uint8_t*)(s ? s : "");
    v.length = strlen((const char*)v.data);
    return v;
}

SnmpValue SnmpValue::makeOid(const Oid& o) {
    SnmpValue v = blankValue(BerTag::ObjectId);
    v.oid = o;
    return v;
}

SnmpValue SnmpValue::makeNull() {
    return blankValue(BerTag::Null);
}

bool snmpWriteValue(BerWriter& w, const SnmpValue& value) {
    switch (value.type) {
    case BerTag::Integer:     return w.writeInteger(value.integer);
    case BerTag::Counter32:
    case BerTag::Gauge32:
    case BerTag::TimeTicks:   return w.writeUnsigned(value.type, value.unsigned32);
    case BerTag::OctetString: return w.writeOctetString(value.data, value.length);
    case BerTag::ObjectId:    return w.writeOid(value.oid);
    case BerTag::IpAddress:   return w.writeIpAddress(value.ip);
    case BerTag::Null:        return w.writeNull();
    default:                  return false;  // Unknown type is a programming error
    }
}

// ---- Parsing ----

SnmpParseResult snmpParseRequest(const uint8_t* packet, size_t length, SnmpRequest& out) {
    BerReader r(packet, length);
    BerReader msg;
    if (!r.readSequence(BerTag::Sequence, msg) || !r.atEnd()) return SnmpParseResult::Malformed;

    int32_t version;
    if (!msg.readInteger(version)) return SnmpParseResult::Malformed;
    if (version != SNMP_VERSION_1) return SnmpParseResult::BadVersion;
    out.version = version;

    if (!msg.readOctetString(out.community, out.communityLength)) return SnmpParseResult::Malformed;

    // Check the PDU type before parsing it: a Trap has a different layout
    uint8_t tag;
    if (!msg.peekTag(tag)) return SnmpParseResult::Malformed;
    if (tag != BerTag::GetRequest && tag != BerTag::GetNextRequest && tag != BerTag::SetRequest) {
        return SnmpParseResult::UnsupportedPdu;
    }
    out.pduType = tag;

    BerReader pdu, list;
    int32_t errorStatus, errorIndex;
    if (!msg.readSequence(tag, pdu) || !msg.atEnd()) return SnmpParseResult::Malformed;
    if (!pdu.readInteger(out.requestId) || !pdu.readInteger(errorStatus) ||
        !pdu.readInteger(errorIndex) || !pdu.readSequence(BerTag::Sequence, list) || !pdu.atEnd()) {
        return SnmpParseResult::Malformed;
    }

    out.varbindCount = 0;
    while (list.remaining() > 0) {
        if (out.varbindCount >= SNMP_MAX_VARBINDS) return SnmpParseResult::TooManyVarbinds;
        uint8_t i = out.varbindCount;
        BerReader vb;
        if (!list.readSequence(BerTag::Sequence, vb) || !vb.readOid(out.names[i]) ||
            !vb.readRaw(out.values[i], out.valueLengths[i]) || !vb.atEnd()) {
            return SnmpParseResult::Malformed;
        }
        out.varbindCount++;
    }
    if (!list.atEnd()) return SnmpParseResult::Malformed;

    return SnmpParseResult::Ok;
}

// ---- Responses ----

/**
 * @brief Write the message and PDU headers up to and including the opening of the VarBindList
 */
static void beginResponse(BerWriter& w, const SnmpRequest& req, SnmpError error, uint8_t errorIndex) {
    w.beginSequence();
    w.writeInteger(SNMP_VERSION_1);
    w.writeOctetString(req.community, req.communityLength);
    w.beginSequence(BerTag::GetResponse);
    w.writeInteger(req.requestId);
    w.writeInteger((int32_t)error);
    w.writeInteger(errorIndex);
    w.beginSequence();
}

/**
 * @brief Close VarBindList, PDU and message
 */
static size_t endResponse(BerWriter& w) {
    w.endSequence();
    w.endSequence();
    w.endSequence();
    return w.failed() || w.hasOpenSequence() ? 0 : w.length();
}

size_t snmpBuildErrorResponse(const SnmpRequest& req, SnmpError error, uint8_t errorIndex,
                              uint8_t* out, size_t capacity) {
    BerWriter w(out, capacity);
    beginResponse(w, req, error, errorIndex);
    for (uint8_t i = 0; i < req.varbindCount; i++) {
        w.beginSequence();
        w.writeOid(req.names[i]);
        w.writeRaw(req.values[i], req.valueLengths[i]);
        w.endSequence();
    }
    return endResponse(w);
}

SnmpResponseBuilder::SnmpResponseBuilder(uint8_t* out, size_t capacity, const SnmpRequest& req)
    : _w(out, capacity) {
    beginResponse(_w, req, SnmpError::NoError, 0);
}

bool SnmpResponseBuilder::add(const Oid& name, const SnmpValue& value) {
    _w.beginSequence();
    _w.writeOid(name);
    if (!snmpWriteValue(_w, value)) return false;  // Leaves a sequence open, so finish() fails too
    _w.endSequence();
    return !_w.failed();
}

size_t SnmpResponseBuilder::finish() {
    return endResponse(_w);
}
