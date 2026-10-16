#pragma once

/**
 * @file factory_reset.h
 * @brief Factory reset button on GPIO22, without blocking either core.
 *
 * Core 1 samples the button every millisecond (factoryResetSample) and
 * drives the LED: slow blink while held, fast blink once the 10 s hold is
 * reached. Core 0 polls factoryResetPoll() from loop(); when core 1 reports
 * a completed hold it saves the factory defaults and reboots. SNMP and the
 * menu keep working while the button is held.
 */

#include <Arduino.h>

/**
 * @brief Configure the button and LED. Call from setup1() on core 1.
 */
void factoryResetBegin();

/**
 * @brief Sample the button and update the LED. Call every 1 ms from loop1() on core 1.
 */
void factoryResetSample();

/**
 * @brief Log button activity and perform the reset when requested. Call from loop() on core 0.
 */
void factoryResetPoll();
