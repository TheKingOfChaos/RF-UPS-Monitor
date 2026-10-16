#pragma once

/**
 * @file power_debouncer.h
 * @brief Debouncing of the mains detection input (GPIO27).
 *
 * Pure C++ with no Arduino dependency, unit tested on the host
 * (`pio test -e native`). The core-1 sampler in lib/mains_input feeds it one
 * sample per millisecond.
 */

#include <stdint.h>

class PowerDebouncer {
public:
    /**
     * @param debounceMs How long the input must hold a new level before it is
     *                   accepted. Detection latency is at most debounceMs plus
     *                   one sample period.
     */
    explicit PowerDebouncer(uint32_t debounceMs) : _debounceMs(debounceMs) {}

    /**
     * @brief Set the initial state from the first reading; not counted as a change
     */
    void begin(bool level, uint32_t nowMs);

    /**
     * @brief Feed one sample
     * @param level Raw input level, true = HIGH = mains present
     * @param nowMs Current time in ms (wrap-around safe)
     * @return true if the debounced state changed with this sample
     */
    bool update(bool level, uint32_t nowMs);

    /// Debounced state, true = mains present
    bool state() const { return _stable; }

    /// Number of accepted state changes since begin()
    uint32_t changes() const { return _changes; }

    /// Time of the last accepted change, or of begin() if none
    uint32_t lastChangeMs() const { return _lastChangeMs; }

private:
    uint32_t _debounceMs;
    bool _stable = false;
    bool _pending = false;          // A different level is being timed
    uint32_t _pendingSinceMs = 0;
    uint32_t _changes = 0;
    uint32_t _lastChangeMs = 0;
};
