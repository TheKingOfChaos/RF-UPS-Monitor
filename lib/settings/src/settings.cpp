#include "settings.h"

// Settings file paths with wear leveling
const char* SETTINGS_FILE_A = "data/settings_a.json";
const char* SETTINGS_FILE_B = "data/settings_b.json";
const char* ACTIVE_FILE_FLAG = "data/active_file";
const uint32_t SETTINGS_VERSION = 1;      // Increment when settings structure changes

// Default settings
const char* DEFAULT_COMMUNITY = "public";
const bool DEFAULT_DHCP_ENABLED = true;
const char* DEFAULT_STATIC_IP = "192.168.1.100";
const char* DEFAULT_SUBNET_MASK = "255.255.255.0";
const char* DEFAULT_GATEWAY = "192.168.1.1";

// Global settings instance
Settings currentSettings;
bool settingsLoaded = false;

// Initialize settings with defaults
void loadDefaultSettings() {
    // Initialize all memory to 0
    memset(&currentSettings, 0, sizeof(Settings));
    
    // Copy strings with null termination
    strncpy(currentSettings.community, DEFAULT_COMMUNITY, sizeof(currentSettings.community) - 1);
    currentSettings.community[sizeof(currentSettings.community) - 1] = '\0';
    
    currentSettings.dhcpEnabled = DEFAULT_DHCP_ENABLED;
    
    strncpy(currentSettings.staticIP, DEFAULT_STATIC_IP, sizeof(currentSettings.staticIP) - 1);
    currentSettings.staticIP[sizeof(currentSettings.staticIP) - 1] = '\0';
    
    strncpy(currentSettings.subnetMask, DEFAULT_SUBNET_MASK, sizeof(currentSettings.subnetMask) - 1);
    currentSettings.subnetMask[sizeof(currentSettings.subnetMask) - 1] = '\0';
    
    strncpy(currentSettings.gateway, DEFAULT_GATEWAY, sizeof(currentSettings.gateway) - 1);
    currentSettings.gateway[sizeof(currentSettings.gateway) - 1] = '\0';
    currentSettings.version = SETTINGS_VERSION;
}

// Save settings to flash with wear leveling
bool saveSettings() {
    StaticJsonDocument<512> doc;
    
    // Serialize settings to JSON
    doc["community"] = currentSettings.community;
    doc["dhcpEnabled"] = currentSettings.dhcpEnabled;
    doc["staticIP"] = currentSettings.staticIP;
    doc["subnetMask"] = currentSettings.subnetMask;
    doc["gateway"] = currentSettings.gateway;
    doc["version"] = currentSettings.version;

    // Determine which file to write to
    File activeFlag = LittleFS.open(ACTIVE_FILE_FLAG, "r");
    String currentFile = activeFlag.readString();
    activeFlag.close();

    const char* newFile = (currentFile == SETTINGS_FILE_A) ? SETTINGS_FILE_B : SETTINGS_FILE_A;
    
    // Write settings to the new file
    File settingsFile = LittleFS.open(newFile, "w");
    if (!settingsFile) {
        Serial.print("Failed to open file for writing: ");
        Serial.println(newFile);
        return false;
    }

    // Debug output
    Serial.println("Saving settings:");
    serializeJsonPretty(doc, Serial);
    Serial.println();

    // Serialize JSON to file
    if (serializeJson(doc, settingsFile) == 0) {
        Serial.println("Failed to write settings");
        settingsFile.close();
        return false;
    }
    settingsFile.flush();
    settingsFile.close();

    // Verify written content
    settingsFile = LittleFS.open(newFile, "r");
    if (settingsFile) {
        String content = settingsFile.readString();
        Serial.println("Verifying written content:");
        Serial.print("Raw content: '");
        Serial.print(content);
        Serial.println("'");
        
        // Try to parse the written content
        StaticJsonDocument<512> verifyDoc;
        DeserializationError verifyError = deserializeJson(verifyDoc, content);
        if (!verifyError) {
            Serial.println("Parsed content:");
            serializeJsonPretty(verifyDoc, Serial);
            Serial.println();
        } else {
            Serial.print("Failed to parse written content: ");
            Serial.println(verifyError.c_str());
        }
        settingsFile.close();
    }

    // Update active file flag
    activeFlag = LittleFS.open(ACTIVE_FILE_FLAG, "w");
    activeFlag.print(newFile);
    activeFlag.flush();
    activeFlag.close();

    // Verify active file flag
    activeFlag = LittleFS.open(ACTIVE_FILE_FLAG, "r");
    if (activeFlag) {
        String verifyFlag = activeFlag.readString();
        Serial.print("Verifying active file flag. Expected: '");
        Serial.print(newFile);
        Serial.print("', Actual: '");
        Serial.print(verifyFlag);
        Serial.println("'");
        activeFlag.close();
    }

    return true;
}

// Load settings from flash with validation
bool loadSettings() {
    // Try to read active file flag
    File activeFlag = LittleFS.open(ACTIVE_FILE_FLAG, "r");
    String activeFile;
    
    if (!activeFlag) {
        // First time setup
        activeFile = SETTINGS_FILE_A;
        activeFlag = LittleFS.open(ACTIVE_FILE_FLAG, "w");
        activeFlag.print(activeFile);
        activeFlag.flush();
        activeFlag.close();
        loadDefaultSettings();
        return saveSettings();
    }
    
    activeFile = activeFlag.readString();
    activeFlag.close();

    Serial.print("Active file is: ");
    Serial.println(activeFile);

    // Read settings file
    File settingsFile = LittleFS.open(activeFile.c_str(), "r");
    if (!settingsFile) {
        Serial.print("Failed to open active file: ");
        Serial.println(activeFile);
        
        // Try backup file
        const char* backupFile = (activeFile == SETTINGS_FILE_A) ? SETTINGS_FILE_B : SETTINGS_FILE_A;
        Serial.print("Trying backup file: ");
        Serial.println(backupFile);
        
        settingsFile = LittleFS.open(backupFile, "r");
        if (!settingsFile) {
            Serial.println("Failed to open backup file, loading defaults");
            loadDefaultSettings();
            return saveSettings();
        }
    }

    // Read file contents
    String fileContents = settingsFile.readString();
    Serial.println("Raw file contents:");
    Serial.println(fileContents);
    
    // Parse JSON
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, fileContents);
    
    // Debug output
    Serial.println("Loading settings:");
    serializeJsonPretty(doc, Serial);
    Serial.println();
    
    settingsFile.close();

    if (error) {
        Serial.print("JSON parse error: ");
        Serial.println(error.c_str());
    }

    if (error || doc["version"] != SETTINGS_VERSION) {
        loadDefaultSettings();
        return saveSettings();
    }

    // Initialize memory before loading settings
    memset(&currentSettings, 0, sizeof(Settings));
    
    // Get values from JSON
    const char* community = doc["community"] | DEFAULT_COMMUNITY;
    bool dhcpEnabled = doc["dhcpEnabled"] | DEFAULT_DHCP_ENABLED;
    const char* staticIP = doc["staticIP"] | DEFAULT_STATIC_IP;
    const char* subnetMask = doc["subnetMask"] | DEFAULT_SUBNET_MASK;
    const char* gateway = doc["gateway"] | DEFAULT_GATEWAY;
    
    Serial.println("JSON values:");
    Serial.print("community: '"); Serial.print(community); Serial.println("'");
    Serial.print("dhcpEnabled: "); Serial.println(dhcpEnabled);
    Serial.print("staticIP: "); Serial.println(staticIP);
    Serial.print("subnetMask: "); Serial.println(subnetMask);
    Serial.print("gateway: "); Serial.println(gateway);
    
    // Copy values to settings
    strlcpy(currentSettings.community, community, sizeof(currentSettings.community));
    currentSettings.community[sizeof(currentSettings.community) - 1] = '\0';
    
    currentSettings.dhcpEnabled = dhcpEnabled;
    
    strlcpy(currentSettings.staticIP, staticIP, sizeof(currentSettings.staticIP));
    currentSettings.staticIP[sizeof(currentSettings.staticIP) - 1] = '\0';
    
    strlcpy(currentSettings.subnetMask, subnetMask, sizeof(currentSettings.subnetMask));
    currentSettings.subnetMask[sizeof(currentSettings.subnetMask) - 1] = '\0';
    
    strlcpy(currentSettings.gateway, gateway, sizeof(currentSettings.gateway));
    currentSettings.gateway[sizeof(currentSettings.gateway) - 1] = '\0';
    currentSettings.version = SETTINGS_VERSION;
    
    Serial.println("Loaded settings:");
    Serial.print("community: '"); Serial.print(currentSettings.community); Serial.println("'");
    Serial.print("dhcpEnabled: "); Serial.println(currentSettings.dhcpEnabled);
    Serial.print("staticIP: "); Serial.println(currentSettings.staticIP);
    Serial.print("subnetMask: "); Serial.println(currentSettings.subnetMask);
    Serial.print("gateway: "); Serial.println(currentSettings.gateway);

    return true;
}

// Check if factory reset is requested (10-second button hold)
bool isFactoryResetRequested() {
    if (digitalRead(FACTORY_RESET_PIN) == LOW) {
        unsigned long startTime = millis();
        while (digitalRead(FACTORY_RESET_PIN) == LOW) {
            if (millis() - startTime >= 10000) {
                return true;
            }
            delay(100);
        }
    }
    return false;
}

// Visual feedback during factory reset
void indicateFactoryReset() {
    for (int i = 0; i < 5; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
    }
}

// Handle factory reset
void handleFactoryReset() {
    if (isFactoryResetRequested()) {
        indicateFactoryReset();
        loadDefaultSettings();
        saveSettings();
    }
}
