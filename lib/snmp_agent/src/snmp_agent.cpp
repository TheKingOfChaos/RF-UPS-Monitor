#include "snmp_agent.h"

#include <string.h>

bool SnmpAgent::communityMatches(const SnmpRequest& req) const {
    if (_community == nullptr) return false;
    size_t len = strlen(_community);
    if (req.communityLength != len) return false;
    // Compare every byte so timing does not reveal how much matched
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) diff |= (uint8_t)(req.community[i] ^ (uint8_t)_community[i]);
    return diff == 0;
}

size_t SnmpAgent::respondError(const SnmpRequest& req, SnmpError error, uint8_t index, uint8_t* out,
                               size_t cap) {
    size_t len = snmpBuildErrorResponse(req, error, index, out, cap);
    if (len == 0 && error != SnmpError::TooBig) {
        // The request itself does not fit in the response buffer
        error = SnmpError::TooBig;
        len = snmpBuildErrorResponse(req, error, 0, out, cap);
    }
    if (len == 0) {
        _outcome = SnmpOutcome::NoRoom;
        return 0;
    }

    if (error == SnmpError::NoSuchName) _stats.outNoSuchName++;
    if (error == SnmpError::TooBig) _stats.outTooBig++;
    _stats.outResponses++;
    return len;
}

size_t SnmpAgent::handle(const uint8_t* request, size_t length, uint8_t* response, size_t capacity,
                         const SnmpDeviceState& state) {
    _stats.inPackets++;
    _outcome = SnmpOutcome::Responded;

    switch (snmpParseRequest(request, length, _req)) {
    case SnmpParseResult::Ok:
        break;
    case SnmpParseResult::Malformed:
        _stats.inParseErrors++;
        _outcome = SnmpOutcome::ParseError;
        return 0;
    case SnmpParseResult::BadVersion:
        _stats.inBadVersions++;
        _outcome = SnmpOutcome::BadVersion;
        return 0;
    case SnmpParseResult::UnsupportedPdu:
    case SnmpParseResult::TooManyVarbinds:
        _stats.inUnsupported++;
        _outcome = SnmpOutcome::Unsupported;
        return 0;
    }

    if (!communityMatches(_req)) {
        _stats.inBadCommunity++;
        _outcome = SnmpOutcome::BadCommunity;
        return 0;
    }

    if (_req.pduType == BerTag::SetRequest) {
        _stats.inSetRequests++;
        _outcome = SnmpOutcome::SetRejected;
        // Nothing is writable: RFC 1157 4.1.5 answers noSuchName for the first varbind
        if (_req.varbindCount == 0) return respondError(_req, SnmpError::NoError, 0, response, capacity);
        return respondError(_req, SnmpError::NoSuchName, 1, response, capacity);
    }

    bool getNext = _req.pduType == BerTag::GetNextRequest;
    if (getNext) {
        _stats.inGetNextRequests++;
    } else {
        _stats.inGetRequests++;
    }

    // Resolve every varbind first, so errors are reported before anything is built
    const MibEntry* entries[SNMP_MAX_VARBINDS];
    for (uint8_t i = 0; i < _req.varbindCount; i++) {
        entries[i] = getNext ? mibNext(_req.names[i]) : mibFind(_req.names[i]);
        if (entries[i] == nullptr) return respondError(_req, SnmpError::NoSuchName, i + 1, response, capacity);
    }

    SnmpResponseBuilder builder(response, capacity, _req);
    for (uint8_t i = 0; i < _req.varbindCount; i++) {
        if (!builder.add(entries[i]->oid, entries[i]->get(state))) {
            return respondError(_req, SnmpError::TooBig, 0, response, capacity);
        }
    }
    size_t len = builder.finish();
    if (len == 0) return respondError(_req, SnmpError::TooBig, 0, response, capacity);

    _stats.outResponses++;
    return len;
}
