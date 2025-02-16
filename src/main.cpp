#include <Arduino.h>
#include <settings.h>

void setup() {
    // Initialize pins
    pinMode(LED_PIN, OUTPUT);
    pinMode(POWER_PIN, INPUT);
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    pinMode(W5500_CS_PIN, OUTPUT);
    pinMode(W5500_INT_PIN, INPUT);
    pinMode(W5500_RST_PIN, OUTPUT);

    // Initialize filesystem
    if (!LittleFS.begin()) {
        // Flash error pattern on LED
        while (1) {
            digitalWrite(LED_PIN, HIGH);
            delay(200);
            digitalWrite(LED_PIN, LOW);
            delay(200);
        }
    }

    // Load settings
    settingsLoaded = loadSettings();
}

void loop() {
    // Check for factory reset
    handleFactoryReset();

    // Main loop functionality
    // TODO: Implement network and SNMP handling
}
