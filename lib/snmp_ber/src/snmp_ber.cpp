#include "snmp_ber.h"

#include <string.h>

// ---------------------------------------------------------------------------
// OID helpers
// ---------------------------------------------------------------------------

/**
 * @brief Check the X.690 rules for the first two arcs and the arc count
 */
static bool oidIsEncodable(const Oid& oid) {
    if (oid.length < 2 || oid.length > OID_MAX_ARCS) return false;
    if (oid.arcs[0] > 2) return false;
    if (oid.arcs[0] < 2 && oid.arcs[1] >= 40) return false;
    // First sub-identifier is arc0 * 40 + arc1 and must fit in 32 bits
    if (oid.arcs[0] == 2 && oid.arcs[1] > 0xFFFFFFFFu - 80) return false;
    return true;
}

bool oidFromString(const char* text, Oid& out) {
    if (text == nullptr) return false;
    const char* p = text;
    if (*p == '.') p++;

    Oid oid;
    oid.length = 0;
    for (;;) {
        if (*p < '0' || *p > '9') return false;  // Empty arc, sign or other character
        uint64_t value = 0;
        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (uint64_t)(*p - '0');
            if (value > 0xFFFFFFFFu) return false;
            p++;
        }
        if (oid.length >= OID_MAX_ARCS) return false;
        oid.arcs[oid.length++] = (uint32_t)value;

        if (*p == '\0') break;
        if (*p != '.') return false;
        p++;
    }

    if (!oidIsEncodable(oid)) return false;
    out = oid;
    return true;
}

size_t oidToString(const Oid& oid, char* buf, size_t size) {
    if (buf == nullptr || size == 0) return 0;
    size_t pos = 0;
    for (uint8_t i = 0; i < oid.length; i++) {
        // Digits of this arc, least significant first
        char digits[10];
        int n = 0;
        uint32_t v = oid.arcs[i];
        do {
            digits[n++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);

        size_t need = (size_t)n + (i > 0 ? 1 : 0);
        if (pos + need + 1 > size) return 0;  // +1 for the NUL
        if (i > 0) buf[pos++] = '.';
        while (n) buf[pos++] = digits[--n];
    }
    buf[pos] = '\0';
    return pos;
}

int oidCompare(const Oid& a, const Oid& b) {
    uint8_t n = a.length < b.length ? a.length : b.length;
    for (uint8_t i = 0; i < n; i++) {
        if (a.arcs[i] != b.arcs[i]) return a.arcs[i] < b.arcs[i] ? -1 : 1;
    }
    if (a.length == b.length) return 0;
    return a.length < b.length ? -1 : 1;
}

bool oidStartsWith(const Oid& oid, const Oid& prefix) {
    if (prefix.length > oid.length) return false;
    for (uint8_t i = 0; i < prefix.length; i++) {
        if (oid.arcs[i] != prefix.arcs[i]) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// BerReader
// ---------------------------------------------------------------------------

bool BerReader::readHeader(uint8_t& tag, size_t& length) {
    if (_failed || _end - _p < 2) return fail();

    tag = _p[0];
    if ((tag & 0x1F) == 0x1F) return fail();  // Multi-byte tags are not used by SNMP

    uint8_t first = _p[1];
    const uint8_t* q = _p + 2;
    if (first < 0x80) {
        length = first;
    } else if (first == 0x81) {
        if (_end - q < 1) return fail();
        length = q[0];
        q += 1;
    } else if (first == 0x82) {
        if (_end - q < 2) return fail();
        length = ((size_t)q[0] << 8) | q[1];
        q += 2;
    } else {
        // 0x80 is the indefinite form, 0x83+ would exceed any SNMP message
        return fail();
    }

    if ((size_t)(_end - q) < length) return fail();
    _p = q;
    return true;
}

bool BerReader::peekTag(uint8_t& tag) const {
    if (_failed || _p >= _end) return false;
    tag = *_p;
    return true;
}

bool BerReader::readTlv(uint8_t& tag, BerReader& contents) {
    size_t length;
    if (!readHeader(tag, length)) return false;
    contents = BerReader(_p, length);
    _p += length;
    return true;
}

bool BerReader::readSequence(uint8_t expectedTag, BerReader& contents) {
    uint8_t tag;
    if (!readTlv(tag, contents)) return false;
    if (tag != expectedTag) return fail();
    return true;
}

bool BerReader::readInteger(int32_t& value) {
    uint8_t tag;
    BerReader c;
    if (!readTlv(tag, c)) return false;
    size_t n = c.remaining();
    if (tag != BerTag::Integer || n < 1 || n > 4) return fail();

    // Sign-extend from the first byte
    uint32_t v = (c._p[0] & 0x80) ? 0xFFFFFFFFu : 0;
    for (size_t i = 0; i < n; i++) v = (v << 8) | c._p[i];
    value = (int32_t)v;
    return true;
}

bool BerReader::readUnsigned(uint8_t expectedTag, uint32_t& value) {
    uint8_t tag;
    BerReader c;
    if (!readTlv(tag, c)) return false;
    size_t n = c.remaining();
    if (tag != expectedTag || n < 1 || n > 5) return fail();
    if (c._p[0] & 0x80) return fail();                 // Negative
    if (n == 5 && c._p[0] != 0x00) return fail();      // Would exceed 32 bits

    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) v = (v << 8) | c._p[i];
    value = v;
    return true;
}

bool BerReader::readOctetString(const uint8_t*& data, size_t& length, uint8_t expectedTag) {
    uint8_t tag;
    BerReader c;
    if (!readTlv(tag, c)) return false;
    if (tag != expectedTag) return fail();
    data = c._p;
    length = c.remaining();
    return true;
}

bool BerReader::readNull() {
    uint8_t tag;
    BerReader c;
    if (!readTlv(tag, c)) return false;
    if (tag != BerTag::Null || c.remaining() != 0) return fail();
    return true;
}

bool BerReader::readOid(Oid& oid) {
    uint8_t tag;
    BerReader c;
    if (!readTlv(tag, c)) return false;
    if (tag != BerTag::ObjectId || c.remaining() == 0) return fail();

    Oid result;
    result.length = 0;
    const uint8_t* p = c._p;
    const uint8_t* end = c._end;
    bool first = true;

    while (p < end) {
        // One base-128 sub-identifier; a leading 0x80 byte is not minimal
        if (*p == 0x80) return fail();
        uint64_t v = 0;
        for (;;) {
            if (p >= end) return fail();               // Continuation bit on the last byte
            uint8_t b = *p++;
            v = (v << 7) | (b & 0x7F);
            if (v > 0xFFFFFFFFu) return fail();
            if ((b & 0x80) == 0) break;
        }

        if (first) {
            // First sub-identifier encodes the first two arcs
            if (result.length + 2 > OID_MAX_ARCS) return fail();
            uint32_t sub = (uint32_t)v;
            uint32_t a0 = sub < 40 ? 0 : (sub < 80 ? 1 : 2);
            result.arcs[result.length++] = a0;
            result.arcs[result.length++] = sub - a0 * 40;
            first = false;
        } else {
            if (result.length >= OID_MAX_ARCS) return fail();
            result.arcs[result.length++] = (uint32_t)v;
        }
    }

    oid = result;
    return true;
}

bool BerReader::skip() {
    uint8_t tag;
    BerReader c;
    return readTlv(tag, c);
}

bool BerReader::readRaw(const uint8_t*& start, size_t& length) {
    const uint8_t* begin = _p;
    if (!skip()) return false;
    start = begin;
    length = (size_t)(_p - begin);
    return true;
}

// ---------------------------------------------------------------------------
// BerWriter
// ---------------------------------------------------------------------------

bool BerWriter::put(uint8_t b) {
    if (_failed || _len >= _cap) return fail();
    _buf[_len++] = b;
    return true;
}

bool BerWriter::putHeader(uint8_t tag, size_t length) {
    if (!put(tag)) return false;
    if (length < 0x80) return put((uint8_t)length);
    if (length <= 0xFF) return put(0x81) && put((uint8_t)length);
    if (length <= 0xFFFF) return put(0x82) && put((uint8_t)(length >> 8)) && put((uint8_t)length);
    return fail();
}

bool BerWriter::beginSequence(uint8_t tag) {
    if (_failed || _depth >= MAX_DEPTH) return fail();
    if (!put(tag)) return false;
    _open[_depth++] = _len;
    return put(0);  // Length placeholder, fixed up in endSequence()
}

bool BerWriter::endSequence() {
    if (_failed || _depth == 0) return fail();
    size_t lenPos = _open[--_depth];
    size_t contentLen = _len - lenPos - 1;

    if (contentLen < 0x80) {
        _buf[lenPos] = (uint8_t)contentLen;
        return true;
    }

    // Long form: make room for 1 or 2 extra length bytes
    size_t extra = contentLen <= 0xFF ? 1 : 2;
    if (contentLen > 0xFFFF || _len + extra > _cap) return fail();
    memmove(_buf + lenPos + 1 + extra, _buf + lenPos + 1, contentLen);
    if (extra == 1) {
        _buf[lenPos] = 0x81;
        _buf[lenPos + 1] = (uint8_t)contentLen;
    } else {
        _buf[lenPos] = 0x82;
        _buf[lenPos + 1] = (uint8_t)(contentLen >> 8);
        _buf[lenPos + 2] = (uint8_t)contentLen;
    }
    _len += extra;
    return true;
}

bool BerWriter::writeInteger(int32_t value) {
    uint8_t bytes[4] = {(uint8_t)((uint32_t)value >> 24), (uint8_t)((uint32_t)value >> 16),
                        (uint8_t)((uint32_t)value >> 8), (uint8_t)value};
    // Drop leading bytes that only repeat the sign
    int start = 0;
    while (start < 3 && ((bytes[start] == 0x00 && !(bytes[start + 1] & 0x80)) ||
                         (bytes[start] == 0xFF && (bytes[start + 1] & 0x80)))) {
        start++;
    }
    if (!putHeader(BerTag::Integer, (size_t)(4 - start))) return false;
    for (int i = start; i < 4; i++) {
        if (!put(bytes[i])) return false;
    }
    return true;
}

bool BerWriter::writeUnsigned(uint8_t tag, uint32_t value) {
    uint8_t bytes[5] = {0x00, (uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8),
                        (uint8_t)value};
    // Keep a leading 0x00 only when the next byte has its top bit set
    int start = 0;
    while (start < 4 && bytes[start] == 0x00 && !(bytes[start + 1] & 0x80)) start++;
    if (!putHeader(tag, (size_t)(5 - start))) return false;
    for (int i = start; i < 5; i++) {
        if (!put(bytes[i])) return false;
    }
    return true;
}

bool BerWriter::writeOctetString(const uint8_t* data, size_t length, uint8_t tag) {
    if (data == nullptr && length > 0) return fail();
    if (!putHeader(tag, length)) return false;
    if (_len + length > _cap) return fail();
    if (length) memcpy(_buf + _len, data, length);
    _len += length;
    return true;
}

bool BerWriter::writeOctetString(const char* text) {
    if (text == nullptr) return fail();
    return writeOctetString((const uint8_t*)text, strlen(text));
}

bool BerWriter::writeNull() {
    return putHeader(BerTag::Null, 0);
}

bool BerWriter::writeOid(const Oid& oid) {
    if (_failed) return false;
    if (!oidIsEncodable(oid)) return fail();

    // Encode sub-identifiers into a scratch buffer to learn the length
    uint8_t enc[OID_MAX_ARCS * 5];
    size_t n = 0;
    for (uint8_t i = 1; i < oid.length; i++) {
        uint32_t v = (i == 1) ? oid.arcs[0] * 40 + oid.arcs[1] : oid.arcs[i];
        uint8_t tmp[5];
        int k = 0;
        do {
            tmp[k++] = (uint8_t)(v & 0x7F);
            v >>= 7;
        } while (v);
        while (k) {
            uint8_t b = tmp[--k];
            enc[n++] = k ? (uint8_t)(b | 0x80) : b;
        }
    }
    return writeOctetString(enc, n, BerTag::ObjectId);
}

bool BerWriter::writeIpAddress(const uint8_t addr[4]) {
    if (addr == nullptr) return fail();
    return writeOctetString(addr, 4, BerTag::IpAddress);
}

bool BerWriter::writeRaw(const uint8_t* data, size_t length) {
    if (_failed || (data == nullptr && length > 0)) return fail();
    if (_len + length > _cap) return fail();
    if (length) memcpy(_buf + _len, data, length);
    _len += length;
    return true;
}
