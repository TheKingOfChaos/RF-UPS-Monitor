// Host unit tests for lib/settings_codec: JSON round trip, validation, A/B slot selection.
// Run: pio test -e native -f native/test_settings_codec

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <settings_codec.h>

void setUp() {}
void tearDown() {}

// A v1 file as written by firmware before the "sequence" field existed
static const char* const LEGACY_V1_JSON =
    "{\"community\":\"public\",\"dhcpEnabled\":true,\"staticIP\":\"192.168.1.100\","
    "\"subnetMask\":\"255.255.255.0\",\"gateway\":\"192.168.1.1\",\"version\":1}";

static Settings staticSettings() {
    Settings s;
    settingsDefaults(s);
    s.dhcpEnabled = false;
    strcpy(s.staticIP, "10.132.0.200");
    strcpy(s.subnetMask, "255.255.252.0");
    strcpy(s.gateway, "10.132.0.1");
    strcpy(s.community, "secret");
    s.sequence = 42;
    return s;
}

// ---- defaults ----

static void test_defaults_are_dhcp_and_public() {
    Settings s;
    memset(&s, 0xAA, sizeof(s));
    settingsDefaults(s);
    TEST_ASSERT_TRUE(s.dhcpEnabled);
    TEST_ASSERT_EQUAL_STRING("public", s.community);
    TEST_ASSERT_EQUAL_UINT32(SETTINGS_VERSION, s.version);
    TEST_ASSERT_EQUAL_UINT32(0, s.sequence);
}

static void test_defaults_survive_round_trip() {
    Settings in, out;
    settingsDefaults(in);
    char json[SETTINGS_JSON_MAX];
    TEST_ASSERT_GREATER_THAN(0, settingsToJson(in, json, sizeof(json)));
    TEST_ASSERT_EQUAL(SettingsParseError::None, settingsFromJson(json, out));
    TEST_ASSERT_TRUE(settingsEqual(in, out));
}

// ---- serialization ----

static void test_round_trip_static_settings() {
    Settings in = staticSettings();
    Settings out;
    char json[SETTINGS_JSON_MAX];
    TEST_ASSERT_GREATER_THAN(0, settingsToJson(in, json, sizeof(json)));
    TEST_ASSERT_EQUAL(SettingsParseError::None, settingsFromJson(json, out));
    TEST_ASSERT_TRUE(settingsEqual(in, out));
    TEST_ASSERT_EQUAL_UINT32(42, out.sequence);
}

static void test_max_length_fields_fit_in_buffer() {
    Settings s = staticSettings();
    memset(s.community, 'x', sizeof(s.community) - 1);
    s.community[sizeof(s.community) - 1] = '\0';
    strcpy(s.staticIP, "255.255.255.254");
    strcpy(s.subnetMask, "255.255.255.255");
    strcpy(s.gateway, "255.255.255.253");
    s.sequence = 0xFFFFFFFFu;
    char json[SETTINGS_JSON_MAX];
    TEST_ASSERT_GREATER_THAN(0, settingsToJson(s, json, sizeof(json)));
}

static void test_to_json_fails_when_buffer_too_small() {
    Settings s;
    settingsDefaults(s);
    char json[16];
    TEST_ASSERT_EQUAL(0, settingsToJson(s, json, sizeof(json)));
    TEST_ASSERT_EQUAL(0, settingsToJson(s, nullptr, 0));
}

// ---- parsing and validation ----

static void test_legacy_v1_file_loads_with_sequence_zero() {
    Settings s;
    TEST_ASSERT_EQUAL(SettingsParseError::None, settingsFromJson(LEGACY_V1_JSON, s));
    TEST_ASSERT_EQUAL_UINT32(0, s.sequence);
    TEST_ASSERT_TRUE(s.dhcpEnabled);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", s.staticIP);
}

static void test_rejects_empty_and_garbage() {
    Settings s;
    TEST_ASSERT_EQUAL(SettingsParseError::Empty, settingsFromJson("", s));
    TEST_ASSERT_EQUAL(SettingsParseError::Empty, settingsFromJson(nullptr, s));
    TEST_ASSERT_EQUAL(SettingsParseError::InvalidJson, settingsFromJson("{\"community\":", s));
    TEST_ASSERT_EQUAL(SettingsParseError::InvalidJson, settingsFromJson("[1,2,3]", s));
    TEST_ASSERT_EQUAL(SettingsParseError::InvalidJson, settingsFromJson("\xFF\xFF\xFF", s));
}

static void test_rejects_wrong_or_missing_version() {
    Settings s;
    TEST_ASSERT_EQUAL(SettingsParseError::WrongVersion,
                      settingsFromJson("{\"community\":\"public\",\"dhcpEnabled\":true,\"staticIP\":\"1.1.1.1\","
                                       "\"subnetMask\":\"255.0.0.0\",\"gateway\":\"1.1.1.2\",\"version\":2}", s));
    TEST_ASSERT_EQUAL(SettingsParseError::WrongVersion,
                      settingsFromJson("{\"community\":\"public\",\"dhcpEnabled\":true}", s));
}

static void test_rejects_missing_or_mistyped_field() {
    Settings s;
    TEST_ASSERT_EQUAL(SettingsParseError::MissingField,
                      settingsFromJson("{\"community\":\"public\",\"staticIP\":\"1.1.1.1\","
                                       "\"subnetMask\":\"255.0.0.0\",\"gateway\":\"1.1.1.2\",\"version\":1}", s));
    TEST_ASSERT_EQUAL(SettingsParseError::MissingField,
                      settingsFromJson("{\"community\":\"public\",\"dhcpEnabled\":\"yes\",\"staticIP\":\"1.1.1.1\","
                                       "\"subnetMask\":\"255.0.0.0\",\"gateway\":\"1.1.1.2\",\"version\":1}", s));
    TEST_ASSERT_EQUAL(SettingsParseError::MissingField,
                      settingsFromJson("{\"community\":\"public\",\"dhcpEnabled\":true,\"staticIP\":\"1.1.1.1\","
                                       "\"subnetMask\":\"255.0.0.0\",\"gateway\":\"1.1.1.2\",\"version\":1,"
                                       "\"sequence\":-1}", s));
}

static void test_rejects_overlong_field_instead_of_truncating() {
    Settings s;
    TEST_ASSERT_EQUAL(SettingsParseError::FieldTooLong,
                      settingsFromJson("{\"community\":\"public\",\"dhcpEnabled\":true,"
                                       "\"staticIP\":\"1111.1111.1111.1111\",\"subnetMask\":\"255.0.0.0\","
                                       "\"gateway\":\"1.1.1.2\",\"version\":1}", s));
}

static void test_rejects_empty_or_unprintable_community() {
    Settings s;
    TEST_ASSERT_EQUAL(SettingsParseError::BadCommunity,
                      settingsFromJson("{\"community\":\"\",\"dhcpEnabled\":true,\"staticIP\":\"1.1.1.1\","
                                       "\"subnetMask\":\"255.0.0.0\",\"gateway\":\"1.1.1.2\",\"version\":1}", s));
    TEST_ASSERT_EQUAL(SettingsParseError::BadCommunity,
                      settingsFromJson("{\"community\":\"ab\\ncd\",\"dhcpEnabled\":true,\"staticIP\":\"1.1.1.1\","
                                       "\"subnetMask\":\"255.0.0.0\",\"gateway\":\"1.1.1.2\",\"version\":1}", s));
}

static void test_static_config_validated_only_when_dhcp_disabled() {
    Settings s;
    const char* badStatic =
        "{\"community\":\"public\",\"dhcpEnabled\":%s,\"staticIP\":\"10.0.0.5\","
        "\"subnetMask\":\"255.0.255.0\",\"gateway\":\"10.0.0.1\",\"version\":1}";
    char json[SETTINGS_JSON_MAX];
    snprintf(json, sizeof(json), badStatic, "true");
    TEST_ASSERT_EQUAL(SettingsParseError::None, settingsFromJson(json, s));
    snprintf(json, sizeof(json), badStatic, "false");
    TEST_ASSERT_EQUAL(SettingsParseError::BadStaticConfig, settingsFromJson(json, s));
}

static void test_failed_parse_leaves_output_untouched() {
    Settings s = staticSettings();
    Settings before = s;
    TEST_ASSERT_NOT_EQUAL(SettingsParseError::None, settingsFromJson("{}", s));
    TEST_ASSERT_TRUE(settingsEqual(before, s));
}

static void test_ignores_unknown_fields() {
    Settings s;
    TEST_ASSERT_EQUAL(SettingsParseError::None,
                      settingsFromJson("{\"community\":\"public\",\"dhcpEnabled\":true,\"staticIP\":\"1.1.1.1\","
                                       "\"subnetMask\":\"255.0.0.0\",\"gateway\":\"1.1.1.2\",\"version\":1,"
                                       "\"future\":123}", s));
}

static void test_every_parse_error_has_a_message() {
    for (uint8_t e = 0; e <= (uint8_t)SettingsParseError::BadStaticConfig; e++) {
        TEST_ASSERT_TRUE_MESSAGE(strcmp("unknown error", settingsParseErrorMessage((SettingsParseError)e)) != 0,
                                 "error code has no message");
    }
}

// ---- equality ----

static void test_equal_ignores_bytes_after_terminator() {
    Settings a = staticSettings();
    Settings b = a;
    // Simulate a shorter string copied over a longer one: stale bytes remain after the NUL
    strcpy(b.staticIP, "10.132.0.255");
    strcpy(b.staticIP, "10.132.0.200");
    b.community[10] = 'Z';
    TEST_ASSERT_TRUE(settingsEqual(a, b));
    b.sequence++;
    TEST_ASSERT_FALSE(settingsEqual(a, b));
}

// ---- sequence and slot selection ----

static void test_sequence_newer_handles_wraparound() {
    TEST_ASSERT_TRUE(sequenceIsNewer(2, 1));
    TEST_ASSERT_FALSE(sequenceIsNewer(1, 2));
    TEST_ASSERT_FALSE(sequenceIsNewer(5, 5));
    TEST_ASSERT_TRUE(sequenceIsNewer(0, 0xFFFFFFFFu));
    TEST_ASSERT_FALSE(sequenceIsNewer(0xFFFFFFFFu, 0));
}

static void test_choose_newest_valid_slot() {
    TEST_ASSERT_EQUAL(SettingsSlot::B, chooseActiveSlot(true, 3, true, 4));
    TEST_ASSERT_EQUAL(SettingsSlot::A, chooseActiveSlot(true, 5, true, 4));
    TEST_ASSERT_EQUAL(SettingsSlot::A, chooseActiveSlot(true, 0, true, 0xFFFFFFFFu));
}

static void test_choose_falls_back_to_the_only_valid_slot() {
    // A newer copy that failed validation must not win over an older valid one
    TEST_ASSERT_EQUAL(SettingsSlot::A, chooseActiveSlot(true, 3, false, 99));
    TEST_ASSERT_EQUAL(SettingsSlot::B, chooseActiveSlot(false, 99, true, 3));
    TEST_ASSERT_EQUAL(SettingsSlot::None, chooseActiveSlot(false, 0, false, 0));
}

static void test_tie_prefers_slot_a() {
    TEST_ASSERT_EQUAL(SettingsSlot::A, chooseActiveSlot(true, 7, true, 7));
}

static void test_next_write_slot_alternates() {
    TEST_ASSERT_EQUAL(SettingsSlot::B, nextWriteSlot(SettingsSlot::A));
    TEST_ASSERT_EQUAL(SettingsSlot::A, nextWriteSlot(SettingsSlot::B));
    TEST_ASSERT_EQUAL(SettingsSlot::A, nextWriteSlot(SettingsSlot::None));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_dhcp_and_public);
    RUN_TEST(test_defaults_survive_round_trip);
    RUN_TEST(test_round_trip_static_settings);
    RUN_TEST(test_max_length_fields_fit_in_buffer);
    RUN_TEST(test_to_json_fails_when_buffer_too_small);
    RUN_TEST(test_legacy_v1_file_loads_with_sequence_zero);
    RUN_TEST(test_rejects_empty_and_garbage);
    RUN_TEST(test_rejects_wrong_or_missing_version);
    RUN_TEST(test_rejects_missing_or_mistyped_field);
    RUN_TEST(test_rejects_overlong_field_instead_of_truncating);
    RUN_TEST(test_rejects_empty_or_unprintable_community);
    RUN_TEST(test_static_config_validated_only_when_dhcp_disabled);
    RUN_TEST(test_failed_parse_leaves_output_untouched);
    RUN_TEST(test_ignores_unknown_fields);
    RUN_TEST(test_every_parse_error_has_a_message);
    RUN_TEST(test_equal_ignores_bytes_after_terminator);
    RUN_TEST(test_sequence_newer_handles_wraparound);
    RUN_TEST(test_choose_newest_valid_slot);
    RUN_TEST(test_choose_falls_back_to_the_only_valid_slot);
    RUN_TEST(test_tie_prefers_slot_a);
    RUN_TEST(test_next_write_slot_alternates);
    return UNITY_END();
}
