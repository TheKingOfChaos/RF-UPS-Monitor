#include <Arduino.h>
#include <settings.h>
#include <LittleFS.h>
#include <W5500lwIP.h>
#include "cli.h"
#include "factory_reset.h"
#include "pins.h"

// W5500 on SPI0 (MISO 16, CS 17, SCK 18, MOSI 19) with interrupt on GPIO21
Wiznet5500lwIP eth(W5500_CS_PIN, SPI, W5500_INT_PIN);

const uint32_t DHCP_WAIT_MS = 15000;      // How long setup() waits for a lease

/**
 * @brief Print the current interface addresses
 */
void printNetworkInfo() {
    uint8_t mac[6];
    eth.macAddress(mac);
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print("IP: ");      Serial.println(eth.localIP());
    Serial.print("Subnet: ");  Serial.println(eth.subnetMask());
    Serial.print("Gateway: "); Serial.println(eth.gatewayIP());
    Serial.print("DNS: ");     Serial.println(eth.dnsIP());
}

/**
 * @brief Reset the W5500 and bring up the interface using DHCP or static settings
 * @return true if the W5500 was detected
 */
bool startEthernet() {
    // Hardware reset of the W5500
    digitalWrite(W5500_RST_PIN, LOW);
    delay(1);
    digitalWrite(W5500_RST_PIN, HIGH);
    delay(50);

    SPI.setRX(W5500_MISO_PIN);
    SPI.setCS(W5500_CS_PIN);
    SPI.setSCK(W5500_SCK_PIN);
    SPI.setTX(W5500_MOSI_PIN);

    if (!currentSettings.dhcpEnabled) {
        IPAddress ip, subnet, gateway;
        if (ip.fromString(currentSettings.staticIP) &&
            subnet.fromString(currentSettings.subnetMask) &&
            gateway.fromString(currentSettings.gateway)) {
            eth.config(ip, gateway, subnet, gateway);
        } else {
            Serial.println("Invalid static IP settings, falling back to DHCP");
        }
    }

    if (!eth.begin()) {
        Serial.println("W5500 not detected - check wiring");
        return false;
    }
    eth.setDefault();

    Serial.print(currentSettings.dhcpEnabled ? "Waiting for DHCP lease" : "Using static IP");
    uint32_t start = millis();
    while (!eth.connected() && millis() - start < DHCP_WAIT_MS) {
        Serial.print(".");
        delay(500);
    }
    Serial.println();

    if (eth.connected()) {
        printNetworkInfo();
    } else {
        Serial.printf("No IP address after %lu ms (link %s), still trying in background\n",
                      DHCP_WAIT_MS, eth.isLinked() ? "up" : "down");
    }
    return true;
}

void listFiles(const char * dirname, uint8_t levels) {
    Serial.printf("\nListing directory: %s\n", dirname);

    File root = LittleFS.open(dirname, "r");
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels) {
                listFiles(file.name(), levels - 1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("  SIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    // Wait briefly for a terminal, but don't block boot when none is attached
    uint32_t serialWait = millis();
    while (!Serial && millis() - serialWait < 3000) delay(10);
    Serial.println("\nRF-UPS-Monitor v3 Starting...");

    // Initialize pins
    pinMode(LED_PIN, OUTPUT);
    pinMode(POWER_PIN, INPUT);
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    pinMode(W5500_INT_PIN, INPUT);
    pinMode(W5500_RST_PIN, OUTPUT);
    digitalWrite(W5500_RST_PIN, HIGH);

    // Initialize filesystem
    if (!LittleFS.begin()) {
        Serial.println("LittleFS Mount Failed");
        // Flash error pattern on LED
        while (1) {
            digitalWrite(LED_PIN, HIGH);
            delay(200);
            digitalWrite(LED_PIN, LOW);
            delay(200);
        }
    }
    Serial.println("LittleFS Mounted Successfully");
    
    // Show filesystem contents
    listFiles("/", 1);
    
    // Print filesystem info
    FSInfo fs_info;
    LittleFS.info(fs_info);
    Serial.printf("\nFilesystem Info:\n");
    Serial.printf("Total space: %u bytes\n", fs_info.totalBytes);
    Serial.printf("Used space: %u bytes\n", fs_info.usedBytes);
    Serial.printf("Block size: %u bytes\n", fs_info.blockSize);
    Serial.printf("Page size: %u bytes\n", fs_info.pageSize);

    // Load settings
    settingsLoaded = loadSettings();

    // Bring up Ethernet
    startEthernet();

    // Serial configuration menu
    cliBegin();
}

void loop() {
    // Report when the address is obtained or lost
    static bool wasConnected = eth.connected();
    if (eth.connected() != wasConnected) {
        wasConnected = eth.connected();
        if (wasConnected) {
            Serial.println("Network up");
            printNetworkInfo();
        } else {
            Serial.println("Network down");
        }
    }

    // Serial configuration menu
    cliProcess();

    // Check for factory reset
    handleFactoryReset();

    // Main loop functionality
    // TODO: Implement network and SNMP handling
}
