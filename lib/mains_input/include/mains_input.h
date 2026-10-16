#pragma once

/**
 * @file mains_input.h
 * @brief Mains detection on GPIO27, sampled on core 1.
 *
 * Core 1 calls mainsBegin() once and mainsSample() every millisecond; it is
 * the only writer. Core 0 (network, SNMP, menu) reads a consistent snapshot
 * with mainsRead(). The hand-over uses a sequence lock with memory barriers,
 * so a reader never sees a half-updated snapshot and core 1 never blocks.
 *
 * Input: HIGH = mains present, LOW = mains lost. The pin has the internal
 * pull-down enabled, so an unconnected input reads as "lost".
 */

#include <Arduino.h>

/// Input must hold a new level this long before it is accepted
constexpr uint32_t MAINS_DEBOUNCE_MS = 5;

/**
 * @brief Snapshot of the mains state, as published by core 1
 */
struct MainsSnapshot {
    bool present;            ///< Debounced state, true = mains present
    bool raw;                ///< Last raw pin level
    uint32_t changes;        ///< Accepted state changes since boot
    uint32_t lastChangeMs;   ///< millis() of the last change (boot time if none)
    uint32_t samples;        ///< Samples taken since boot, about 1000 per second
};

/**
 * @brief Configure GPIO27 and take the initial reading. Call from setup1() on core 1.
 */
void mainsBegin();

/**
 * @brief Sample the pin once and publish the result. Call every 1 ms from loop1() on core 1.
 */
void mainsSample();

/**
 * @brief Read the latest snapshot. Safe to call from core 0 at any time.
 * @return false if core 1 has not published a reading yet
 */
bool mainsRead(MainsSnapshot& out);
