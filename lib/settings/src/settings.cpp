#include "settings.h"

#include <LittleFS.h>

const char* const SETTINGS_FILE_A = "data/settings_a.json";
const char* const SETTINGS_FILE_B = "data/settings_b.json";

// Pointer file used by v1 storage, removed on first load
static const char* const LEGACY_ACTIVE_FILE = "data/active_file";

Settings currentSettings;
bool settingsLoaded = false;

static SettingsStore defaultStore(SETTINGS_FILE_A, SETTINGS_FILE_B);

/**
 * @brief Name of a slot for log output
 */
static const char* slotName(SettingsSlot slot) {
    switch (slot) {
    case SettingsSlot::A: return "A";
    case SettingsSlot::B: return "B";
    default:              return "none";
    }
}

bool SettingsStore::readSlot(const char* path, Settings& out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;

    char buf[SETTINGS_JSON_MAX];
    size_t len = f.readBytes(buf, sizeof(buf) - 1);
    bool tooBig = f.available() > 0;
    f.close();
    buf[len] = '\0';

    if (tooBig) {
        Serial.printf("Settings %s: file too large, ignored\n", path);
        return false;
    }

    SettingsParseError err = settingsFromJson(buf, out);
    if (err != SettingsParseError::None) {
        Serial.printf("Settings %s: %s, ignored\n", path, settingsParseErrorMessage(err));
        return false;
    }
    return true;
}

SettingsSlot SettingsStore::load(Settings& out) {
    Settings a, b;
    bool aValid = readSlot(_pathA, a);
    bool bValid = readSlot(_pathB, b);

    _active = chooseActiveSlot(aValid, a.sequence, bValid, b.sequence);
    switch (_active) {
    case SettingsSlot::A: out = a; break;
    case SettingsSlot::B: out = b; break;
    default:              settingsDefaults(out); break;
    }
    _activeSequence = out.sequence;
    return _active;
}

bool SettingsStore::save(Settings& s) {
    SettingsSlot target = nextWriteSlot(_active);
    const char* path = pathFor(target);

    Settings toWrite = s;
    toWrite.version = SETTINGS_VERSION;
    toWrite.sequence = (_active == SettingsSlot::None) ? 1 : _activeSequence + 1;

    char json[SETTINGS_JSON_MAX];
    size_t len = settingsToJson(toWrite, json, sizeof(json));
    if (len == 0) {
        Serial.println("Settings: serialization failed");
        return false;
    }

    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.printf("Settings %s: cannot open for writing\n", path);
        return false;
    }
    size_t written = f.write((const uint8_t*)json, len);
    f.close();
    if (written != len) {
        Serial.printf("Settings %s: short write (%u of %u bytes)\n", path, (unsigned)written, (unsigned)len);
        return false;
    }

    // Only switch to the new copy once it reads back identical
    Settings verify;
    if (!readSlot(path, verify) || !settingsEqual(verify, toWrite)) {
        Serial.printf("Settings %s: verification failed, keeping previous copy\n", path);
        return false;
    }

    _active = target;
    _activeSequence = toWrite.sequence;
    s = toWrite;
    return true;
}

bool loadSettings() {
    SettingsSlot slot = defaultStore.load(currentSettings);

    if (LittleFS.exists(LEGACY_ACTIVE_FILE)) {
        LittleFS.remove(LEGACY_ACTIVE_FILE);
    }

    if (slot == SettingsSlot::None) {
        Serial.println("Settings: no valid copy, saving factory defaults");
        settingsLoaded = saveSettings();
        return settingsLoaded;
    }

    Serial.printf("Settings: loaded slot %s (sequence %lu), %s\n", slotName(slot),
                  (unsigned long)currentSettings.sequence,
                  currentSettings.dhcpEnabled ? "DHCP" : "static IP");
    settingsLoaded = true;
    return true;
}

bool saveSettings() {
    bool ok = defaultStore.save(currentSettings);
    if (ok) {
        Serial.printf("Settings: saved slot %s (sequence %lu)\n", slotName(defaultStore.activeSlot()),
                      (unsigned long)currentSettings.sequence);
    }
    return ok;
}
