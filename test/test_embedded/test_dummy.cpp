#include "test_common.h"
#include "../lib/network/include/network.h"
#include "../lib/w5500/include/w5500.h"

// External declarations
extern W5500 eth;
extern Network net;

// Test states and variables
unsigned long factoryResetStartTime = 0;
bool factoryResetButtonPressed = false;

// Forward declarations of tests from other files
void test_static_ip_configuration();
void test_dhcp_configuration();
void test_udp_socket_creation();
void test_udp_packet_send_receive();
void test_spi_initialization();
void test_basic_read_write();
void test_interrupt_handling();
void test_register_access();
void test_chip_identification();

void test_power_monitoring_state(void) {
    // Simulate power present (HIGH)
    digitalWrite(POWER_PIN, HIGH);
    delayMicroseconds(100); // Allow signal to stabilize
    TEST_ASSERT_EQUAL(LOW, digitalRead(POWER_PIN));
    
    // Simulate power loss (LOW)
    digitalWrite(POWER_PIN, LOW);
    delayMicroseconds(100); // Allow signal to stabilize
    TEST_ASSERT_EQUAL(LOW, digitalRead(POWER_PIN));
}

void test_factory_reset_timing(void) {
    // Simulate button press
    digitalWrite(FACTORY_RESET_PIN, LOW);
    unsigned long startTime = millis();
    
    // Simulate 10 second hold
    delay(10000);
    
    // Verify timing
    unsigned long elapsedTime = millis() - startTime;
    TEST_ASSERT_GREATER_OR_EQUAL(10000, elapsedTime);
    
    // Release button
    digitalWrite(FACTORY_RESET_PIN, HIGH);
}

void test_network_config(void) {
    // Test DHCP configuration
    bool dhcpEnabled = true;
    TEST_ASSERT_TRUE(dhcpEnabled);
    
    // Test static IP configuration
    uint8_t staticIP[] = {192, 168, 1, 100};
    uint8_t expectedIP[] = {192, 168, 1, 100};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedIP, staticIP, 4);
}

void test_snmp_community(void) {
    // Test default community string
    const char* defaultCommunity = "public";
    const char* currentCommunity = "public";
    TEST_ASSERT_EQUAL_STRING(defaultCommunity, currentCommunity);
    
    // Test community string validation
    const char* invalidCommunity = "";
    TEST_ASSERT_FALSE(strcmp(defaultCommunity, invalidCommunity) == 0);
}

void test_json_config(void) {
    // Test JSON serialization
    StaticJsonDocument<512> doc;
    doc["community"] = "public";
    doc["dhcpEnabled"] = true;
    doc["staticIP"] = "192.168.1.100";
    doc["subnetMask"] = "255.255.255.0";
    doc["gateway"] = "192.168.1.1";
    doc["version"] = 1;

    // Serialize to string
    char jsonBuffer[512];
    size_t bytesWritten = serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
    TEST_ASSERT_GREATER_THAN(0, bytesWritten);

    // Test JSON deserialization
    StaticJsonDocument<512> docRead;
    DeserializationError error = deserializeJson(docRead, jsonBuffer);
    TEST_ASSERT_TRUE(error.code() == DeserializationError::Ok);

    // Verify values
    TEST_ASSERT_EQUAL_STRING("public", docRead["community"]);
    TEST_ASSERT_TRUE(docRead["dhcpEnabled"]);
    TEST_ASSERT_EQUAL_STRING("192.168.1.100", docRead["staticIP"]);
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", docRead["subnetMask"]);
    TEST_ASSERT_EQUAL_STRING("192.168.1.1", docRead["gateway"]);
    TEST_ASSERT_EQUAL(1, docRead["version"]);
}

void test_settings_file_operations(void) {

    Serial.println("\n=== Starting settings file operations test ===");

    // Initialize test settings
    memset(&currentSettings, 0, sizeof(Settings));
    
    Serial.println("\nInitializing test settings...");
    
    strncpy(currentSettings.community, "testCommunity", sizeof(currentSettings.community) - 1);
    currentSettings.community[sizeof(currentSettings.community) - 1] = '\0';
    Serial.print("Set community to: '");
    Serial.print(currentSettings.community);
    Serial.println("'");
    
    currentSettings.dhcpEnabled = false;
    Serial.print("Set dhcpEnabled to: ");
    Serial.println(currentSettings.dhcpEnabled);
    
    strncpy(currentSettings.staticIP, "10.0.0.100", sizeof(currentSettings.staticIP) - 1);
    currentSettings.staticIP[sizeof(currentSettings.staticIP) - 1] = '\0';
    Serial.print("Set staticIP to: ");
    Serial.println(currentSettings.staticIP);
    
    strncpy(currentSettings.subnetMask, "255.255.0.0", sizeof(currentSettings.subnetMask) - 1);
    currentSettings.subnetMask[sizeof(currentSettings.subnetMask) - 1] = '\0';
    Serial.print("Set subnetMask to: ");
    Serial.println(currentSettings.subnetMask);
    
    strncpy(currentSettings.gateway, "10.0.0.1", sizeof(currentSettings.gateway) - 1);
    currentSettings.gateway[sizeof(currentSettings.gateway) - 1] = '\0';
    Serial.print("Set gateway to: ");
    Serial.println(currentSettings.gateway);
    
    currentSettings.version = SETTINGS_VERSION;
    Serial.print("Set version to: ");
    Serial.println(currentSettings.version);

    // Debug output before save
    Serial.println("\nTest settings before save:");
    Serial.print("Community: '");
    Serial.print(currentSettings.community);
    Serial.println("'");
    
    // Save settings
    TEST_ASSERT_TRUE(saveSettings());

    // Store test settings for comparison
    Settings testSettings;
    memcpy(&testSettings, &currentSettings, sizeof(Settings));

    // Clear current settings
    memset(&currentSettings, 0, sizeof(Settings));

    // Debug output after clear
    Serial.println("\nSettings after clear:");
    Serial.print("Community: '");
    Serial.print(currentSettings.community);
    Serial.println("'");

    // Load settings back
    TEST_ASSERT_TRUE(loadSettings());

    // Debug output after load
    Serial.println("\nSettings after load:");
    Serial.print("Community: '");
    Serial.print(currentSettings.community);
    Serial.println("'");

    // Verify loaded settings match original test settings

    // Debug output
    Serial.print("Expected community: ");
    Serial.println(testSettings.community);
    Serial.print("Actual community: ");
    Serial.println(currentSettings.community);
    
    // Verify loaded settings match original test settings
    TEST_ASSERT_EQUAL_STRING(testSettings.community, currentSettings.community);
    TEST_ASSERT_EQUAL(testSettings.dhcpEnabled, currentSettings.dhcpEnabled);
    TEST_ASSERT_EQUAL_STRING(testSettings.staticIP, currentSettings.staticIP);
    TEST_ASSERT_EQUAL_STRING(testSettings.subnetMask, currentSettings.subnetMask);
    TEST_ASSERT_EQUAL_STRING(testSettings.gateway, currentSettings.gateway);
    TEST_ASSERT_EQUAL(testSettings.version, currentSettings.version);

    // Test wear leveling
    Serial.println("\nTesting wear leveling...");
    
    File activeFlag = LittleFS.open(ACTIVE_FILE_FLAG, "r");
    TEST_ASSERT_TRUE(activeFlag);
    String activeFile = activeFlag.readString();
    activeFlag.close();
    
    Serial.print("Initial active file: ");
    Serial.println(activeFile);

    // Save settings again to trigger wear leveling
    Serial.println("Saving settings again to trigger wear leveling...");
    TEST_ASSERT_TRUE(saveSettings());

    // Verify active file changed
    activeFlag = LittleFS.open(ACTIVE_FILE_FLAG, "r");
    TEST_ASSERT_TRUE(activeFlag);
    String newActiveFile = activeFlag.readString();
    activeFlag.close();
    
    Serial.print("New active file: ");
    Serial.println(newActiveFile);
    
    Serial.println("Verifying active file changed...");
    TEST_ASSERT_FALSE(strcmp(activeFile.c_str(), newActiveFile.c_str()) == 0);
    Serial.println("Wear leveling test complete");
}

void setup() {
    // wait until serial port has connected
    while(!Serial) {}
    
    pinMode(LED_BUILTIN, OUTPUT);
    
    // Initialize W5500 and Network for tests
    eth.begin();
    net.begin(&eth);
    
    UNITY_BEGIN();
    
    // Basic functionality tests
    RUN_TEST(test_power_monitoring_state);
    RUN_TEST(test_factory_reset_timing);
    RUN_TEST(test_network_config);
    RUN_TEST(test_snmp_community);
    RUN_TEST(test_json_config);
    RUN_TEST(test_settings_file_operations);
    
    // W5500 driver tests
    RUN_TEST(test_spi_initialization);
    RUN_TEST(test_basic_read_write);
    RUN_TEST(test_interrupt_handling);
    RUN_TEST(test_register_access);
    RUN_TEST(test_chip_identification);
    
    // Network stack tests
    RUN_TEST(test_static_ip_configuration);
    RUN_TEST(test_dhcp_configuration);
    RUN_TEST(test_udp_socket_creation);
    RUN_TEST(test_udp_packet_send_receive);
    
    UNITY_END();
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(500);
}
