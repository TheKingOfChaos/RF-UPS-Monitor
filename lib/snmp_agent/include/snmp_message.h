#pragma once

/**
 * @file snmp_message.h
 * @brief SNMP v1 message parsing and response building (RFC 1157).
 *
 * Pure C++ on top of lib/snmp_ber, unit tested on the host.
 *
 *   Message ::= SEQUENCE { version INTEGER (0), community OCTET STRING, data PDU }
 *   PDU     ::= [tag] SEQUENCE { request-id, error-status, error-index, VarBindList }
 */

#include <snmp_ber.h>

/// Most variable bindings accepted in one request
constexpr uint8_t SNMP_MAX_VARBINDS = 16;

/// SNMP v1 version field value
constexpr int32_t SNMP_VERSION_1 = 0;

/// error-status values (RFC 1157 4.1.1)
enum class SnmpError : int32_t {
    NoError = 0,
    TooBig = 1,
    NoSuchName = 2,
    BadValue = 3,
    ReadOnly = 4,
    GenErr = 5
};

/// Why a request could not be parsed
enum class SnmpParseResult : uint8_t {
    Ok = 0,
    Malformed,         ///< Not valid BER / not an SNMP message
    BadVersion,        ///< Valid message but not SNMP v1 (e.g. v2c, v3)
    UnsupportedPdu,    ///< Response, trap or unknown PDU type
    TooManyVarbinds    ///< More than SNMP_MAX_VARBINDS
};

/**
 * @brief A parsed request. Pointers refer into the received packet.
 */
struct SnmpRequest {
    int32_t version;
    const uint8_t* community;
    size_t communityLength;
    uint8_t pduType;           ///< BerTag::GetRequest, GetNextRequest or SetRequest
    int32_t requestId;
    uint8_t varbindCount;
    Oid names[SNMP_MAX_VARBINDS];
    /// Encoded value TLV of each request varbind, echoed back in error responses
    const uint8_t* values[SNMP_MAX_VARBINDS];
    size_t valueLengths[SNMP_MAX_VARBINDS];
};

/**
 * @brief A value returned by the MIB
 */
struct SnmpValue {
    uint8_t type;              ///< BerTag of the value
    int32_t integer;           ///< Integer
    uint32_t unsigned32;       ///< Counter32, Gauge32, TimeTicks
    const uint8_t* data;       ///< OctetString (must stay valid until encoded)
    size_t length;
    Oid oid;                   ///< ObjectId
    uint8_t ip[4];             ///< IpAddress

    static SnmpValue makeInteger(int32_t v);
    static SnmpValue makeUnsigned(uint8_t type, uint32_t v);
    static SnmpValue makeString(const char* s);
    static SnmpValue makeOid(const Oid& o);
    static SnmpValue makeNull();
};

/**
 * @brief Parse an SNMP v1 request
 *
 * The version is checked before the PDU, so a v2c/v3 packet reports
 * BadVersion even if the rest would not parse as v1.
 */
SnmpParseResult snmpParseRequest(const uint8_t* packet, size_t length, SnmpRequest& out);

/**
 * @brief Encode one value
 */
bool snmpWriteValue(BerWriter& w, const SnmpValue& value);

/**
 * @brief Build a GetResponse whose varbinds are the request's own (used for errors)
 *
 * RFC 1157: on error the response carries the request's variable bindings
 * unchanged, with error-status and error-index set.
 *
 * @return Response length, or 0 if it does not fit
 */
size_t snmpBuildErrorResponse(const SnmpRequest& req, SnmpError error, uint8_t errorIndex,
                              uint8_t* out, size_t capacity);

/**
 * @brief Incrementally builds a successful GetResponse
 *
 *   SnmpResponseBuilder b(buf, sizeof(buf), req);
 *   b.add(name, value); ...
 *   size_t len = b.finish();
 */
class SnmpResponseBuilder {
public:
    SnmpResponseBuilder(uint8_t* out, size_t capacity, const SnmpRequest& req);

    /// Append a variable binding; false if it does not fit (response is then tooBig)
    bool add(const Oid& name, const SnmpValue& value);

    /// Close the message; returns its length, or 0 on failure
    size_t finish();

private:
    BerWriter _w;
};
