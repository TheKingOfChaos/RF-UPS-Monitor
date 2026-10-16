#pragma once

/**
 * @file settings_codec.h
 * @brief Settings structure, JSON (de)serialization and A/B copy selection.
 *
 * Pure C++ plus ArduinoJson, no Arduino core or filesystem dependency, so it
 * is unit tested on the host (`pio test -e native`). The flash storage layer
 * in lib/settings uses it.
 *
 * Storage model: two copies (slot A and slot B) are kept. Every save writes
 * the slot that is NOT currently active, with sequence = active sequence + 1.
 * On load, the valid copy with the newest sequence wins. A power cut during a
 * save therefore leaves the previous copy intact.
 */

#include <stddef.h>
#include <stdint.h>

/// Increment only when the JSON layout changes incompatibly
constexpr uint32_t SETTINGS_VERSION = 1;

/// Largest serialized settings document, including the terminating NUL
constexpr size_t SETTINGS_JSON_MAX = 256;

/**
 * @brief Persistent device settings
 */
struct Settings {
    char community[32];   ///< SNMP community string, 1-31 printable characters
    bool dhcpEnabled;     ///< true = DHCP, false = static configuration below
    char staticIP[16];    ///< Static IP address, dotted quad
    char subnetMask[16];  ///< Static subnet mask, dotted quad
    char gateway[16];     ///< Static gateway, dotted quad
    uint32_t version;     ///< Layout version, must equal SETTINGS_VERSION
    uint32_t sequence;    ///< Save counter, newest valid copy wins on load
};

/**
 * @brief Why a settings document was rejected
 */
enum class SettingsParseError : uint8_t {
    None = 0,         ///< Parsed and valid
    Empty,            ///< Input is nullptr or empty
    InvalidJson,      ///< Not a JSON object
    WrongVersion,     ///< "version" missing or not SETTINGS_VERSION
    MissingField,     ///< A required field is missing or has the wrong type
    FieldTooLong,     ///< A string does not fit its buffer
    BadCommunity,     ///< Community string empty or contains non-printable characters
    BadStaticConfig   ///< DHCP disabled but static configuration is invalid
};

/**
 * @brief Which stored copy to use
 */
enum class SettingsSlot : uint8_t { None, A, B };

/**
 * @brief Fill settings with factory defaults (DHCP, community "public")
 */
void settingsDefaults(Settings& s);

/**
 * @brief Serialize settings to compact JSON
 * @param buf  Output buffer
 * @param size Buffer size in bytes, SETTINGS_JSON_MAX is always enough
 * @return Length written excluding NUL, or 0 if the buffer is too small
 */
size_t settingsToJson(const Settings& s, char* buf, size_t size);

/**
 * @brief Parse and validate a settings document
 *
 * Unknown fields are ignored. "sequence" is optional and defaults to 0, so
 * files written before it existed still load.
 *
 * @param json NUL-terminated JSON text
 * @param out  Receives the settings only when the result is None
 */
SettingsParseError settingsFromJson(const char* json, Settings& out);

/**
 * @brief Human-readable message for a parse error, for log output
 */
const char* settingsParseErrorMessage(SettingsParseError error);

/**
 * @brief Compare two settings field by field (ignores bytes after string terminators)
 */
bool settingsEqual(const Settings& a, const Settings& b);

/**
 * @brief true if sequence a is newer than b, handling 32-bit wrap-around
 */
bool sequenceIsNewer(uint32_t a, uint32_t b);

/**
 * @brief Choose the copy to load
 * @return The valid slot with the newest sequence, A on a tie, None if neither is valid
 */
SettingsSlot chooseActiveSlot(bool aValid, uint32_t aSequence, bool bValid, uint32_t bSequence);

/**
 * @brief The slot the next save writes to: the opposite of the active one, A if none
 */
SettingsSlot nextWriteSlot(SettingsSlot active);
