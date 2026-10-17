#pragma once

/**
 * @file hostname.h
 * @brief The device hostname: one name for SNMP sysName, the DHCP hostname
 *        (option 12) and mDNS (<name>.local, with the web page announced as
 *        an _http._tcp service).
 *
 * The name comes from the sysName setting. A sysName from older firmware
 * that is not a valid hostname (spaces, ...) is converted with
 * hostnameFromText() for DHCP and mDNS; sysName itself stays as it is until
 * it is changed. Changes apply immediately: the DHCP lease is renewed with
 * the new name and mDNS announces it.
 */

/// Set the DHCP hostname from the settings. Call before Ethernet starts.
void hostnameBegin();

/// Start mDNS, or announce again after an address change. Call when the network comes up.
void hostnameNetworkUp();

/// Apply a changed sysName: DHCP renew and mDNS rename. Call after saving it.
void hostnameApply();

/// The hostname in use (valid, at most 32 characters)
const char* hostnameCurrent();

/// Announce (or stop announcing) the web page over mDNS as _http._tcp; follows the web on/off setting
void hostnameAdvertiseWeb(bool on);
