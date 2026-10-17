# Changelog

All notable changes to the RF-UPS-Monitor firmware are recorded here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses [Semantic Versioning](https://semver.org/): the version is `FIRMWARE_VERSION` in [include/version.h](include/version.h), shown in the boot banner and in sysDescr, and each release has a git tag `vX.Y.Z`.

## [Unreleased]

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
- Not yet tested with the real hardware: mains traps with real mains, the factory reset button, the "no link" and "no DHCP server" states. The LibreNMS definitions are validated against the schema but not yet tried in a running LibreNMS.

[Unreleased]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/TheKingOfChaos/RF-UPS-Monitor-v3/releases/tag/v1.0.0
