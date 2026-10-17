// Host unit tests for lib/snmp_ber: BER encoding/decoding and OID handling for SNMP v1.
// Byte vectors marked "net-snmp" were captured from real snmpget/snmpgetnext requests.
// Run: pio test -e native -f native/test_snmp_ber

#include <unity.h>
#include <string.h>
#include <snmp_ber.h>

void setUp() {}
void tearDown() {}

// snmpget -v1 -c public <host> 1.3.6.1.2.1.1.1.0  (net-snmp 5.9)
static const uint8_t NETSNMP_GET_SYSDESCR[] = {
    0x30, 0x29, 0x02, 0x01, 0x00, 0x04, 0x06, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63,
    0xa0, 0x1c, 0x02, 0x04, 0x46, 0x74, 0x1e, 0x94, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00,
    0x30, 0x0e, 0x30, 0x0c, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00,
    0x05, 0x00};

// snmpget -v1 -c public <host> 1.3.6.1.2.1.1.3.0 1.3.6.1.4.1.63050.1.0  (net-snmp 5.9)
static const uint8_t NETSNMP_GET_TWO_VARBINDS[] = {
    0x30, 0x39, 0x02, 0x01, 0x00, 0x04, 0x06, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63,
    0xa0, 0x2c, 0x02, 0x04, 0x10, 0xd6, 0xf5, 0x95, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00,
    0x30, 0x1e, 0x30, 0x0c, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00,
    0x05, 0x00, 0x30, 0x0e, 0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x83, 0xec, 0x4a,
    0x01, 0x00, 0x05, 0x00};

// ---- helpers ----

static Oid oid(const char* text) {
    Oid o;
    TEST_ASSERT_TRUE_MESSAGE(oidFromString(text, o), text);
    return o;
}

static void assertOidEquals(const char* expected, const Oid& actual) {
    char buf[200];
    TEST_ASSERT_GREATER_THAN(0, oidToString(actual, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

/// Encode with fn into a buffer and compare to the expected bytes
#define ASSERT_ENCODES(expected, call)                                   \
    do {                                                                 \
        uint8_t buf_[512];                                               \
        BerWriter w_(buf_, sizeof(buf_));                                \
        TEST_ASSERT_TRUE(w_.call);                                       \
        TEST_ASSERT_FALSE(w_.failed());                                  \
        TEST_ASSERT_EQUAL_size_t(sizeof(expected), w_.length());         \
        TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf_, sizeof(expected));  \
    } while (0)

// ---- OID text ----

static void test_oid_from_string_parses_dotted_notation() {
    Oid o = oid("1.3.6.1.4.1.63050.1.0");
    TEST_ASSERT_EQUAL_UINT8(9, o.length);
    TEST_ASSERT_EQUAL_UINT32(63050, o.arcs[6]);
    assertOidEquals("1.3.6.1.4.1.63050.1.0", o);
}

static void test_oid_from_string_accepts_leading_dot_and_max_arc() {
    assertOidEquals("1.3.6.1", oid(".1.3.6.1"));
    assertOidEquals("1.3.4294967295", oid("1.3.4294967295"));
}

static void test_oid_from_string_rejects_invalid() {
    Oid o;
    const char* bad[] = {"", "1", "1.", "1..3", "1.3.", "a.b", "1.3.x", "3.1", "1.40", "0.40",
                         "1.3.4294967296", "1.3.-1", " 1.3", "1.3 ", nullptr};
    for (int i = 0; bad[i]; i++) {
        TEST_ASSERT_FALSE_MESSAGE(oidFromString(bad[i], o), bad[i]);
    }
    TEST_ASSERT_FALSE(oidFromString(nullptr, o));
}

static void test_oid_from_string_allows_large_second_arc_under_2() {
    // First arc 2 may have any second arc (e.g. 2.999)
    assertOidEquals("2.999.1", oid("2.999.1"));
}

static void test_oid_from_string_rejects_too_many_arcs() {
    char text[256] = "1.3";
    for (int i = 2; i < OID_MAX_ARCS; i++) strcat(text, ".1");
    Oid o;
    TEST_ASSERT_TRUE(oidFromString(text, o));      // exactly OID_MAX_ARCS
    strcat(text, ".1");
    TEST_ASSERT_FALSE(oidFromString(text, o));     // one more
}

static void test_oid_to_string_fails_when_buffer_too_small() {
    char buf[5];
    TEST_ASSERT_EQUAL(0, oidToString(oid("1.3.6.1.2"), buf, sizeof(buf)));
    char ok[10];
    TEST_ASSERT_EQUAL(9, oidToString(oid("1.3.6.1.2"), ok, sizeof(ok)));
}

// ---- OID ordering ----

static void test_oid_compare_is_lexicographic() {
    TEST_ASSERT_EQUAL(0, oidCompare(oid("1.3.6.1"), oid("1.3.6.1")));
    TEST_ASSERT_LESS_THAN(0, oidCompare(oid("1.3.6.1.2"), oid("1.3.6.1.4")));
    TEST_ASSERT_GREATER_THAN(0, oidCompare(oid("1.3.6.1.4"), oid("1.3.6.1.2.1")));
    // Numeric, not textual: 9 < 10
    TEST_ASSERT_LESS_THAN(0, oidCompare(oid("1.3.9"), oid("1.3.10")));
}

static void test_oid_prefix_sorts_first() {
    TEST_ASSERT_LESS_THAN(0, oidCompare(oid("1.3.6.1.2.1.1"), oid("1.3.6.1.2.1.1.1.0")));
    TEST_ASSERT_GREATER_THAN(0, oidCompare(oid("1.3.6.1.2.1.1.1.0"), oid("1.3.6.1.2.1.1")));
}

static void test_oid_compare_handles_arcs_above_int_max() {
    TEST_ASSERT_LESS_THAN(0, oidCompare(oid("1.3.1"), oid("1.3.4294967295")));
}

static void test_oid_starts_with() {
    TEST_ASSERT_TRUE(oidStartsWith(oid("1.3.6.1.4.1.63050.1.0"), oid("1.3.6.1.4.1.63050")));
    TEST_ASSERT_TRUE(oidStartsWith(oid("1.3.6.1"), oid("1.3.6.1")));
    TEST_ASSERT_FALSE(oidStartsWith(oid("1.3.6.1"), oid("1.3.6.1.2")));
    TEST_ASSERT_FALSE(oidStartsWith(oid("1.3.6.2.1"), oid("1.3.6.1")));
}

// ---- encoding: INTEGER ----

static void test_encode_integer_minimal_forms() {
    const uint8_t zero[] = {0x02, 0x01, 0x00};
    const uint8_t i127[] = {0x02, 0x01, 0x7F};
    const uint8_t i128[] = {0x02, 0x02, 0x00, 0x80};
    const uint8_t i256[] = {0x02, 0x02, 0x01, 0x00};
    const uint8_t m1[] = {0x02, 0x01, 0xFF};
    const uint8_t m128[] = {0x02, 0x01, 0x80};
    const uint8_t m129[] = {0x02, 0x02, 0xFF, 0x7F};
    const uint8_t max[] = {0x02, 0x04, 0x7F, 0xFF, 0xFF, 0xFF};
    const uint8_t min[] = {0x02, 0x04, 0x80, 0x00, 0x00, 0x00};
    ASSERT_ENCODES(zero, writeInteger(0));
    ASSERT_ENCODES(i127, writeInteger(127));
    ASSERT_ENCODES(i128, writeInteger(128));
    ASSERT_ENCODES(i256, writeInteger(256));
    ASSERT_ENCODES(m1, writeInteger(-1));
    ASSERT_ENCODES(m128, writeInteger(-128));
    ASSERT_ENCODES(m129, writeInteger(-129));
    ASSERT_ENCODES(max, writeInteger(INT32_MAX));
    ASSERT_ENCODES(min, writeInteger(INT32_MIN));
}

static void test_encode_unsigned_types() {
    const uint8_t ticks0[] = {0x43, 0x01, 0x00};
    const uint8_t counterMax[] = {0x41, 0x05, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t gauge128[] = {0x42, 0x02, 0x00, 0x80};
    const uint8_t ticks[] = {0x43, 0x03, 0x01, 0xE2, 0x40};  // 123456
    ASSERT_ENCODES(ticks0, writeUnsigned(BerTag::TimeTicks, 0));
    ASSERT_ENCODES(counterMax, writeUnsigned(BerTag::Counter32, 0xFFFFFFFFu));
    ASSERT_ENCODES(gauge128, writeUnsigned(BerTag::Gauge32, 128));
    ASSERT_ENCODES(ticks, writeUnsigned(BerTag::TimeTicks, 123456));
}

// ---- encoding: strings, NULL, OID, IpAddress ----

static void test_encode_octet_string_and_null() {
    const uint8_t pub[] = {0x04, 0x06, 'p', 'u', 'b', 'l', 'i', 'c'};
    const uint8_t empty[] = {0x04, 0x00};
    const uint8_t null[] = {0x05, 0x00};
    ASSERT_ENCODES(pub, writeOctetString("public"));
    ASSERT_ENCODES(empty, writeOctetString(""));
    ASSERT_ENCODES(null, writeNull());
}

static void test_encode_long_form_lengths() {
    uint8_t data[300];
    memset(data, 'x', sizeof(data));
    uint8_t buf[512];

    BerWriter w(buf, sizeof(buf));
    TEST_ASSERT_TRUE(w.writeOctetString(data, 127));
    TEST_ASSERT_EQUAL_HEX8(0x7F, buf[1]);                  // short form up to 127

    BerWriter w2(buf, sizeof(buf));
    TEST_ASSERT_TRUE(w2.writeOctetString(data, 200));
    const uint8_t h200[] = {0x04, 0x81, 0xC8};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(h200, buf, 3);
    TEST_ASSERT_EQUAL_size_t(203, w2.length());

    BerWriter w3(buf, sizeof(buf));
    TEST_ASSERT_TRUE(w3.writeOctetString(data, 300));
    const uint8_t h300[] = {0x04, 0x82, 0x01, 0x2C};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(h300, buf, 4);
}

static void test_encode_oids() {
    const uint8_t sysDescr[] = {0x06, 0x08, 0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00};
    const uint8_t mains[] = {0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x83, 0xec, 0x4a, 0x01, 0x00};
    const uint8_t zeroZero[] = {0x06, 0x01, 0x00};                  // 0.0
    const uint8_t big[] = {0x06, 0x06, 0x2b, 0x8F, 0xFF, 0xFF, 0xFF, 0x7F};  // 1.3.4294967295
    const uint8_t joint[] = {0x06, 0x02, 0x88, 0x37};               // 2.999
    ASSERT_ENCODES(sysDescr, writeOid(oid("1.3.6.1.2.1.1.1.0")));
    ASSERT_ENCODES(mains, writeOid(oid("1.3.6.1.4.1.63050.1.0")));
    ASSERT_ENCODES(zeroZero, writeOid(oid("0.0")));
    ASSERT_ENCODES(big, writeOid(oid("1.3.4294967295")));
    ASSERT_ENCODES(joint, writeOid(oid("2.999")));
}

static void test_encode_rejects_invalid_oid() {
    uint8_t buf[32];
    Oid o = oid("1.3");
    o.length = 1;
    BerWriter w(buf, sizeof(buf));
    TEST_ASSERT_FALSE(w.writeOid(o));
    TEST_ASSERT_TRUE(w.failed());
}

static void test_encode_ip_address() {
    const uint8_t ip[] = {0x40, 0x04, 10, 10, 6, 111};
    const uint8_t addr[4] = {10, 10, 6, 111};
    ASSERT_ENCODES(ip, writeIpAddress(addr));
}

// ---- encoding: sequences ----

static void test_encode_reproduces_netsnmp_request_exactly() {
    uint8_t buf[128];
    BerWriter w(buf, sizeof(buf));
    w.beginSequence();
    w.writeInteger(0);
    w.writeOctetString("public");
    w.beginSequence(BerTag::GetRequest);
    w.writeInteger(0x46741e94);
    w.writeInteger(0);
    w.writeInteger(0);
    w.beginSequence();
    w.beginSequence();
    w.writeOid(oid("1.3.6.1.2.1.1.1.0"));
    w.writeNull();
    w.endSequence();
    w.endSequence();
    w.endSequence();
    TEST_ASSERT_TRUE(w.endSequence());
    TEST_ASSERT_FALSE(w.failed());
    TEST_ASSERT_FALSE(w.hasOpenSequence());
    TEST_ASSERT_EQUAL_size_t(sizeof(NETSNMP_GET_SYSDESCR), w.length());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(NETSNMP_GET_SYSDESCR, buf, sizeof(NETSNMP_GET_SYSDESCR));
}

static void test_encode_nested_sequence_longer_than_127() {
    // Inner content grows past 127 bytes after the outer header was written:
    // both lengths must switch to long form and contents must move intact
    uint8_t data[150];
    for (int i = 0; i < 150; i++) data[i] = (uint8_t)i;
    uint8_t buf[256];
    BerWriter w(buf, sizeof(buf));
    w.beginSequence();
    w.beginSequence();
    w.writeOctetString(data, sizeof(data));
    w.endSequence();
    TEST_ASSERT_TRUE(w.endSequence());

    // Octet string 3 + 150 = 153 (0x99); inner sequence 3 + 153 = 156 (0x9C)
    // 30 81 9C | 30 81 99 | 04 81 96 | data
    const uint8_t head[] = {0x30, 0x81, 0x9C, 0x30, 0x81, 0x99, 0x04, 0x81, 0x96};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(head, buf, sizeof(head));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, buf + sizeof(head), sizeof(data));
    TEST_ASSERT_EQUAL_size_t(sizeof(head) + sizeof(data), w.length());
}

static void test_encode_empty_sequence() {
    const uint8_t empty[] = {0x30, 0x00};
    uint8_t buf[8];
    BerWriter w(buf, sizeof(buf));
    w.beginSequence();
    TEST_ASSERT_TRUE(w.endSequence());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(empty, buf, 2);
}

static void test_encode_fails_cleanly_when_buffer_full() {
    uint8_t buf[8];
    memset(buf, 0xEE, sizeof(buf));
    BerWriter w(buf, 6);
    TEST_ASSERT_TRUE(w.writeOctetString("abcd"));    // exactly 6 bytes
    TEST_ASSERT_FALSE(w.writeNull());
    TEST_ASSERT_TRUE(w.failed());
    TEST_ASSERT_FALSE(w.writeInteger(1));            // sticky
    TEST_ASSERT_EQUAL_HEX8(0xEE, buf[6]);            // nothing written past capacity
}

static void test_encode_fails_when_length_growth_does_not_fit() {
    // A sequence whose length needs two bytes must fail if the extra byte does not fit
    uint8_t data[130];
    memset(data, 'a', sizeof(data));
    uint8_t buf[2 + 3 + 130];
    BerWriter w(buf, sizeof(buf));
    w.beginSequence();
    TEST_ASSERT_TRUE(w.writeOctetString(data, sizeof(data)));  // fits: 2 + 133
    TEST_ASSERT_FALSE(w.endSequence());                        // outer needs 0x81 xx: one byte more
    TEST_ASSERT_TRUE(w.failed());
}

static void test_encode_rejects_unbalanced_sequences() {
    uint8_t buf[64];
    BerWriter w(buf, sizeof(buf));
    TEST_ASSERT_FALSE(w.endSequence());
    TEST_ASSERT_TRUE(w.failed());

    BerWriter deep(buf, sizeof(buf));
    for (int i = 0; i < BerWriter::MAX_DEPTH; i++) TEST_ASSERT_TRUE(deep.beginSequence());
    TEST_ASSERT_FALSE(deep.beginSequence());
}

static void test_writer_with_null_buffer_fails() {
    BerWriter w(nullptr, 100);
    TEST_ASSERT_TRUE(w.failed());
    TEST_ASSERT_FALSE(w.writeNull());
}

// ---- decoding: real requests ----

static void test_decode_netsnmp_get_request() {
    BerReader r(NETSNMP_GET_SYSDESCR, sizeof(NETSNMP_GET_SYSDESCR));
    BerReader msg, pdu, list, vb;
    int32_t version, requestId, errStatus, errIndex;
    const uint8_t* community;
    size_t communityLen;
    Oid name;

    TEST_ASSERT_TRUE(r.readSequence(BerTag::Sequence, msg));
    TEST_ASSERT_TRUE(r.atEnd());
    TEST_ASSERT_TRUE(msg.readInteger(version));
    TEST_ASSERT_EQUAL_INT32(0, version);
    TEST_ASSERT_TRUE(msg.readOctetString(community, communityLen));
    TEST_ASSERT_EQUAL_size_t(6, communityLen);
    TEST_ASSERT_EQUAL_MEMORY("public", community, 6);

    uint8_t pduTag;
    TEST_ASSERT_TRUE(msg.peekTag(pduTag));
    TEST_ASSERT_EQUAL_HEX8(BerTag::GetRequest, pduTag);
    TEST_ASSERT_TRUE(msg.readSequence(BerTag::GetRequest, pdu));
    TEST_ASSERT_TRUE(msg.atEnd());
    TEST_ASSERT_TRUE(pdu.readInteger(requestId));
    TEST_ASSERT_EQUAL_HEX32(0x46741e94, (uint32_t)requestId);
    TEST_ASSERT_TRUE(pdu.readInteger(errStatus));
    TEST_ASSERT_TRUE(pdu.readInteger(errIndex));
    TEST_ASSERT_TRUE(pdu.readSequence(BerTag::Sequence, list));
    TEST_ASSERT_TRUE(pdu.atEnd());
    TEST_ASSERT_TRUE(list.readSequence(BerTag::Sequence, vb));
    TEST_ASSERT_TRUE(list.atEnd());
    TEST_ASSERT_TRUE(vb.readOid(name));
    assertOidEquals("1.3.6.1.2.1.1.1.0", name);
    TEST_ASSERT_TRUE(vb.readNull());
    TEST_ASSERT_TRUE(vb.atEnd());
}

static void test_decode_netsnmp_request_with_two_varbinds() {
    BerReader r(NETSNMP_GET_TWO_VARBINDS, sizeof(NETSNMP_GET_TWO_VARBINDS));
    BerReader msg, pdu, list, vb;
    TEST_ASSERT_TRUE(r.readSequence(BerTag::Sequence, msg));
    TEST_ASSERT_TRUE(msg.skip());   // version
    TEST_ASSERT_TRUE(msg.skip());   // community
    TEST_ASSERT_TRUE(msg.readSequence(BerTag::GetRequest, pdu));
    TEST_ASSERT_TRUE(pdu.skip());
    TEST_ASSERT_TRUE(pdu.skip());
    TEST_ASSERT_TRUE(pdu.skip());
    TEST_ASSERT_TRUE(pdu.readSequence(BerTag::Sequence, list));

    const char* expected[] = {"1.3.6.1.2.1.1.3.0", "1.3.6.1.4.1.63050.1.0"};
    int count = 0;
    while (list.remaining() > 0) {
        Oid name;
        TEST_ASSERT_TRUE(list.readSequence(BerTag::Sequence, vb));
        TEST_ASSERT_TRUE(vb.readOid(name));
        TEST_ASSERT_TRUE(vb.readNull());
        TEST_ASSERT_TRUE(count < 2);
        assertOidEquals(expected[count], name);
        count++;
    }
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_TRUE(list.atEnd());
}

// ---- decoding: values and round trips ----

static void test_integer_round_trip_over_boundaries() {
    const int32_t values[] = {0, 1, -1, 127, 128, -128, -129, 255, 256, 32767, 32768, -32768, -32769,
                              8388607, 8388608, -8388608, -8388609, INT32_MAX, INT32_MIN};
    for (int32_t v : values) {
        uint8_t buf[8];
        BerWriter w(buf, sizeof(buf));
        TEST_ASSERT_TRUE(w.writeInteger(v));
        BerReader r(buf, w.length());
        int32_t out = 0;
        TEST_ASSERT_TRUE(r.readInteger(out));
        TEST_ASSERT_EQUAL_INT32(v, out);
        TEST_ASSERT_TRUE(r.atEnd());
    }
}

static void test_unsigned_round_trip() {
    const uint32_t values[] = {0, 1, 127, 128, 255, 256, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu};
    for (uint32_t v : values) {
        uint8_t buf[8];
        BerWriter w(buf, sizeof(buf));
        TEST_ASSERT_TRUE(w.writeUnsigned(BerTag::Counter32, v));
        BerReader r(buf, w.length());
        uint32_t out = 0;
        TEST_ASSERT_TRUE(r.readUnsigned(BerTag::Counter32, out));
        TEST_ASSERT_EQUAL_UINT32(v, out);
    }
}

static void test_oid_round_trip() {
    const char* oids[] = {"0.0", "1.3.6.1", "2.999.3", "1.3.6.1.4.1.63050.1.0",
                          "1.3.6.1.2.1.1.1.0", "1.3.4294967295.127.128.16383.16384", nullptr};
    for (int i = 0; oids[i]; i++) {
        uint8_t buf[64];
        BerWriter w(buf, sizeof(buf));
        TEST_ASSERT_TRUE(w.writeOid(oid(oids[i])));
        BerReader r(buf, w.length());
        Oid out;
        TEST_ASSERT_TRUE_MESSAGE(r.readOid(out), oids[i]);
        assertOidEquals(oids[i], out);
    }
}

static void test_decode_accepts_non_minimal_length_from_other_encoders() {
    // BER (unlike DER) allows a long-form length for small values
    const uint8_t data[] = {0x04, 0x81, 0x02, 'h', 'i'};
    BerReader r(data, sizeof(data));
    const uint8_t* s;
    size_t len;
    TEST_ASSERT_TRUE(r.readOctetString(s, len));
    TEST_ASSERT_EQUAL_size_t(2, len);
}

// ---- decoding: malformed input ----

static void test_decode_rejects_truncated_input() {
    // Every proper prefix of a valid message must fail, never read past the end
    for (size_t n = 0; n < sizeof(NETSNMP_GET_SYSDESCR); n++) {
        BerReader r(NETSNMP_GET_SYSDESCR, n);
        BerReader msg;
        bool ok = r.readSequence(BerTag::Sequence, msg);
        TEST_ASSERT_FALSE(ok);
        TEST_ASSERT_TRUE(r.failed());
    }
}

static void test_decode_rejects_length_beyond_buffer() {
    const uint8_t data[] = {0x04, 0x05, 'a', 'b'};
    BerReader r(data, sizeof(data));
    const uint8_t* s;
    size_t len;
    TEST_ASSERT_FALSE(r.readOctetString(s, len));
}

static void test_decode_rejects_unsupported_length_forms() {
    const uint8_t indefinite[] = {0x30, 0x80, 0x00, 0x00};
    const uint8_t threeByte[] = {0x04, 0x83, 0x00, 0x00, 0x01, 'a'};
    const uint8_t zeroLenOfLen[] = {0x04, 0x81};
    BerReader a(indefinite, sizeof(indefinite)), b(threeByte, sizeof(threeByte)),
        c(zeroLenOfLen, sizeof(zeroLenOfLen));
    BerReader seq;
    const uint8_t* s;
    size_t len;
    TEST_ASSERT_FALSE(a.readSequence(BerTag::Sequence, seq));
    TEST_ASSERT_FALSE(b.readOctetString(s, len));
    TEST_ASSERT_FALSE(c.readOctetString(s, len));
}

static void test_decode_rejects_multi_byte_tags() {
    const uint8_t data[] = {0x1F, 0x81, 0x00, 0x00};
    BerReader r(data, sizeof(data));
    uint8_t tag;
    BerReader contents;
    TEST_ASSERT_FALSE(r.readTlv(tag, contents));
}

static void test_decode_rejects_wrong_tag() {
    const uint8_t data[] = {0x04, 0x01, 0x00};
    BerReader r(data, sizeof(data));
    int32_t v;
    TEST_ASSERT_FALSE(r.readInteger(v));
    TEST_ASSERT_TRUE(r.failed());
}

static void test_decode_rejects_bad_integers() {
    const uint8_t empty[] = {0x02, 0x00};
    const uint8_t tooLong[] = {0x02, 0x05, 0x00, 0x80, 0x00, 0x00, 0x00};
    BerReader a(empty, sizeof(empty)), b(tooLong, sizeof(tooLong));
    int32_t v;
    TEST_ASSERT_FALSE(a.readInteger(v));
    TEST_ASSERT_FALSE(b.readInteger(v));
}

static void test_decode_rejects_bad_unsigned() {
    const uint8_t negative[] = {0x41, 0x01, 0x80};
    const uint8_t fiveNonZero[] = {0x41, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00};
    const uint8_t six[] = {0x41, 0x06, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t wrongTag[] = {0x42, 0x01, 0x01};
    uint32_t v;
    BerReader a(negative, sizeof(negative)), b(fiveNonZero, sizeof(fiveNonZero)),
        c(six, sizeof(six)), d(wrongTag, sizeof(wrongTag));
    TEST_ASSERT_FALSE(a.readUnsigned(BerTag::Counter32, v));
    TEST_ASSERT_FALSE(b.readUnsigned(BerTag::Counter32, v));
    TEST_ASSERT_FALSE(c.readUnsigned(BerTag::Counter32, v));
    TEST_ASSERT_FALSE(d.readUnsigned(BerTag::Counter32, v));
}

static void test_decode_rejects_null_with_content() {
    const uint8_t data[] = {0x05, 0x01, 0x00};
    BerReader r(data, sizeof(data));
    TEST_ASSERT_FALSE(r.readNull());
}

static void test_decode_rejects_bad_oids() {
    const uint8_t empty[] = {0x06, 0x00};
    const uint8_t nonMinimal[] = {0x06, 0x03, 0x2b, 0x80, 0x01};       // 0x80 padding in an arc
    const uint8_t unterminated[] = {0x06, 0x02, 0x2b, 0x86};           // continuation bit on last byte
    const uint8_t overflow[] = {0x06, 0x07, 0x2b, 0x90, 0x80, 0x80, 0x80, 0x80, 0x00};  // > 32 bits
    Oid o;
    BerReader a(empty, sizeof(empty)), b(nonMinimal, sizeof(nonMinimal)),
        c(unterminated, sizeof(unterminated)), d(overflow, sizeof(overflow));
    TEST_ASSERT_FALSE(a.readOid(o));
    TEST_ASSERT_FALSE(b.readOid(o));
    TEST_ASSERT_FALSE(c.readOid(o));
    TEST_ASSERT_FALSE(d.readOid(o));
}

/// Build "1.3" (0x2b, two arcs) followed by `extra` arcs of value 1
static size_t buildOidWithArcs(uint8_t* buf, uint8_t arcs) {
    uint8_t contentLen = arcs - 1;  // 0x2b carries two arcs, every other byte one
    buf[0] = BerTag::ObjectId;
    buf[1] = contentLen;
    buf[2] = 0x2b;
    for (int i = 0; i < contentLen - 1; i++) buf[3 + i] = 0x01;
    return 2 + contentLen;
}

static void test_decode_rejects_oid_with_too_many_arcs() {
    uint8_t buf[64];
    Oid o;
    BerReader exact(buf, buildOidWithArcs(buf, OID_MAX_ARCS));
    TEST_ASSERT_TRUE(exact.readOid(o));
    TEST_ASSERT_EQUAL_UINT8(OID_MAX_ARCS, o.length);

    BerReader tooMany(buf, buildOidWithArcs(buf, OID_MAX_ARCS + 1));
    TEST_ASSERT_FALSE(tooMany.readOid(o));
}

static void test_failed_reader_stays_failed() {
    const uint8_t data[] = {0x05, 0x01, 0x00, 0x05, 0x00};
    BerReader r(data, sizeof(data));
    TEST_ASSERT_FALSE(r.readNull());
    TEST_ASSERT_FALSE(r.readNull());
    TEST_ASSERT_TRUE(r.failed());
    TEST_ASSERT_FALSE(r.atEnd());
    TEST_ASSERT_EQUAL_size_t(0, r.remaining());
}

static void test_reader_does_not_read_past_child_region() {
    // A child reader must stop at its own end even if the parent has more bytes
    const uint8_t data[] = {0x30, 0x02, 0x05, 0x00, 0x05, 0x00};
    BerReader r(data, sizeof(data));
    BerReader seq;
    TEST_ASSERT_TRUE(r.readSequence(BerTag::Sequence, seq));
    TEST_ASSERT_TRUE(seq.readNull());
    TEST_ASSERT_TRUE(seq.atEnd());
    TEST_ASSERT_FALSE(seq.readNull());
    TEST_ASSERT_TRUE(r.readNull());
    TEST_ASSERT_TRUE(r.atEnd());
}

static void test_read_raw_returns_whole_element_and_write_raw_copies_it() {
    // Varbind value from the net-snmp request: 05 00 after the OID
    BerReader vb(NETSNMP_GET_SYSDESCR + 31, 12);
    Oid name;
    TEST_ASSERT_TRUE(vb.readOid(name));
    const uint8_t* raw;
    size_t rawLen;
    TEST_ASSERT_TRUE(vb.readRaw(raw, rawLen));
    TEST_ASSERT_EQUAL_size_t(2, rawLen);
    TEST_ASSERT_EQUAL_HEX8(0x05, raw[0]);
    TEST_ASSERT_TRUE(vb.atEnd());

    uint8_t buf[4];
    BerWriter w(buf, sizeof(buf));
    TEST_ASSERT_TRUE(w.writeRaw(raw, rawLen));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(raw, buf, 2);
    TEST_ASSERT_FALSE(w.writeRaw(raw, 3));          // only 2 bytes left
    TEST_ASSERT_TRUE(w.failed());
}

static void test_read_raw_fails_on_truncated_element() {
    const uint8_t data[] = {0x04, 0x05, 'a'};
    BerReader r(data, sizeof(data));
    const uint8_t* raw;
    size_t len;
    TEST_ASSERT_FALSE(r.readRaw(raw, len));
}

static void test_rollback_discards_a_partly_written_element() {
    uint8_t buf[16];
    BerWriter w(buf, sizeof(buf));
    w.beginSequence();
    w.writeNull();
    BerWriter::Mark m = w.mark();
    w.beginSequence();
    TEST_ASSERT_FALSE(w.writeOctetString("much too long for this buffer"));
    TEST_ASSERT_TRUE(w.failed());
    w.rollback(m);
    TEST_ASSERT_FALSE(w.failed());
    TEST_ASSERT_TRUE(w.endSequence());
    const uint8_t expected[] = {0x30, 0x02, 0x05, 0x00};
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), w.length());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, sizeof(expected));
}

static void test_v2c_exception_values_encode_as_empty_context_tags() {
    // noSuchObject [0], noSuchInstance [1], endOfMibView [2], all with length 0
    const uint8_t nso[] = {0x80, 0x00};
    const uint8_t nsi[] = {0x81, 0x00};
    const uint8_t eom[] = {0x82, 0x00};
    ASSERT_ENCODES(nso, writeOctetString(nullptr, 0, BerTag::NoSuchObject));
    ASSERT_ENCODES(nsi, writeOctetString(nullptr, 0, BerTag::NoSuchInstance));
    ASSERT_ENCODES(eom, writeOctetString(nullptr, 0, BerTag::EndOfMibView));
}

static void test_counter64_encoding() {
    const uint8_t zero[] = {0x46, 0x01, 0x00};
    const uint8_t big[] = {0x46, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00};               // 2^32
    const uint8_t max[] = {0x46, 0x09, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ASSERT_ENCODES(zero, writeUnsigned64(BerTag::Counter64, 0));
    ASSERT_ENCODES(big, writeUnsigned64(BerTag::Counter64, 0x100000000ull));
    ASSERT_ENCODES(max, writeUnsigned64(BerTag::Counter64, 0xFFFFFFFFFFFFFFFFull));
}

static void test_counter64_round_trip_and_rejects() {
    const uint64_t values[] = {0, 1, 0x7F, 0x80, 0xFFFFFFFFull, 0x100000000ull, 0x8000000000000000ull,
                               0xFFFFFFFFFFFFFFFFull};
    for (uint64_t v : values) {
        uint8_t buf[16];
        BerWriter w(buf, sizeof(buf));
        TEST_ASSERT_TRUE(w.writeUnsigned64(BerTag::Counter64, v));
        BerReader r(buf, w.length());
        uint64_t out = 0;
        TEST_ASSERT_TRUE(r.readUnsigned64(BerTag::Counter64, out));
        TEST_ASSERT_TRUE(v == out);
    }
    const uint8_t negative[] = {0x46, 0x01, 0x80};
    const uint8_t tooLong[] = {0x46, 0x0A, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t nineNonZero[] = {0x46, 0x09, 0x01, 0, 0, 0, 0, 0, 0, 0, 0};
    uint64_t out;
    BerReader a(negative, sizeof(negative)), b(tooLong, sizeof(tooLong)), c(nineNonZero, sizeof(nineNonZero));
    TEST_ASSERT_FALSE(a.readUnsigned64(BerTag::Counter64, out));
    TEST_ASSERT_FALSE(b.readUnsigned64(BerTag::Counter64, out));
    TEST_ASSERT_FALSE(c.readUnsigned64(BerTag::Counter64, out));
}

static void test_null_and_empty_input() {
    BerReader none(nullptr, 10);
    TEST_ASSERT_TRUE(none.failed());
    BerReader empty((const uint8_t*)"", 0);
    uint8_t tag;
    TEST_ASSERT_FALSE(empty.peekTag(tag));
    TEST_ASSERT_TRUE(empty.atEnd());
}

static void test_decode_every_single_byte_corruption_is_safe() {
    // Flip each byte of a real request to every value: decoding must never
    // crash or overrun. Run under AddressSanitizer for full effect.
    uint8_t data[sizeof(NETSNMP_GET_TWO_VARBINDS)];
    for (size_t i = 0; i < sizeof(data); i++) {
        for (int v = 0; v < 256; v++) {
            memcpy(data, NETSNMP_GET_TWO_VARBINDS, sizeof(data));
            data[i] = (uint8_t)v;
            BerReader r(data, sizeof(data));
            BerReader msg, pdu, list, vb;
            Oid name;
            int32_t n;
            const uint8_t* s;
            size_t len;
            if (!r.readSequence(BerTag::Sequence, msg)) continue;
            msg.readInteger(n);
            msg.readOctetString(s, len);
            uint8_t tag;
            if (!msg.readTlv(tag, pdu)) continue;
            pdu.readInteger(n);
            pdu.readInteger(n);
            pdu.readInteger(n);
            if (!pdu.readSequence(BerTag::Sequence, list)) continue;
            while (list.remaining() > 0 && list.readSequence(BerTag::Sequence, vb)) {
                vb.readOid(name);
                vb.skip();
            }
        }
    }
    TEST_PASS();
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_oid_from_string_parses_dotted_notation);
    RUN_TEST(test_oid_from_string_accepts_leading_dot_and_max_arc);
    RUN_TEST(test_oid_from_string_rejects_invalid);
    RUN_TEST(test_oid_from_string_allows_large_second_arc_under_2);
    RUN_TEST(test_oid_from_string_rejects_too_many_arcs);
    RUN_TEST(test_oid_to_string_fails_when_buffer_too_small);
    RUN_TEST(test_oid_compare_is_lexicographic);
    RUN_TEST(test_oid_prefix_sorts_first);
    RUN_TEST(test_oid_compare_handles_arcs_above_int_max);
    RUN_TEST(test_oid_starts_with);
    RUN_TEST(test_encode_integer_minimal_forms);
    RUN_TEST(test_encode_unsigned_types);
    RUN_TEST(test_encode_octet_string_and_null);
    RUN_TEST(test_encode_long_form_lengths);
    RUN_TEST(test_encode_oids);
    RUN_TEST(test_encode_rejects_invalid_oid);
    RUN_TEST(test_encode_ip_address);
    RUN_TEST(test_encode_reproduces_netsnmp_request_exactly);
    RUN_TEST(test_encode_nested_sequence_longer_than_127);
    RUN_TEST(test_encode_empty_sequence);
    RUN_TEST(test_encode_fails_cleanly_when_buffer_full);
    RUN_TEST(test_encode_fails_when_length_growth_does_not_fit);
    RUN_TEST(test_encode_rejects_unbalanced_sequences);
    RUN_TEST(test_writer_with_null_buffer_fails);
    RUN_TEST(test_decode_netsnmp_get_request);
    RUN_TEST(test_decode_netsnmp_request_with_two_varbinds);
    RUN_TEST(test_integer_round_trip_over_boundaries);
    RUN_TEST(test_unsigned_round_trip);
    RUN_TEST(test_oid_round_trip);
    RUN_TEST(test_decode_accepts_non_minimal_length_from_other_encoders);
    RUN_TEST(test_decode_rejects_truncated_input);
    RUN_TEST(test_decode_rejects_length_beyond_buffer);
    RUN_TEST(test_decode_rejects_unsupported_length_forms);
    RUN_TEST(test_decode_rejects_multi_byte_tags);
    RUN_TEST(test_decode_rejects_wrong_tag);
    RUN_TEST(test_decode_rejects_bad_integers);
    RUN_TEST(test_decode_rejects_bad_unsigned);
    RUN_TEST(test_decode_rejects_null_with_content);
    RUN_TEST(test_decode_rejects_bad_oids);
    RUN_TEST(test_decode_rejects_oid_with_too_many_arcs);
    RUN_TEST(test_failed_reader_stays_failed);
    RUN_TEST(test_reader_does_not_read_past_child_region);
    RUN_TEST(test_read_raw_returns_whole_element_and_write_raw_copies_it);
    RUN_TEST(test_read_raw_fails_on_truncated_element);
    RUN_TEST(test_rollback_discards_a_partly_written_element);
    RUN_TEST(test_v2c_exception_values_encode_as_empty_context_tags);
    RUN_TEST(test_counter64_encoding);
    RUN_TEST(test_counter64_round_trip_and_rejects);
    RUN_TEST(test_null_and_empty_input);
    RUN_TEST(test_decode_every_single_byte_corruption_is_safe);
    return UNITY_END();
}
