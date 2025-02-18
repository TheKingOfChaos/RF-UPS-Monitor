#pragma once

#include <Arduino.h>
#include <unity.h>
#include <settings.h>

// Common setUp and tearDown functions
inline void setUp(void) {
    // Initialize Serial for debug output
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }
    
    // Initialize common pins
    pinMode(POWER_PIN, INPUT);
    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    
    // SPI pins
    pinMode(16, INPUT);  // MISO
    pinMode(17, OUTPUT); // CS
    pinMode(18, OUTPUT); // SCK
    pinMode(19, OUTPUT); // MOSI
    pinMode(20, OUTPUT); // RST
    pinMode(21, INPUT);  // INT
    
    // Initialize filesystem
    if (!LittleFS.begin()) {
        Serial.println("Failed to initialize LittleFS");
    }
}

inline void tearDown(void) {
    // Reset any test state
    digitalWrite(17, HIGH); // Deselect chip
    digitalWrite(20, HIGH); // Release reset
    
    // Clean up filesystem
    if (LittleFS.exists(SETTINGS_FILE_A)) {
        LittleFS.remove(SETTINGS_FILE_A);
    }
    if (LittleFS.exists(SETTINGS_FILE_B)) {
        LittleFS.remove(SETTINGS_FILE_B);
    }
    if (LittleFS.exists(ACTIVE_FILE_FLAG)) {
        LittleFS.remove(ACTIVE_FILE_FLAG);
    }
}
