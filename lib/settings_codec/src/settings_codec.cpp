#include "settings_codec.h"

#include <ArduinoJson.h>
#include <string.h>

#include "netconfig.h"

// Factory defaults
static const char* const DEFAULT_COMMUNITY = "public";
static const char* const DEFAULT_STATIC_IP = "192.168.1.100";
static const char* const DEFAULT_SUBNET_MASK = "255.255.255.0";
static const char* const DEFAULT_GATEWAY = "192.168.1.1";

/**
 * @brief Copy a string into a fixed buffer, failing instead of truncating
 */
static bool copyField(char* dest, size_t size, const char* src) {
    size_t len = strlen(src);
    if (len >= size) return false;
    memcpy(dest, src, len + 1);
    return true;
}

void settingsDefaults(Settings& s) {
    memset(&s, 0, sizeof(s));
    copyField(s.community, sizeof(s.community), DEFAULT_COMMUNITY);
    s.dhcpEnabled = true;
    copyField(s.staticIP, sizeof(s.staticIP), DEFAULT_STATIC_IP);
    copyField(s.subnetMask, sizeof(s.subnetMask), DEFAULT_SUBNET_MASK);
    copyField(s.gateway, sizeof(s.gateway), DEFAULT_GATEWAY);
    s.version = SETTINGS_VERSION;
    s.sequence = 0;
}

size_t settingsToJson(const Settings& s, char* buf, size_t size) {
    StaticJsonDocument<384> doc;
    doc["community"] = s.community;
    doc["dhcpEnabled"] = s.dhcpEnabled;
    doc["staticIP"] = s.staticIP;
    doc["subnetMask"] = s.subnetMask;
    doc["gateway"] = s.gateway;
    doc["version"] = s.version;
    doc["sequence"] = s.sequence;

    if (buf == nullptr || measureJson(doc) + 1 > size) return 0;
    return serializeJson(doc, buf, size);
}

SettingsParseError settingsFromJson(const char* json, Settings& out) {
    if (json == nullptr || json[0] == '\0') return SettingsParseError::Empty;

    StaticJsonDocument<384> doc;
    if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return SettingsParseError::InvalidJson;

    if (!doc["version"].is<uint32_t>() || doc["version"].as<uint32_t>() != SETTINGS_VERSION) {
        return SettingsParseError::WrongVersion;
    }

    if (!doc["community"].is<const char*>() || !doc["dhcpEnabled"].is<bool>() ||
        !doc["staticIP"].is<const char*>() || !doc["subnetMask"].is<const char*>() ||
        !doc["gateway"].is<const char*>()) {
        return SettingsParseError::MissingField;
    }
    if (!doc["sequence"].isNull() && !doc["sequence"].is<uint32_t>()) {
        return SettingsParseError::MissingField;
    }

    Settings s;
    memset(&s, 0, sizeof(s));
    if (!copyField(s.community, sizeof(s.community), doc["community"]) ||
        !copyField(s.staticIP, sizeof(s.staticIP), doc["staticIP"]) ||
        !copyField(s.subnetMask, sizeof(s.subnetMask), doc["subnetMask"]) ||
        !copyField(s.gateway, sizeof(s.gateway), doc["gateway"])) {
        return SettingsParseError::FieldTooLong;
    }

    if (s.community[0] == '\0') return SettingsParseError::BadCommunity;
    for (const char* c = s.community; *c; c++) {
        if (*c < 0x20 || *c > 0x7E) return SettingsParseError::BadCommunity;
    }

    s.dhcpEnabled = doc["dhcpEnabled"];
    if (!s.dhcpEnabled &&
        validateStaticConfig(s.staticIP, s.subnetMask, s.gateway) != StaticConfigError::None) {
        return SettingsParseError::BadStaticConfig;
    }

    s.version = SETTINGS_VERSION;
    s.sequence = doc["sequence"] | 0u;

    out = s;
    return SettingsParseError::None;
}

const char* settingsParseErrorMessage(SettingsParseError error) {
    switch (error) {
    case SettingsParseError::None:            return "OK";
    case SettingsParseError::Empty:           return "empty file";
    case SettingsParseError::InvalidJson:     return "invalid JSON";
    case SettingsParseError::WrongVersion:    return "wrong or missing version";
    case SettingsParseError::MissingField:    return "missing or mistyped field";
    case SettingsParseError::FieldTooLong:    return "field too long";
    case SettingsParseError::BadCommunity:    return "invalid community string";
    case SettingsParseError::BadStaticConfig: return "invalid static IP configuration";
    }
    return "unknown error";
}

bool settingsEqual(const Settings& a, const Settings& b) {
    return strcmp(a.community, b.community) == 0 && a.dhcpEnabled == b.dhcpEnabled &&
           strcmp(a.staticIP, b.staticIP) == 0 && strcmp(a.subnetMask, b.subnetMask) == 0 &&
           strcmp(a.gateway, b.gateway) == 0 && a.version == b.version && a.sequence == b.sequence;
}

bool sequenceIsNewer(uint32_t a, uint32_t b) {
    // Serial number arithmetic: a is newer if it is ahead by less than half the range
    return (int32_t)(a - b) > 0;
}

SettingsSlot chooseActiveSlot(bool aValid, uint32_t aSequence, bool bValid, uint32_t bSequence) {
    if (aValid && bValid) return sequenceIsNewer(bSequence, aSequence) ? SettingsSlot::B : SettingsSlot::A;
    if (aValid) return SettingsSlot::A;
    if (bValid) return SettingsSlot::B;
    return SettingsSlot::None;
}

SettingsSlot nextWriteSlot(SettingsSlot active) {
    return active == SettingsSlot::A ? SettingsSlot::B : SettingsSlot::A;
}
