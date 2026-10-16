// Host unit tests for lib/netconfig: IPv4 parsing and static configuration validation.
// Run: pio test -e native -f native/test_netconfig

#include <unity.h>
#include <string.h>
#include <netconfig.h>

void setUp() {}
void tearDown() {}

// ---- parseIPv4 ----

static void test_parse_valid_addresses() {
    uint8_t out[4];
    TEST_ASSERT_TRUE(parseIPv4("10.10.6.111", out));
    const uint8_t expected[4] = {10, 10, 6, 111};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);

    TEST_ASSERT_TRUE(parseIPv4("0.0.0.0", out));
    TEST_ASSERT_TRUE(parseIPv4("255.255.255.255", out));
    const uint8_t all[4] = {255, 255, 255, 255};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(all, out, 4);
}

static void test_parse_accepts_leading_zeros_within_three_digits() {
    uint8_t out[4];
    TEST_ASSERT_TRUE(parseIPv4("010.001.000.009", out));
    const uint8_t expected[4] = {10, 1, 0, 9};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);
}

static void test_parse_rejects_out_of_range_octet() {
    uint8_t out[4];
    TEST_ASSERT_FALSE(parseIPv4("256.1.1.1", out));
    TEST_ASSERT_FALSE(parseIPv4("1.1.1.999", out));
}

static void test_parse_rejects_wrong_octet_count() {
    uint8_t out[4];
    TEST_ASSERT_FALSE(parseIPv4("1.2.3", out));
    TEST_ASSERT_FALSE(parseIPv4("1.2.3.4.5", out));
    TEST_ASSERT_FALSE(parseIPv4("1.2.3.", out));
    TEST_ASSERT_FALSE(parseIPv4(".1.2.3", out));
    TEST_ASSERT_FALSE(parseIPv4("1..2.3", out));
}

static void test_parse_rejects_non_digits_and_whitespace() {
    uint8_t out[4];
    TEST_ASSERT_FALSE(parseIPv4("abc", out));
    TEST_ASSERT_FALSE(parseIPv4("1.2.3.4 ", out));
    TEST_ASSERT_FALSE(parseIPv4(" 1.2.3.4", out));
    TEST_ASSERT_FALSE(parseIPv4("1.2.3.-4", out));
    TEST_ASSERT_FALSE(parseIPv4("1.2.3.+4", out));
    TEST_ASSERT_FALSE(parseIPv4("1.2.3.4a", out));
    TEST_ASSERT_FALSE(parseIPv4("0x1.2.3.4", out));
}

static void test_parse_rejects_four_digit_octet() {
    uint8_t out[4];
    TEST_ASSERT_FALSE(parseIPv4("0001.2.3.4", out));
}

static void test_parse_rejects_empty_and_null() {
    uint8_t out[4];
    TEST_ASSERT_FALSE(parseIPv4("", out));
    TEST_ASSERT_FALSE(parseIPv4(nullptr, out));
}

static void test_parse_leaves_output_untouched_on_failure() {
    uint8_t out[4] = {1, 2, 3, 4};
    TEST_ASSERT_FALSE(parseIPv4("9.9.9.x", out));
    const uint8_t expected[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 4);
}

// ---- isValidSubnetMask ----

static void test_mask_accepts_every_prefix_length_1_to_32() {
    for (int prefix = 1; prefix <= 32; prefix++) {
        uint32_t m = 0xFFFFFFFFu << (32 - prefix);
        uint8_t mask[4] = {(uint8_t)(m >> 24), (uint8_t)(m >> 16), (uint8_t)(m >> 8), (uint8_t)m};
        TEST_ASSERT_TRUE_MESSAGE(isValidSubnetMask(mask), "contiguous mask rejected");
    }
}

static void test_mask_rejects_zero_and_non_contiguous() {
    const uint8_t zero[4] = {0, 0, 0, 0};
    const uint8_t gap[4] = {255, 0, 255, 0};
    const uint8_t inverted[4] = {0, 0, 0, 255};
    const uint8_t hole[4] = {255, 255, 253, 0};
    TEST_ASSERT_FALSE(isValidSubnetMask(zero));
    TEST_ASSERT_FALSE(isValidSubnetMask(gap));
    TEST_ASSERT_FALSE(isValidSubnetMask(inverted));
    TEST_ASSERT_FALSE(isValidSubnetMask(hole));
}

// ---- isSameSubnet ----

static void test_same_subnet() {
    const uint8_t a[4] = {10, 132, 0, 200};
    const uint8_t b[4] = {10, 132, 3, 1};
    const uint8_t c[4] = {10, 132, 4, 1};
    const uint8_t mask22[4] = {255, 255, 252, 0};
    TEST_ASSERT_TRUE(isSameSubnet(a, b, mask22));
    TEST_ASSERT_FALSE(isSameSubnet(a, c, mask22));
}

// ---- validateStaticConfig ----

static void test_validate_accepts_typical_configs() {
    TEST_ASSERT_EQUAL(StaticConfigError::None, validateStaticConfig("192.168.1.100", "255.255.255.0", "192.168.1.1"));
    TEST_ASSERT_EQUAL(StaticConfigError::None, validateStaticConfig("10.132.0.200", "255.255.252.0", "10.132.0.1"));
    TEST_ASSERT_EQUAL(StaticConfigError::None, validateStaticConfig("10.10.6.111", "255.255.0.0", "10.10.0.1"));
}

static void test_validate_reports_first_failing_field() {
    TEST_ASSERT_EQUAL(StaticConfigError::InvalidIP, validateStaticConfig("x", "y", "z"));
    TEST_ASSERT_EQUAL(StaticConfigError::InvalidMask, validateStaticConfig("10.0.0.5", "y", "z"));
    TEST_ASSERT_EQUAL(StaticConfigError::NonContiguousMask, validateStaticConfig("10.0.0.5", "255.0.255.0", "z"));
    TEST_ASSERT_EQUAL(StaticConfigError::InvalidGateway, validateStaticConfig("10.0.0.5", "255.0.0.0", "z"));
}

static void test_validate_rejects_network_and_broadcast_host() {
    TEST_ASSERT_EQUAL(StaticConfigError::HostIsNetworkOrBroadcast,
                      validateStaticConfig("192.168.1.0", "255.255.255.0", "192.168.1.1"));
    TEST_ASSERT_EQUAL(StaticConfigError::HostIsNetworkOrBroadcast,
                      validateStaticConfig("192.168.1.255", "255.255.255.0", "192.168.1.1"));
}

static void test_validate_allows_point_to_point_masks() {
    // /31 (RFC 3021) and /32 have no network/broadcast address
    TEST_ASSERT_EQUAL(StaticConfigError::None, validateStaticConfig("10.0.0.0", "255.255.255.254", "10.0.0.1"));
    TEST_ASSERT_EQUAL(StaticConfigError::GatewayNotInSubnet,
                      validateStaticConfig("10.0.0.5", "255.255.255.255", "10.0.0.1"));
}

static void test_validate_rejects_gateway_outside_subnet() {
    TEST_ASSERT_EQUAL(StaticConfigError::GatewayNotInSubnet,
                      validateStaticConfig("10.10.6.200", "255.255.0.0", "192.168.1.1"));
}

static void test_validate_rejects_gateway_equal_to_host() {
    TEST_ASSERT_EQUAL(StaticConfigError::GatewayIsHost,
                      validateStaticConfig("192.168.1.1", "255.255.255.0", "192.168.1.1"));
}

static void test_every_error_has_a_message() {
    for (uint8_t e = 0; e <= (uint8_t)StaticConfigError::GatewayIsHost; e++) {
        const char* msg = staticConfigErrorMessage((StaticConfigError)e);
        TEST_ASSERT_NOT_NULL(msg);
        TEST_ASSERT_TRUE_MESSAGE(strcmp("Unknown error", msg) != 0, "error code has no message");
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_valid_addresses);
    RUN_TEST(test_parse_accepts_leading_zeros_within_three_digits);
    RUN_TEST(test_parse_rejects_out_of_range_octet);
    RUN_TEST(test_parse_rejects_wrong_octet_count);
    RUN_TEST(test_parse_rejects_non_digits_and_whitespace);
    RUN_TEST(test_parse_rejects_four_digit_octet);
    RUN_TEST(test_parse_rejects_empty_and_null);
    RUN_TEST(test_parse_leaves_output_untouched_on_failure);
    RUN_TEST(test_mask_accepts_every_prefix_length_1_to_32);
    RUN_TEST(test_mask_rejects_zero_and_non_contiguous);
    RUN_TEST(test_same_subnet);
    RUN_TEST(test_validate_accepts_typical_configs);
    RUN_TEST(test_validate_reports_first_failing_field);
    RUN_TEST(test_validate_rejects_network_and_broadcast_host);
    RUN_TEST(test_validate_allows_point_to_point_masks);
    RUN_TEST(test_validate_rejects_gateway_outside_subnet);
    RUN_TEST(test_validate_rejects_gateway_equal_to_host);
    RUN_TEST(test_every_error_has_a_message);
    return UNITY_END();
}
