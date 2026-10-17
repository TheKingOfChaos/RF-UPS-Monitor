#pragma once

/**
 * @file snmp_ber.h
 * @brief ASN.1 BER encoding and decoding for SNMP v1 (RFC 1157, X.690).
 *
 * Pure C++ with no Arduino dependency and no heap use, unit tested on the
 * host (`pio test -e native`).
 *
 * Only the subset SNMP v1 needs is supported: single-byte tags, definite
 * lengths up to 65535, and the types listed in BerTag. Decoding is strict and
 * every read is bounds checked: malformed or truncated input makes the reader
 * fail instead of reading past the buffer. Once a reader or writer has failed
 * it stays failed (sticky error), so a sequence of calls can be checked once
 * at the end.
 */

#include <stddef.h>
#include <stdint.h>

/// BER tags used by SNMP v1
namespace BerTag {
constexpr uint8_t Integer = 0x02;
constexpr uint8_t OctetString = 0x04;
constexpr uint8_t Null = 0x05;
constexpr uint8_t ObjectId = 0x06;
constexpr uint8_t Sequence = 0x30;
// SNMP application types (RFC 1155)
constexpr uint8_t IpAddress = 0x40;
constexpr uint8_t Counter32 = 0x41;
constexpr uint8_t Gauge32 = 0x42;
constexpr uint8_t TimeTicks = 0x43;
constexpr uint8_t Opaque = 0x44;
constexpr uint8_t Counter64 = 0x46;       ///< v2c only (RFC 2578)
// SNMP PDUs (context-specific, constructed)
constexpr uint8_t GetRequest = 0xA0;
constexpr uint8_t GetNextRequest = 0xA1;
constexpr uint8_t GetResponse = 0xA2;     ///< "Response" in v2c
constexpr uint8_t SetRequest = 0xA3;
constexpr uint8_t Trap = 0xA4;            ///< v1 Trap-PDU
constexpr uint8_t GetBulkRequest = 0xA5;  ///< v2c only
constexpr uint8_t InformRequest = 0xA6;   ///< v2c only
constexpr uint8_t SNMPv2Trap = 0xA7;      ///< v2c only
constexpr uint8_t Report = 0xA8;          ///< v3 only
// v2c exception values (context-specific, primitive, empty)
constexpr uint8_t NoSuchObject = 0x80;
constexpr uint8_t NoSuchInstance = 0x81;
constexpr uint8_t EndOfMibView = 0x82;
}  // namespace BerTag

/// Maximum number of arcs in an OID; longer OIDs are rejected
constexpr uint8_t OID_MAX_ARCS = 32;

/**
 * @brief An object identifier, e.g. 1.3.6.1.2.1.1.1.0
 */
struct Oid {
    uint32_t arcs[OID_MAX_ARCS];
    uint8_t length;  ///< Number of valid arcs
};

/**
 * @brief Parse dotted notation such as "1.3.6.1.2.1.1.1.0" (a leading '.' is allowed)
 *
 * Requires at least two arcs, first arc 0-2, second arc below 40 when the
 * first is 0 or 1, every arc fitting in 32 bits.
 *
 * @return true on success; out is untouched on failure
 */
bool oidFromString(const char* text, Oid& out);

/**
 * @brief Format as dotted notation without a leading '.'
 * @return Characters written excluding NUL, or 0 if the buffer is too small
 */
size_t oidToString(const Oid& oid, char* buf, size_t size);

/**
 * @brief Lexicographic comparison as used by GetNextRequest
 * @return <0 if a sorts before b, 0 if equal, >0 if after. A prefix sorts first.
 */
int oidCompare(const Oid& a, const Oid& b);

/**
 * @brief true if oid starts with all arcs of prefix
 */
bool oidStartsWith(const Oid& oid, const Oid& prefix);

/**
 * @brief Bounds-checked BER decoder over a byte buffer
 *
 * A reader covers one region. Reading a SEQUENCE or PDU returns a child
 * reader for its contents, so nesting needs no extra state.
 */
class BerReader {
public:
    BerReader() : _p(nullptr), _end(nullptr), _failed(true) {}
    BerReader(const uint8_t* data, size_t length)
        : _p(data), _end(data ? data + length : nullptr), _failed(data == nullptr) {}

    /// true once any read has failed
    bool failed() const { return _failed; }

    /// true if all bytes were consumed without error
    bool atEnd() const { return !_failed && _p == _end; }

    /// Bytes left to read
    size_t remaining() const { return _failed ? 0 : (size_t)(_end - _p); }

    /**
     * @brief Tag of the next element without consuming it
     * @return false at the end or after a failure
     */
    bool peekTag(uint8_t& tag) const;

    /**
     * @brief Read any element: tag, length and a reader over its contents
     */
    bool readTlv(uint8_t& tag, BerReader& contents);

    /**
     * @brief Read a constructed element with the expected tag (SEQUENCE or a PDU)
     */
    bool readSequence(uint8_t expectedTag, BerReader& contents);

    /**
     * @brief Read an INTEGER that fits in int32_t (1-4 content bytes)
     */
    bool readInteger(int32_t& value);

    /**
     * @brief Read an unsigned 32-bit value with the given tag (Counter32, Gauge32, TimeTicks)
     *
     * Accepts 1-5 content bytes; a fifth byte is only allowed as a leading 0x00.
     * Negative encodings are rejected.
     */
    bool readUnsigned(uint8_t expectedTag, uint32_t& value);

    /**
     * @brief Read an unsigned 64-bit value (Counter64): 1-9 content bytes, a ninth only as leading 0x00
     */
    bool readUnsigned64(uint8_t expectedTag, uint64_t& value);

    /**
     * @brief Read an OCTET STRING (or another primitive tag), returning a pointer into the buffer
     */
    bool readOctetString(const uint8_t*& data, size_t& length, uint8_t expectedTag = BerTag::OctetString);

    /// Read a NULL (content length must be 0)
    bool readNull();

    /// Read an OBJECT IDENTIFIER
    bool readOid(Oid& oid);

    /// Skip one element of any type
    bool skip();

    /**
     * @brief Read one complete element (tag, length and contents) as raw bytes
     * @param start Receives a pointer to the element's tag byte
     * @param length Receives the element's total encoded length
     */
    bool readRaw(const uint8_t*& start, size_t& length);

private:
    bool fail() { _failed = true; return false; }
    bool readHeader(uint8_t& tag, size_t& length);

    const uint8_t* _p;
    const uint8_t* _end;
    bool _failed;
};

/**
 * @brief BER encoder into a fixed buffer
 *
 * Sequences are written with beginSequence()/endSequence(); the length is
 * filled in at the end using the shortest form, moving the contents if the
 * length needs more than one byte.
 */
class BerWriter {
public:
    /// Maximum nesting depth of beginSequence()
    static constexpr uint8_t MAX_DEPTH = 8;

    BerWriter(uint8_t* buffer, size_t capacity)
        : _buf(buffer), _cap(buffer ? capacity : 0), _len(0), _depth(0), _failed(buffer == nullptr) {}

    /// true once any write has failed (buffer full, bad nesting, invalid value)
    bool failed() const { return _failed; }

    /// Bytes written so far
    size_t length() const { return _len; }

    /// Start a constructed element; contents follow until endSequence()
    bool beginSequence(uint8_t tag = BerTag::Sequence);

    /// Close the innermost open sequence and fill in its length
    bool endSequence();

    /// true if a sequence is still open
    bool hasOpenSequence() const { return _depth > 0; }

    bool writeInteger(int32_t value);
    bool writeUnsigned(uint8_t tag, uint32_t value);
    bool writeUnsigned64(uint8_t tag, uint64_t value);
    bool writeOctetString(const uint8_t* data, size_t length, uint8_t tag = BerTag::OctetString);
    bool writeOctetString(const char* text);
    bool writeNull();
    bool writeOid(const Oid& oid);

    /// Write an IpAddress (application tag 0x40, 4 bytes)
    bool writeIpAddress(const uint8_t addr[4]);

    /// Copy already-encoded bytes (e.g. an element from BerReader::readRaw)
    bool writeRaw(const uint8_t* data, size_t length);

    /// A position to return to with rollback()
    struct Mark {
        size_t length;
        uint8_t depth;
    };

    /// Remember the current position
    Mark mark() const { return {_len, _depth}; }

    /**
     * @brief Discard everything written after the mark, including a failure
     *
     * Used to try adding an element and drop it again if it does not fit
     * (e.g. truncating a GetBulk response). The mark must come from this
     * writer, taken while it had not failed.
     */
    void rollback(const Mark& m) {
        _len = m.length;
        _depth = m.depth;
        _failed = false;
    }

private:
    bool fail() { _failed = true; return false; }
    bool put(uint8_t b);
    bool putHeader(uint8_t tag, size_t length);

    uint8_t* _buf;
    size_t _cap;
    size_t _len;
    size_t _open[MAX_DEPTH];  // Offset of each open sequence's length byte
    uint8_t _depth;
    bool _failed;
};
