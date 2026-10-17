# Changelog

All notable changes to the RF-UPS-Monitor firmware are recorded here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/): the version is `FIRMWARE_VERSION` in [include/version.h](include/version.h), shown in the boot banner and in sysDescr, and each release has a git tag `vX.Y.Z`.

## [Unreleased]

## [2.2.0] - 2026-10-17

### Added
- **Trap destination from DHCP:** enter `dhcp` as the trap destination (menu option 4). The device asks for DHCP option 224 in its DHCP messages and sends traps to the address in the ACK for its current lease. Works with any DHCP server that has option 224 configured; tested with Kea 2.2.0 (configuration examples for Kea, ISC dhcpd and dnsmasq in docs/MIB.md). New pure library `lib/dhcp_option`, tested with frames captured from the device and from Kea. Option 224 is only taken from an ACK lwIP accepted (same transaction id and server, lwIP bound by that frame), so a forged ACK from another LAN host cannot redirect traps (found by an automated security review; HIL test sends one).
- **Web interface on/off:** serial menu option 8 and a "Turn off…" button on the page (with confirmation). Off closes TCP port 80, logs every browser out and stops the mDNS announcement; kept across restarts, turned on again by option 8 or a factory reset. Turning it on works at once, also right after the page turned itself off: the core's lwIP is built without SO_REUSE, so a just-closed web connection in TIME-WAIT would block port 80 for 2 minutes; the firmware drops those entries for port 80 before reopening it, and retries every second if opening still fails (found by the HIL suite).
- **Logo** (`logo/RF-canopy-RGB-Orange.svg`) in the top bar, on the login card and as the browser tab icon (`/favicon.svg`).
- Settings fields `trapFromDhcp` and `webEnabled` (optional; older files load unchanged, older firmware ignores them).
- Status screen: `Trap destination source` and `Web interface` lines; the trap destination shown is the one in use.

## [2.1.1] - 2026-10-17

### Changed
- Web page restyled with Bootstrap 5.3.3: dark navigation bar, status card with green/red badges for mains power and the Ethernet link, settings in cards, Bootstrap forms and alerts, two columns on wide screens and one on phones. Bootstrap's CSS is built into the firmware (31 KB gzip in flash, served at `/static/bootstrap-5.3.3.min.css`, cached for a year, no login needed), so no internet access is needed. `scripts/embed_bootstrap.py` regenerates it from a pinned, integrity-checked download.
- Content-Security-Policy now allows the device's own stylesheet and Bootstrap's `data:` images; still no scripts.

## [2.1.0] - 2026-10-17

### Added
- **Hostname:** one name for SNMP sysName, the DHCP hostname (option 12) and mDNS: the device answers as `<hostname>.local` and announces the web page as `_http._tcp`. Set in serial menu option 4 ("Hostname (sysName)") or on the web page ("Device name"); changes apply immediately (mDNS rename, DHCP lease renewal), no restart.
- Status screen and web page show the hostname and the `.local` name.

### Changed
- New sysName values must be valid hostnames (1-32 letters, digits and `-`). A free-text sysName from older firmware still loads; DHCP and mDNS then use a cleaned-up version of it.
- A login with an own web password hashes 20 rounds per loop pass instead of 40 (about 5 ms), for more margin to the 100 ms SNMP limit.

## [2.0.1] - 2026-10-17

### Security
- Serial menu option 7 replaces the device default web password with a new random one, for when the default password became known (the README of 2.0.0 showed the development device's real default password as an example). The old password stops working at once and browsers logged in with it are logged out.

### Fixed
- README: the example default password is made up.

## [2.0.0] - 2026-10-17

A new major version for the second management interface: the configuration web page with login. SNMP, the MIB, the serial menu and the settings files stay compatible: files from 1.x load unchanged, and older firmware ignores the new `webPassword` field (a downgrade only loses the own web password).

### Added
- **Web page** on `http://<device-ip>/` with login (user `admin`): status, DHCP/static IP (saves and restarts), SNMP community (active immediately), restart button, log out. Pure library `lib/web_ui` (HTTP parser, sessions, pages, SHA-256) with host tests; `src/web_service.cpp` serves it without ever blocking SNMP.
- **Web password:** a random 10-character default password per device, generated on first boot, stored in `data/web_default.txt` and shown only on the serial status screen; serial menu option 6 sets an own password (stored as a salted, iterated SHA-256 hash in the new optional settings field `webPassword`) or returns to the default.
- Web security: 30 s lockout after 5 failed logins, HttpOnly SameSite=Strict session cookie, CSRF token in every form, idle sessions end after 15 min, strict Content-Security-Policy, no framing; failed logins and lockouts in the access log.
- The own-password check (2000 SHA-256 rounds, ~0.45 s on the RP2040) runs 40 rounds per loop pass, so a login never delays SNMP (measured: worst 52 ms during a login, before the change 467 ms); found by a code review before release.
- Status screen: web login line and the core 0 stack high-water mark (`Core 0 stack: N bytes never used`).
- Excalidraw diagrams in `docs/diagrams/` (system overview, firmware architecture, SNMP request flow, mains flow, settings storage, test layers), editable `.excalidraw` files with SVG exports shown in the README and docs/TESTING.md.

### Changed
- `Settings` grew by the password hash, so every temporary copy of it on core 0's 4 KB stack became static (serial menu, web page, settings parser). Settings save/load measures 1120 bytes of stack on the device (budget 1536).

## [1.0.2] - 2026-10-17

LibreNMS support tested in a real LibreNMS, and the mains tests automated. The firmware itself is unchanged apart from the version number.

### Fixed
- LibreNMS temperature sensor: low limit set to -20 C. Without it LibreNMS put the low limit 10 C below the first reading, which alarmed when the room cooled down.

### Added
- Documentation overview in the README, a real status screen example, and the trap settings in the settings file description.
- `librenms/test/compose.yml`: a throw-away LibreNMS in Docker for checking the definitions. Tested with LibreNMS 26.9.1 against the device (see docs/LIBRENMS.md), including a real mains cut raising and clearing the "State Sensor Critical" alert.
- Automated mains HIL tests: with `RFUPS_MAINS_SWITCH=dirigera` the tests switch an IKEA Dirigera smart plug feeding the mains detector (`test/hil/mains_switch.py`, `dirigera_outlet.py`). Mains loss/restore over serial and SNMP and the mains traps in v1 and v2c now pass with real mains, plus a new test that a ~1 s outage shows in mainsPowerChanges.

## [1.0.1] - 2026-10-17

### Added
- IF-MIB `ifInDiscards`: received frames the driver dropped because lwIP had no free buffer. Counted by a small subclass of the core's W5500 driver (`CountingW5500` in `src/net_stats.h`).
- IF-MIB `ifInErrors`: received frames shorter than the Ethernet header or longer than 1514 bytes.
- IF-MIB `ifInUnknownProtos`: received frames with an EtherType lwIP does not handle (IPv6 while IPv6 is off, LLDP, spanning tree, VLAN-tagged).
- Status screen line `Ethernet drops` with these counters and the send errors.

### Changed
- Received frames that lwIP drops no longer count as unicast/multicast/broadcast packets (IF-MIB counts only delivered packets there); their octets still count in ifInOctets.

### Known limits
- The W5500 has no counters for CRC errors or frames lost when its own receive buffer is full, so those are not visible. ifOutDiscards stays 0.

## [1.0.0] - 2026-10-17

First release.

### Added
- **Ethernet** with the W5500 on the Arduino-Pico core's lwIP driver: DHCP (default) or static IP, lease renewal, boot without waiting for the lease, and when no DHCP server answers the device keeps retrying instead of falling back to another address. MAC address derived from the chip ID.
- **Serial menu** on USB (115200 8N1): status, DHCP, static IP with validation, SNMP settings, access log. Echo and backspace for terminals without local echo.
- **Settings** as JSON on LittleFS in two copies (A/B) with a sequence number and read-back check, so a power cut during a save never loses them. Factory defaults when no copy is valid; files from older firmware still load.
- **Mains power monitoring** on GPIO27: sampled at 1 kHz on core 1, debounced 5 ms, change detected within 10 ms, handed to core 0 through a sequence lock; logged on the serial port and shown on the status screen.
- **Factory reset** with the GPIO22 button: 10 s hold, non-blocking (sampled on core 1), LED feedback, ignored when held at boot.
- **SNMP agent** (own code) on UDP 161, read-only, SNMPv1 and SNMPv2c; SNMPv3 is dropped and counted.
  - Get, GetNext and in v2c GetBulk; v2c exceptions (noSuchObject, noSuchInstance, endOfMibView) and errors (notWritable, noCreation, tooBig with an empty list).
  - System group with sysName, sysContact and sysLocation set from the menu, active without a reboot.
  - RF-UPS-MONITOR-MIB (enterprise 63050): mainsPowerStatus, mainsPowerChanges, mainsPowerLastChange, boardTemperature; MIB file in `mibs/`, validated with net-snmp.
  - IF-MIB ifTable and ifXTable for the Ethernet port, with 64-bit counters (Counter64, v2c only) counted by the firmware for every frame, and link state and speed from the W5500 PHY.
  - Board temperature from the RP2040's internal sensor, in 0.1 °C.
- **Traps** in SNMPv1 or SNMPv2c (setting, default v2c): coldStart, authenticationFailure (at most one per 10 s), mainsPowerLost and mainsPowerRestored with the mains objects.
- **Security**: per-source rate limit (50/s, burst 100), a global limit for unauthenticated sources (100/s) so faked source addresses cannot bypass it, a bounded separate limit for sources that know the community (200/s, at most 4, expires after 10 minutes idle, revoked when the community changes), GetBulk replies capped at 16 objects against reflection, and an access log of rejected requests (menu option 5 and serial log).
- **LibreNMS** OS detection and discovery definitions in `librenms/` (mains state sensor, temperature sensor, change counter), see [docs/LIBRENMS.md](docs/LIBRENMS.md).
- **Tests**: native unit tests for every pure library, on-device tests, hardware-in-the-loop tests over the serial menu and net-snmp, AddressSanitizer/UBSan runs, the agent checked against the real net-snmp tools without hardware, and GitHub Actions CI. Test rules in `.clinerules`, guide in [docs/TESTING.md](docs/TESTING.md).

### Known limits
- The network stack queues 5 UDP packets; larger bursts are dropped (SNMP managers retry).
- Not yet tested with the real hardware: mains traps with real mains, the factory reset button, the "no link" and "no DHCP server" states. The LibreNMS definitions are validated against the schema but not yet tried in a running LibreNMS (done after 1.0.1).

[Unreleased]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v2.2.0...HEAD
[2.2.0]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v2.1.1...v2.2.0
[2.1.1]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v2.1.0...v2.1.1
[2.1.0]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v2.0.1...v2.1.0
[2.0.1]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v1.0.2...v2.0.0
[1.0.2]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/releases/tag/v1.0.0
