#include <Arduino.h>
#include <settings.h>
#include <LittleFS.h>

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
    while (!Serial) delay(10);
    Serial.println("\nRF-UPS-Monitor v3 Starting...");

    // Initialize pins
    pinMode(LED_PIN, OUTPUT);
    pinMode(POWER_PIN, INPUT);
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    pinMode(W5500_CS_PIN, OUTPUT);
    pinMode(W5500_INT_PIN, INPUT);
    pinMode(W5500_RST_PIN, OUTPUT);

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
}

void loop() {
    // Check for factory reset
    handleFactoryReset();

    // Main loop functionality
    // TODO: Implement network and SNMP handling
}
