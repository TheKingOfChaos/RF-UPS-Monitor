#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// Pin definitions
const uint8_t LED_PIN = LED_BUILTIN;      // Onboard LED
const uint8_t POWER_PIN = 27;             // Mains power detection
const uint8_t FACTORY_RESET_PIN = 22;     // Factory reset button
const uint8_t W5500_CS_PIN = 17;          // W5500 CS
const uint8_t W5500_INT_PIN = 21;         // W5500 INT
const uint8_t W5500_RST_PIN = 20;         // W5500 RST

// Settings file paths with wear leveling
extern const char* SETTINGS_FILE_A;
extern const char* SETTINGS_FILE_B;
extern const char* ACTIVE_FILE_FLAG;
extern const uint32_t SETTINGS_VERSION;

// Default settings
extern const char* DEFAULT_COMMUNITY;
extern const bool DEFAULT_DHCP_ENABLED;
extern const char* DEFAULT_STATIC_IP;
extern const char* DEFAULT_SUBNET_MASK;
extern const char* DEFAULT_GATEWAY;

// Settings structure
struct Settings {
    char community[32];        // SNMP community string
    bool dhcpEnabled;         // DHCP enable flag
    char staticIP[16];        // Static IP if DHCP disabled
    char subnetMask[16];      // Subnet mask
    char gateway[16];         // Gateway address
    uint32_t version;         // Settings version for validation
};

// Global settings instance
extern Settings currentSettings;
extern bool settingsLoaded;

// Function declarations
void loadDefaultSettings();
bool saveSettings();
bool loadSettings();
void handleFactoryReset();
bool isFactoryResetRequested();
void indicateFactoryReset();
