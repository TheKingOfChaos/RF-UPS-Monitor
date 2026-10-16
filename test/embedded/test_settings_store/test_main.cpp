// On-device tests for lib/settings: A/B storage on LittleFS, fallback and recovery.
// Uses its own paths under /test so the device's real settings are never touched.
// Run: pio test -e pico -f embedded/test_settings_store

#include <Arduino.h>
#include <LittleFS.h>
#include <unity.h>
#include <settings.h>

static const char* const PATH_A = "test/settings_a.json";
static const char* const PATH_B = "test/settings_b.json";

static void removeTestFiles() {
    LittleFS.remove(PATH_A);
    LittleFS.remove(PATH_B);
}

static void writeRaw(const char* path, const char* content) {
    File f = LittleFS.open(path, "w");
    TEST_ASSERT_TRUE_MESSAGE((bool)f, "cannot open test file for writing");
    f.print(content);
    f.close();
}

static Settings staticSettings(const char* ip) {
    Settings s;
    settingsDefaults(s);
    s.dhcpEnabled = false;
    strcpy(s.staticIP, ip);
    strcpy(s.subnetMask, "255.255.255.0");
    strcpy(s.gateway, "192.168.50.1");
    return s;
}

void setUp() {
    removeTestFiles();
}

void tearDown() {
    removeTestFiles();
}

static void test_empty_store_loads_defaults() {
    SettingsStore store(PATH_A, PATH_B);
    Settings s;
    TEST_ASSERT_EQUAL(SettingsSlot::None, store.load(s));
    TEST_ASSERT_TRUE(s.dhcpEnabled);
    TEST_ASSERT_EQUAL_STRING("public", s.community);
}

static void test_first_save_goes_to_slot_a_and_survives_reload() {
    SettingsStore store(PATH_A, PATH_B);
    Settings s = staticSettings("192.168.50.10");
    store.load(s);
    s = staticSettings("192.168.50.10");
    TEST_ASSERT_TRUE(store.save(s));
    TEST_ASSERT_EQUAL(SettingsSlot::A, store.activeSlot());
    TEST_ASSERT_EQUAL_UINT32(1, s.sequence);

    SettingsStore fresh(PATH_A, PATH_B);
    Settings loaded;
    TEST_ASSERT_EQUAL(SettingsSlot::A, fresh.load(loaded));
    TEST_ASSERT_TRUE(settingsEqual(s, loaded));
}

static void test_saves_alternate_slots_and_newest_wins() {
    SettingsStore store(PATH_A, PATH_B);
    Settings s;
    store.load(s);

    s = staticSettings("192.168.50.11");
    TEST_ASSERT_TRUE(store.save(s));
    s = staticSettings("192.168.50.12");
    TEST_ASSERT_TRUE(store.save(s));
    TEST_ASSERT_EQUAL(SettingsSlot::B, store.activeSlot());
    TEST_ASSERT_EQUAL_UINT32(2, s.sequence);
    s = staticSettings("192.168.50.13");
    TEST_ASSERT_TRUE(store.save(s));
    TEST_ASSERT_EQUAL(SettingsSlot::A, store.activeSlot());

    SettingsStore fresh(PATH_A, PATH_B);
    Settings loaded;
    TEST_ASSERT_EQUAL(SettingsSlot::A, fresh.load(loaded));
    TEST_ASSERT_EQUAL_STRING("192.168.50.13", loaded.staticIP);
    TEST_ASSERT_EQUAL_UINT32(3, loaded.sequence);
}

static void test_corrupt_newest_copy_falls_back_to_previous() {
    SettingsStore store(PATH_A, PATH_B);
    Settings s;
    store.load(s);
    s = staticSettings("192.168.50.21");
    TEST_ASSERT_TRUE(store.save(s));   // A, sequence 1
    s = staticSettings("192.168.50.22");
    TEST_ASSERT_TRUE(store.save(s));   // B, sequence 2

    // Simulate a power cut halfway through writing B
    writeRaw(PATH_B, "{\"community\":\"public\",\"dhcpEn");

    SettingsStore fresh(PATH_A, PATH_B);
    Settings loaded;
    TEST_ASSERT_EQUAL(SettingsSlot::A, fresh.load(loaded));
    TEST_ASSERT_EQUAL_STRING("192.168.50.21", loaded.staticIP);

    // The next save must overwrite the corrupt copy, not the good one
    loaded = staticSettings("192.168.50.23");
    TEST_ASSERT_TRUE(fresh.save(loaded));
    TEST_ASSERT_EQUAL(SettingsSlot::B, fresh.activeSlot());
    TEST_ASSERT_EQUAL_UINT32(2, loaded.sequence);
}

static void test_both_copies_corrupt_loads_defaults() {
    writeRaw(PATH_A, "garbage");
    writeRaw(PATH_B, "");
    SettingsStore store(PATH_A, PATH_B);
    Settings s;
    TEST_ASSERT_EQUAL(SettingsSlot::None, store.load(s));
    TEST_ASSERT_TRUE(s.dhcpEnabled);
}

static void test_oversized_file_is_ignored() {
    String big = "{\"community\":\"";
    for (int i = 0; i < 400; i++) big += 'x';
    big += "\"}";
    writeRaw(PATH_A, big.c_str());
    SettingsStore store(PATH_A, PATH_B);
    Settings s;
    TEST_ASSERT_EQUAL(SettingsSlot::None, store.load(s));
}

static void test_legacy_file_without_sequence_is_loaded_and_superseded() {
    writeRaw(PATH_A,
             "{\"community\":\"legacy\",\"dhcpEnabled\":true,\"staticIP\":\"192.168.1.100\","
             "\"subnetMask\":\"255.255.255.0\",\"gateway\":\"192.168.1.1\",\"version\":1}");
    SettingsStore store(PATH_A, PATH_B);
    Settings s;
    TEST_ASSERT_EQUAL(SettingsSlot::A, store.load(s));
    TEST_ASSERT_EQUAL_STRING("legacy", s.community);

    TEST_ASSERT_TRUE(store.save(s));
    SettingsStore fresh(PATH_A, PATH_B);
    Settings loaded;
    TEST_ASSERT_EQUAL(SettingsSlot::B, fresh.load(loaded));
    TEST_ASSERT_EQUAL_STRING("legacy", loaded.community);
}

static void test_saving_defaults_is_newer_than_stored_copy() {
    // Factory reset saves settingsDefaults(), whose sequence is 0. It must still win.
    SettingsStore store(PATH_A, PATH_B);
    Settings s;
    store.load(s);
    for (int i = 0; i < 3; i++) {
        s = staticSettings("192.168.50.30");
        TEST_ASSERT_TRUE(store.save(s));
    }
    settingsDefaults(s);
    TEST_ASSERT_TRUE(store.save(s));

    SettingsStore fresh(PATH_A, PATH_B);
    Settings loaded;
    fresh.load(loaded);
    TEST_ASSERT_TRUE(loaded.dhcpEnabled);
    TEST_ASSERT_EQUAL_UINT32(4, loaded.sequence);
}

void setup() {
    Serial.begin(115200);
    uint32_t start = millis();
    while (!Serial && millis() - start < 10000) delay(10);
    delay(500);

    UNITY_BEGIN();
    if (!LittleFS.begin()) {
        TEST_FAIL_MESSAGE("LittleFS mount failed");
    }
    LittleFS.mkdir("test");

    RUN_TEST(test_empty_store_loads_defaults);
    RUN_TEST(test_first_save_goes_to_slot_a_and_survives_reload);
    RUN_TEST(test_saves_alternate_slots_and_newest_wins);
    RUN_TEST(test_corrupt_newest_copy_falls_back_to_previous);
    RUN_TEST(test_both_copies_corrupt_loads_defaults);
    RUN_TEST(test_oversized_file_is_ignored);
    RUN_TEST(test_legacy_file_without_sequence_is_loaded_and_superseded);
    RUN_TEST(test_saving_defaults_is_newer_than_stored_copy);

    LittleFS.rmdir("test");
    UNITY_END();
}

void loop() {}
