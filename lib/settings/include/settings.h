#pragma once

/**
 * @file settings.h
 * @brief Settings persistence on the Pico's internal flash (LittleFS).
 *
 * Two copies are stored (slot A and slot B). See settings_codec.h for the
 * storage model and validation rules. This layer only does file I/O; all
 * logic that can run on the host lives in settings_codec.
 */

#include <Arduino.h>
#include "settings_codec.h"

/// Default slot paths, kept from v1 so existing devices keep their settings
extern const char* const SETTINGS_FILE_A;
extern const char* const SETTINGS_FILE_B;

/**
 * @brief Reads and writes the two settings copies at a pair of paths
 *
 * Production code uses the default paths; tests use their own paths so they
 * never touch the device's real settings.
 */
class SettingsStore {
public:
    SettingsStore(const char* pathA, const char* pathB) : _pathA(pathA), _pathB(pathB) {}

    /**
     * @brief Load the newest valid copy
     *
     * LittleFS must already be mounted. Invalid copies are logged and ignored.
     *
     * @param out Receives the loaded settings, or factory defaults if no copy is valid
     * @return The slot loaded from, or SettingsSlot::None if defaults were used
     */
    SettingsSlot load(Settings& out);

    /**
     * @brief Save settings to the inactive slot and make it active
     *
     * The sequence number is assigned here (active sequence + 1), so callers
     * cannot make a save older than the copy already stored. The file is read
     * back and parsed before it is considered saved.
     *
     * @param s Settings to save; s.sequence and s.version are updated on success
     * @return true if the new copy was written and verified
     */
    bool save(Settings& s);

    /// Slot that the last load() or save() selected
    SettingsSlot activeSlot() const { return _active; }

    /// Sequence of the active copy
    uint32_t activeSequence() const { return _activeSequence; }

    /// Path of a slot
    const char* pathFor(SettingsSlot slot) const { return slot == SettingsSlot::B ? _pathB : _pathA; }

private:
    bool readSlot(const char* path, Settings& out);

    const char* _pathA;
    const char* _pathB;
    SettingsSlot _active = SettingsSlot::None;
    uint32_t _activeSequence = 0;
};

/// Settings in use by the firmware
extern Settings currentSettings;

/// true once loadSettings() has run
extern bool settingsLoaded;

/**
 * @brief Load currentSettings from the default store, saving defaults if no copy is valid
 * @return true if valid settings were loaded or defaults were saved
 */
bool loadSettings();

/**
 * @brief Save currentSettings to the default store
 * @return true if saved and verified
 */
bool saveSettings();
