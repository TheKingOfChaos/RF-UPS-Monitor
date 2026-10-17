#pragma once

/**
 * @file web_service.h
 * @brief The configuration web page on TCP port 80 (lib/web_ui does the parsing,
 *        login and pages; this file connects it to the network and the device).
 *
 * One connection at a time, fully non-blocking: the request is read as it
 * arrives, the response is written only as fast as the TCP send buffer takes
 * it, and a connection that does not finish within WEB_CONNECTION_TIMEOUT_MS
 * is closed. SNMP is served in between, so a slow or hostile browser cannot
 * stall the agent.
 *
 * Login: user "admin". Until a password is set with serial menu option 6 the
 * device default password is used: 10 random characters generated once and
 * kept in data/web_default.txt (not touched by a factory reset), shown only
 * on the serial status screen.
 */

#include <Arduino.h>

static const uint32_t WEB_CONNECTION_TIMEOUT_MS = 5000;

/// Load or create the default password and start listening. Call after Ethernet started.
void webBegin();

/// Serve the web page; call from loop() on core 0
void webPoll();

/// The device default password (shown on the serial status screen)
const char* webDefaultPassword();

/// true while no own password has been set
bool webUsesDefaultPassword();

/**
 * @brief Set the web password (serial menu), or nullptr to go back to the device default
 *
 * Saves the settings and logs every browser out.
 * @return false if the password is invalid or saving failed (nothing changed then)
 */
bool webSetPassword(const char* password);
