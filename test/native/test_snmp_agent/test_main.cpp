// Host unit tests for lib/snmp_agent: MIB contents and order, GetRequest/GetNextRequest/SetRequest
// handling, error responses, community check and statistics.
// Run: pio test -e native -f native/test_snmp_agent

#include <unity.h>
#include <string.h>
#include <snmp_agent.h>

static SnmpDeviceState state;
static uint8_t resp[SNMP_MAX_MESSAGE];

void setUp() {
    state.uptimeTicks = 123456;
    state.mainsPresent = true;
    state.mainsChanges = 3;
    state.mainsLastChangeTicks = 100000;
    state.sysDescr = "RF-UPS-Monitor v3";
    state.sysContact = "ops@example.com";
    state.sysName = "ups-monitor";
    state.sysLocation = "Rack 4";
}

void tearDown() {}

// snmpget -v1 -c public 1.3.6.1.2.1.1.1.0 (net-snmp)
static const uint8_t NETSNMP_GET_SYSDESCR[] = {
    0x30, 0x29, 0x02, 0x01, 0x00, 0x04, 0x06, 0x70, 0x75, 0x62, 0x6c, 0x69, 0x63,
    0xa0, 0x1c, 0x02, 0x04, 0x46, 0x74, 0x1e, 0x94, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00,
    0x30, 0x0e, 0x30, 0x0c, 0x06, 0x08, 0x2b, 0x06, 0x01, 0x02, 0x01, 0x01, 0x01, 0x00,
    0x05, 0x00};

static Oid oid(const char* text) {
    Oid o;
    TEST_ASSERT_TRUE_MESSAGE(oidFromString(text, o), text);
    return o;
}

static const char* str(const Oid& o) {
    static char buf[4][128];
    static int n = 0;
    n = (n + 1) % 4;
    oidToString(o, buf[n], sizeof(buf[n]));
    return buf[n];
}

/// Build a request with the given PDU type and OIDs
static size_t buildRequest(uint8_t* buf, size_t cap, uint8_t pdu, const char* const* oids, int n,
                           const char* community = "public", int32_t version = 0, int32_t requestId = 4242) {
    BerWriter w(buf, cap);
    w.beginSequence();
    w.writeInteger(version);
    w.writeOctetString(community);
    w.beginSequence(pdu);
    w.writeInteger(requestId);
    w.writeInteger(0);
    w.writeInteger(0);
    w.beginSequence();
    for (int i = 0; i < n; i++) {
        w.beginSequence();
        w.writeOid(oid(oids[i]));
        if (pdu == BerTag::SetRequest) {
            w.writeInteger(1);
        } else {
            w.writeNull();
        }
        w.endSequence();
    }
    w.endSequence();
    w.endSequence();
    w.endSequence();
    TEST_ASSERT_FALSE(w.failed());
    return w.length();
}

/// A decoded GetResponse
struct Reply {
    int32_t requestId, errorStatus, errorIndex;
    char community[32];
    uint8_t count;
    Oid names[SNMP_MAX_VARBINDS];
    uint8_t tags[SNMP_MAX_VARBINDS];
    int32_t ints[SNMP_MAX_VARBINDS];
    uint32_t uints[SNMP_MAX_VARBINDS];
    char strings[SNMP_MAX_VARBINDS][64];
    Oid oids[SNMP_MAX_VARBINDS];
};

static Reply decode(const uint8_t* buf, size_t len) {
    Reply r;
    memset(&r, 0, sizeof(r));
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, len, "agent sent no response");
    BerReader top(buf, len), msg, pdu, list, vb;
    int32_t version;
    const uint8_t* c;
    size_t clen;
    TEST_ASSERT_TRUE(top.readSequence(BerTag::Sequence, msg));
    TEST_ASSERT_TRUE(top.atEnd());
    TEST_ASSERT_TRUE(msg.readInteger(version));
    TEST_ASSERT_EQUAL_INT32(0, version);
    TEST_ASSERT_TRUE(msg.readOctetString(c, clen));
    memcpy(r.community, c, clen < 31 ? clen : 31);
    TEST_ASSERT_TRUE(msg.readSequence(BerTag::GetResponse, pdu));
    TEST_ASSERT_TRUE(pdu.readInteger(r.requestId));
    TEST_ASSERT_TRUE(pdu.readInteger(r.errorStatus));
    TEST_ASSERT_TRUE(pdu.readInteger(r.errorIndex));
    TEST_ASSERT_TRUE(pdu.readSequence(BerTag::Sequence, list));
    while (list.remaining() > 0) {
        uint8_t i = r.count++;
        TEST_ASSERT_TRUE(list.readSequence(BerTag::Sequence, vb));
        TEST_ASSERT_TRUE(vb.readOid(r.names[i]));
        TEST_ASSERT_TRUE(vb.peekTag(r.tags[i]));
        const uint8_t* s;
        size_t slen;
        switch (r.tags[i]) {
        case BerTag::Integer: TEST_ASSERT_TRUE(vb.readInteger(r.ints[i])); break;
        case BerTag::Counter32:
        case BerTag::Gauge32:
        case BerTag::TimeTicks: TEST_ASSERT_TRUE(vb.readUnsigned(r.tags[i], r.uints[i])); break;
        case BerTag::OctetString:
            TEST_ASSERT_TRUE(vb.readOctetString(s, slen));
            memcpy(r.strings[i], s, slen < 63 ? slen : 63);
            break;
        case BerTag::ObjectId: TEST_ASSERT_TRUE(vb.readOid(r.oids[i])); break;
        default: TEST_ASSERT_TRUE(vb.skip()); break;
        }
        TEST_ASSERT_TRUE(vb.atEnd());
    }
    return r;
}

static Reply request(SnmpAgent& agent, uint8_t pdu, const char* const* oids, int n) {
    uint8_t req[1024];
    size_t len = buildRequest(req, sizeof(req), pdu, oids, n);
    return decode(resp, agent.handle(req, len, resp, sizeof(resp), state));
}

// ---- MIB table ----

static void test_mib_is_strictly_sorted() {
    for (uint8_t i = 1; i < mibSize(); i++) {
        TEST_ASSERT_LESS_THAN_MESSAGE(0, oidCompare(mibEntry(i - 1).oid, mibEntry(i).oid), str(mibEntry(i).oid));
    }
}

static void test_mib_contains_expected_objects() {
    const char* expected[] = {"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.2.0", "1.3.6.1.2.1.1.3.0",
                              "1.3.6.1.2.1.1.4.0", "1.3.6.1.2.1.1.5.0", "1.3.6.1.2.1.1.6.0",
                              "1.3.6.1.2.1.1.7.0", "1.3.6.1.4.1.63050.1.0", "1.3.6.1.4.1.63050.2.0",
                              "1.3.6.1.4.1.63050.3.0"};
    TEST_ASSERT_EQUAL_UINT8(10, mibSize());
    for (uint8_t i = 0; i < 10; i++) TEST_ASSERT_EQUAL_STRING(expected[i], str(mibEntry(i).oid));
}

static void test_mib_find_and_next() {
    TEST_ASSERT_NOT_NULL(mibFind(oid("1.3.6.1.2.1.1.3.0")));
    TEST_ASSERT_NULL(mibFind(oid("1.3.6.1.2.1.1.3")));        // not a leaf instance
    TEST_ASSERT_NULL(mibFind(oid("1.3.6.1.2.1.1.3.0.0")));
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.2.1.1.1.0", str(mibNext(oid("1.3"))->oid));
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.2.1.1.4.0", str(mibNext(oid("1.3.6.1.2.1.1.3.0"))->oid));
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.2.1.1.4.0", str(mibNext(oid("1.3.6.1.2.1.1.3.0.5"))->oid));
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.4.1.63050.1.0", str(mibNext(oid("1.3.6.1.2.1.1.7.0"))->oid));
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.4.1.63050.1.0", str(mibNext(oid("1.3.6.1.4.1.63050"))->oid));
    TEST_ASSERT_NULL(mibNext(oid("1.3.6.1.4.1.63050.3.0")));
    TEST_ASSERT_NULL(mibNext(oid("2.0")));
}

// ---- GetRequest ----

static void test_get_netsnmp_request_returns_sysdescr() {
    SnmpAgent agent("public");
    size_t len = agent.handle(NETSNMP_GET_SYSDESCR, sizeof(NETSNMP_GET_SYSDESCR), resp, sizeof(resp), state);
    Reply r = decode(resp, len);
    TEST_ASSERT_EQUAL_HEX32(0x46741e94, (uint32_t)r.requestId);
    TEST_ASSERT_EQUAL_INT32(0, r.errorStatus);
    TEST_ASSERT_EQUAL_UINT8(1, r.count);
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.2.1.1.1.0", str(r.names[0]));
    TEST_ASSERT_EQUAL_HEX8(BerTag::OctetString, r.tags[0]);
    TEST_ASSERT_EQUAL_STRING("RF-UPS-Monitor v3", r.strings[0]);
    TEST_ASSERT_EQUAL_STRING("public", r.community);
}

static void test_get_every_object_value_and_type() {
    SnmpAgent agent("public");
    const char* oids[] = {"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.2.0", "1.3.6.1.2.1.1.3.0",
                          "1.3.6.1.2.1.1.4.0", "1.3.6.1.2.1.1.5.0", "1.3.6.1.2.1.1.6.0",
                          "1.3.6.1.2.1.1.7.0", "1.3.6.1.4.1.63050.1.0", "1.3.6.1.4.1.63050.2.0",
                          "1.3.6.1.4.1.63050.3.0"};
    Reply r = request(agent, BerTag::GetRequest, oids, 10);
    TEST_ASSERT_EQUAL_INT32(0, r.errorStatus);
    TEST_ASSERT_EQUAL_UINT8(10, r.count);
    TEST_ASSERT_EQUAL_STRING("RF-UPS-Monitor v3", r.strings[0]);
    TEST_ASSERT_EQUAL_HEX8(BerTag::ObjectId, r.tags[1]);
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.4.1.63050", str(r.oids[1]));
    TEST_ASSERT_EQUAL_HEX8(BerTag::TimeTicks, r.tags[2]);
    TEST_ASSERT_EQUAL_UINT32(123456, r.uints[2]);
    TEST_ASSERT_EQUAL_STRING("ops@example.com", r.strings[3]);
    TEST_ASSERT_EQUAL_STRING("ups-monitor", r.strings[4]);
    TEST_ASSERT_EQUAL_STRING("Rack 4", r.strings[5]);
    TEST_ASSERT_EQUAL_INT32(72, r.ints[6]);
    TEST_ASSERT_EQUAL_HEX8(BerTag::Integer, r.tags[7]);
    TEST_ASSERT_EQUAL_INT32(1, r.ints[7]);
    TEST_ASSERT_EQUAL_HEX8(BerTag::Counter32, r.tags[8]);
    TEST_ASSERT_EQUAL_UINT32(3, r.uints[8]);
    TEST_ASSERT_EQUAL_HEX8(BerTag::TimeTicks, r.tags[9]);
    TEST_ASSERT_EQUAL_UINT32(100000, r.uints[9]);
}

static void test_get_reports_mains_lost() {
    SnmpAgent agent("public");
    state.mainsPresent = false;
    const char* oids[] = {"1.3.6.1.4.1.63050.1.0"};
    TEST_ASSERT_EQUAL_INT32(0, request(agent, BerTag::GetRequest, oids, 1).ints[0]);
}

static void test_get_empty_strings_are_allowed() {
    SnmpAgent agent("public");
    state.sysContact = "";
    state.sysLocation = nullptr;
    const char* oids[] = {"1.3.6.1.2.1.1.4.0", "1.3.6.1.2.1.1.6.0"};
    Reply r = request(agent, BerTag::GetRequest, oids, 2);
    TEST_ASSERT_EQUAL_INT32(0, r.errorStatus);
    TEST_ASSERT_EQUAL_STRING("", r.strings[0]);
    TEST_ASSERT_EQUAL_STRING("", r.strings[1]);
}

static void test_get_unknown_oid_is_no_such_name_with_index() {
    SnmpAgent agent("public");
    const char* oids[] = {"1.3.6.1.2.1.1.1.0", "1.3.6.1.2.1.1.99.0", "1.3.6.1.2.1.1.3.0"};
    Reply r = request(agent, BerTag::GetRequest, oids, 3);
    TEST_ASSERT_EQUAL_INT32((int32_t)SnmpError::NoSuchName, r.errorStatus);
    TEST_ASSERT_EQUAL_INT32(2, r.errorIndex);
    // v1 error responses echo the request varbinds with their NULL values
    TEST_ASSERT_EQUAL_UINT8(3, r.count);
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.2.1.1.99.0", str(r.names[1]));
    TEST_ASSERT_EQUAL_HEX8(BerTag::Null, r.tags[0]);
    TEST_ASSERT_EQUAL_UINT32(1, agent.stats().outNoSuchName);
}

static void test_get_non_leaf_is_no_such_name() {
    SnmpAgent agent("public");
    const char* oids[] = {"1.3.6.1.2.1.1.1"};
    TEST_ASSERT_EQUAL_INT32((int32_t)SnmpError::NoSuchName, request(agent, BerTag::GetRequest, oids, 1).errorStatus);
}

static void test_get_with_no_varbinds_returns_empty_response() {
    SnmpAgent agent("public");
    Reply r = request(agent, BerTag::GetRequest, nullptr, 0);
    TEST_ASSERT_EQUAL_INT32(0, r.errorStatus);
    TEST_ASSERT_EQUAL_UINT8(0, r.count);
}

// ---- GetNextRequest ----

static void test_walk_from_root_visits_every_object_in_order_then_ends() {
    SnmpAgent agent("public");
    char current[128] = "1.3";
    uint8_t visited = 0;
    for (;;) {
        const char* oids[] = {current};
        Reply r = request(agent, BerTag::GetNextRequest, oids, 1);
        if (r.errorStatus != 0) {
            TEST_ASSERT_EQUAL_INT32((int32_t)SnmpError::NoSuchName, r.errorStatus);
            TEST_ASSERT_EQUAL_INT32(1, r.errorIndex);
            TEST_ASSERT_EQUAL_STRING(current, str(r.names[0]));  // echoed unchanged
            break;
        }
        TEST_ASSERT_TRUE_MESSAGE(visited < mibSize(), "walk did not terminate");
        TEST_ASSERT_EQUAL_STRING(str(mibEntry(visited).oid), str(r.names[0]));
        strcpy(current, str(r.names[0]));
        visited++;
    }
    TEST_ASSERT_EQUAL_UINT8(mibSize(), visited);
    TEST_ASSERT_EQUAL_UINT32(visited + 1, agent.stats().inGetNextRequests);
}

static void test_walk_of_enterprise_subtree() {
    SnmpAgent agent("public");
    const char* start[] = {"1.3.6.1.4.1.63050"};
    Reply r = request(agent, BerTag::GetNextRequest, start, 1);
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.4.1.63050.1.0", str(r.names[0]));
    TEST_ASSERT_EQUAL_INT32(1, r.ints[0]);
}

static void test_getnext_with_several_varbinds() {
    SnmpAgent agent("public");
    const char* oids[] = {"1.3.6.1.2.1.1", "1.3.6.1.2.1.1.3.0", "1.3.6.1.4.1.63050.1.0"};
    Reply r = request(agent, BerTag::GetNextRequest, oids, 3);
    TEST_ASSERT_EQUAL_INT32(0, r.errorStatus);
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.2.1.1.1.0", str(r.names[0]));
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.2.1.1.4.0", str(r.names[1]));
    TEST_ASSERT_EQUAL_STRING("1.3.6.1.4.1.63050.2.0", str(r.names[2]));
}

static void test_getnext_past_end_reports_index_of_that_varbind() {
    SnmpAgent agent("public");
    const char* oids[] = {"1.3.6.1.2.1.1.1.0", "1.3.6.1.4.1.63050.3.0"};
    Reply r = request(agent, BerTag::GetNextRequest, oids, 2);
    TEST_ASSERT_EQUAL_INT32((int32_t)SnmpError::NoSuchName, r.errorStatus);
    TEST_ASSERT_EQUAL_INT32(2, r.errorIndex);
}

// ---- SetRequest ----

static void test_set_is_rejected_with_no_such_name() {
    SnmpAgent agent("public");
    const char* oids[] = {"1.3.6.1.2.1.1.5.0"};
    Reply r = request(agent, BerTag::SetRequest, oids, 1);
    TEST_ASSERT_EQUAL_INT32((int32_t)SnmpError::NoSuchName, r.errorStatus);
    TEST_ASSERT_EQUAL_INT32(1, r.errorIndex);
    TEST_ASSERT_EQUAL_HEX8(BerTag::Integer, r.tags[0]);  // the set value is echoed
    TEST_ASSERT_EQUAL_UINT32(1, agent.stats().inSetRequests);
}

// ---- requests that get no response ----

static void test_wrong_community_gets_no_response() {
    SnmpAgent agent("secret");
    uint8_t req[128];
    const char* oids[] = {"1.3.6.1.2.1.1.1.0"};
    const char* wrong[] = {"public", "secre", "secrets", "Secret", ""};
    for (const char* c : wrong) {
        size_t len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1, c);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(0, agent.handle(req, len, resp, sizeof(resp), state), c);
    }
    TEST_ASSERT_EQUAL_UINT32(5, agent.stats().inBadCommunity);
    size_t len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1, "secret");
    TEST_ASSERT_GREATER_THAN(0, agent.handle(req, len, resp, sizeof(resp), state));
}

static void test_community_follows_the_configured_string() {
    char community[32] = "public";
    SnmpAgent agent(community);
    uint8_t req[128];
    const char* oids[] = {"1.3.6.1.2.1.1.1.0"};
    size_t len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1, "public");
    TEST_ASSERT_GREATER_THAN(0, agent.handle(req, len, resp, sizeof(resp), state));
    strcpy(community, "changed");
    TEST_ASSERT_EQUAL_size_t(0, agent.handle(req, len, resp, sizeof(resp), state));
}

static void test_v2c_and_v3_get_no_response() {
    SnmpAgent agent("public");
    uint8_t req[128];
    const char* oids[] = {"1.3.6.1.2.1.1.1.0"};
    size_t len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1, "public", 1);
    TEST_ASSERT_EQUAL_size_t(0, agent.handle(req, len, resp, sizeof(resp), state));
    len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1, "public", 3);
    TEST_ASSERT_EQUAL_size_t(0, agent.handle(req, len, resp, sizeof(resp), state));
    TEST_ASSERT_EQUAL_UINT32(2, agent.stats().inBadVersions);
}

static void test_responses_and_traps_get_no_response() {
    SnmpAgent agent("public");
    uint8_t req[128];
    const char* oids[] = {"1.3.6.1.2.1.1.1.0"};
    size_t len = buildRequest(req, sizeof(req), BerTag::GetResponse, oids, 1);
    TEST_ASSERT_EQUAL_size_t(0, agent.handle(req, len, resp, sizeof(resp), state));
    TEST_ASSERT_EQUAL_UINT32(1, agent.stats().inUnsupported);
}

static void test_malformed_packets_get_no_response() {
    SnmpAgent agent("public");
    const uint8_t junk[] = {0x30, 0x03, 0x02, 0x01};
    TEST_ASSERT_EQUAL_size_t(0, agent.handle(junk, sizeof(junk), resp, sizeof(resp), state));
    TEST_ASSERT_EQUAL_size_t(0, agent.handle(nullptr, 0, resp, sizeof(resp), state));
    TEST_ASSERT_EQUAL_UINT32(2, agent.stats().inParseErrors);
    TEST_ASSERT_EQUAL_UINT32(2, agent.stats().inPackets);
}

// ---- tooBig ----

static void test_response_too_big_for_buffer_is_too_big_error() {
    SnmpAgent agent("public");
    char longDescr[300];
    memset(longDescr, 'd', sizeof(longDescr) - 1);
    longDescr[sizeof(longDescr) - 1] = '\0';
    state.sysDescr = longDescr;
    uint8_t req[128];
    const char* oids[] = {"1.3.6.1.2.1.1.1.0"};
    size_t len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1);
    uint8_t small[100];
    Reply r = decode(small, agent.handle(req, len, small, sizeof(small), state));
    TEST_ASSERT_EQUAL_INT32((int32_t)SnmpError::TooBig, r.errorStatus);
    TEST_ASSERT_EQUAL_INT32(0, r.errorIndex);
    TEST_ASSERT_EQUAL_UINT8(1, r.count);
    TEST_ASSERT_EQUAL_UINT32(1, agent.stats().outTooBig);
}

static void test_no_response_when_even_the_error_does_not_fit() {
    SnmpAgent agent("public");
    uint8_t tiny[10];
    TEST_ASSERT_EQUAL_size_t(0, agent.handle(NETSNMP_GET_SYSDESCR, sizeof(NETSNMP_GET_SYSDESCR), tiny,
                                             sizeof(tiny), state));
}

static void test_max_varbinds_fit_in_max_message() {
    SnmpAgent agent("public");
    const char* oids[SNMP_MAX_VARBINDS];
    for (int i = 0; i < SNMP_MAX_VARBINDS; i++) oids[i] = "1.3.6.1.2.1.1.1.0";
    Reply r = request(agent, BerTag::GetRequest, oids, SNMP_MAX_VARBINDS);
    TEST_ASSERT_EQUAL_INT32(0, r.errorStatus);
    TEST_ASSERT_EQUAL_UINT8(SNMP_MAX_VARBINDS, r.count);
}

// ---- statistics and robustness ----

static void test_statistics_count_each_kind() {
    SnmpAgent agent("public");
    const char* get[] = {"1.3.6.1.2.1.1.1.0"};
    request(agent, BerTag::GetRequest, get, 1);
    request(agent, BerTag::GetNextRequest, get, 1);
    request(agent, BerTag::SetRequest, get, 1);
    const SnmpStats& s = agent.stats();
    TEST_ASSERT_EQUAL_UINT32(3, s.inPackets);
    TEST_ASSERT_EQUAL_UINT32(1, s.inGetRequests);
    TEST_ASSERT_EQUAL_UINT32(1, s.inGetNextRequests);
    TEST_ASSERT_EQUAL_UINT32(1, s.inSetRequests);
    TEST_ASSERT_EQUAL_UINT32(3, s.outResponses);
}

static void test_request_id_is_echoed() {
    SnmpAgent agent("public");
    const int32_t ids[] = {0, 1, -1, INT32_MAX, INT32_MIN};
    const char* oids[] = {"1.3.6.1.2.1.1.3.0"};
    for (int32_t id : ids) {
        uint8_t req[128];
        size_t len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1, "public", 0, id);
        TEST_ASSERT_EQUAL_INT32(id, decode(resp, agent.handle(req, len, resp, sizeof(resp), state)).requestId);
    }
}

static void test_every_single_byte_corruption_is_handled_safely() {
    // Any response produced from a corrupted request must itself be a valid GetResponse
    SnmpAgent agent("public");
    uint8_t pkt[sizeof(NETSNMP_GET_SYSDESCR)];
    int responses = 0;
    for (size_t i = 0; i < sizeof(pkt); i++) {
        for (int v = 0; v < 256; v++) {
            memcpy(pkt, NETSNMP_GET_SYSDESCR, sizeof(pkt));
            pkt[i] = (uint8_t)v;
            size_t len = agent.handle(pkt, sizeof(pkt), resp, sizeof(resp), state);
            if (len) {
                responses++;
                BerReader top(resp, len), msg;
                TEST_ASSERT_TRUE(top.readSequence(BerTag::Sequence, msg));
                TEST_ASSERT_TRUE(top.atEnd());
            }
        }
    }
    TEST_ASSERT_GREATER_THAN(0, responses);
}

static void test_last_outcome_reports_why_a_request_was_dropped() {
    SnmpAgent agent("public");
    uint8_t req[128];
    const char* oids[] = {"1.3.6.1.2.1.1.1.0"};

    size_t len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1);
    agent.handle(req, len, resp, sizeof(resp), state);
    TEST_ASSERT_EQUAL(SnmpOutcome::Responded, agent.lastOutcome());

    len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1, "wrong");
    agent.handle(req, len, resp, sizeof(resp), state);
    TEST_ASSERT_EQUAL(SnmpOutcome::BadCommunity, agent.lastOutcome());

    len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1, "public", 1);
    agent.handle(req, len, resp, sizeof(resp), state);
    TEST_ASSERT_EQUAL(SnmpOutcome::BadVersion, agent.lastOutcome());

    const uint8_t junk[] = {0x30, 0x01};
    agent.handle(junk, sizeof(junk), resp, sizeof(resp), state);
    TEST_ASSERT_EQUAL(SnmpOutcome::ParseError, agent.lastOutcome());

    len = buildRequest(req, sizeof(req), BerTag::Trap, oids, 1);
    agent.handle(req, len, resp, sizeof(resp), state);
    TEST_ASSERT_EQUAL(SnmpOutcome::Unsupported, agent.lastOutcome());

    len = buildRequest(req, sizeof(req), BerTag::SetRequest, oids, 1);
    TEST_ASSERT_GREATER_THAN(0, agent.handle(req, len, resp, sizeof(resp), state));
    TEST_ASSERT_EQUAL(SnmpOutcome::SetRejected, agent.lastOutcome());

    uint8_t tiny[10];
    len = buildRequest(req, sizeof(req), BerTag::GetRequest, oids, 1);
    agent.handle(req, len, tiny, sizeof(tiny), state);
    TEST_ASSERT_EQUAL(SnmpOutcome::NoRoom, agent.lastOutcome());

    // A noSuchName error is still a response
    const char* unknown[] = {"1.3.6.1.9.9.9.0"};
    len = buildRequest(req, sizeof(req), BerTag::GetRequest, unknown, 1);
    TEST_ASSERT_GREATER_THAN(0, agent.handle(req, len, resp, sizeof(resp), state));
    TEST_ASSERT_EQUAL(SnmpOutcome::Responded, agent.lastOutcome());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mib_is_strictly_sorted);
    RUN_TEST(test_mib_contains_expected_objects);
    RUN_TEST(test_mib_find_and_next);
    RUN_TEST(test_get_netsnmp_request_returns_sysdescr);
    RUN_TEST(test_get_every_object_value_and_type);
    RUN_TEST(test_get_reports_mains_lost);
    RUN_TEST(test_get_empty_strings_are_allowed);
    RUN_TEST(test_get_unknown_oid_is_no_such_name_with_index);
    RUN_TEST(test_get_non_leaf_is_no_such_name);
    RUN_TEST(test_get_with_no_varbinds_returns_empty_response);
    RUN_TEST(test_walk_from_root_visits_every_object_in_order_then_ends);
    RUN_TEST(test_walk_of_enterprise_subtree);
    RUN_TEST(test_getnext_with_several_varbinds);
    RUN_TEST(test_getnext_past_end_reports_index_of_that_varbind);
    RUN_TEST(test_set_is_rejected_with_no_such_name);
    RUN_TEST(test_wrong_community_gets_no_response);
    RUN_TEST(test_community_follows_the_configured_string);
    RUN_TEST(test_v2c_and_v3_get_no_response);
    RUN_TEST(test_responses_and_traps_get_no_response);
    RUN_TEST(test_malformed_packets_get_no_response);
    RUN_TEST(test_response_too_big_for_buffer_is_too_big_error);
    RUN_TEST(test_no_response_when_even_the_error_does_not_fit);
    RUN_TEST(test_max_varbinds_fit_in_max_message);
    RUN_TEST(test_statistics_count_each_kind);
    RUN_TEST(test_request_id_is_echoed);
    RUN_TEST(test_every_single_byte_corruption_is_handled_safely);
    RUN_TEST(test_last_outcome_reports_why_a_request_was_dropped);
    return UNITY_END();
}
