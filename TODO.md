# RF-UPS-Monitor TODO

An item is only ticked when it is implemented **and** its tests exist and pass (see `.clinerules`, Test Rules).

## 1. Network
- [x] W5500 bring-up via the core's lwIP driver (`Wiznet5500lwIP`), hardware reset on GPIO20
- [x] DHCP client (lwIP), lease renewal — `test/embedded/test_ethernet`, HIL
- [x] Static IP from settings — HIL `test_static_ip_applies_after_reboot` (passed with 10.10.6.249, 2026-10-16)
- [x] Boot without a serial terminal attached (3 s wait max)
- [x] No lease: keep retrying DHCP, no fallback address (decided 2026-10-16, avoids IP conflicts)
- [x] Boot does not wait for DHCP, menu available immediately — HIL `test_menu_is_available_before_dhcp_lease`
- [x] Status shows connection state (connected / waiting for lease / no link / no W5500) — HIL
- [ ] Test the "no link" and "no DHCP server" states (needs a switchable port or isolated network)
- [x] Delete the obsolete custom stack (`lib/w5500`, `lib/network`) and its tests

## 2. SNMP v1 agent
### 2.1 BER codec (pure library, native tests)
- [ ] Length encoding/decoding (short and long form)
- [ ] INTEGER, OCTET STRING, NULL, OBJECT IDENTIFIER, SEQUENCE
- [ ] TimeTicks, Counter32, Gauge32
- [ ] Reject malformed and truncated input, bounds checks on every read
### 2.2 Message handling (pure library, native tests)
- [ ] Parse Message (version 0, community, PDU)
- [ ] Community string check, wrong community silently dropped
- [ ] Build GetResponse with error-status/error-index
### 2.3 Operations
- [ ] GetRequest, noSuchName for unknown OIDs
- [ ] GetNextRequest, lexicographic OID order, endOfMib as noSuchName (v1)
### 2.4 MIB
- [ ] system group: sysDescr, sysObjectID, sysUpTime, sysContact, sysName, sysLocation
- [ ] private .1.3.6.1.4.1.63050.1 mainsPowerStatus
- [ ] OID table in sorted order, walk test
### 2.5 Integration
- [ ] UDP port 161 listener on lwIP
- [ ] HIL tests with `snmpget` / `snmpwalk` from WSL
- [ ] Response time < 100 ms

## 3. Power monitoring
- [x] Debouncer (5 ms), detection within 10 ms — `test/native/test_power_debouncer`
- [x] GPIO27 sampled at 1 kHz on core 1, seqlock hand-over to core 0 — `test/embedded/test_mains_input`
- [x] Status screen line and serial log on every change — HIL `test_status_reports_mains_power`
- [x] Verify loss/restore with real mains — HIL `test_mains_loss_and_restore_are_reported` (manual, `RFUPS_MANUAL_MAINS=1`, passed 2026-10-16)
- [ ] Expose in MIB (`mainsPowerStatus`), plus change count / last change as extra OIDs?
- [ ] SNMP trap on change? (v1 trap, needs a trap destination setting)

## 4. Configuration storage
- [x] LittleFS mount
- [x] JSON settings with validation (`lib/settings_codec`) — `test/native/test_settings_codec`
- [x] A/B copies with sequence number, power-cut safe — `test/embedded/test_settings_store`
- [x] Read-back verification on save
- [x] Load v1 files without `sequence`, remove old `active_file`
- [x] Factory defaults when no copy is valid
- [ ] Store sysName / sysLocation / sysContact
- [ ] Remove leftover `config.bak` / `config.schema.json` from older firmware

## 5. Serial menu
- [x] Show status (mode, stored static config, live interface)
- [x] Switch to DHCP
- [x] Set static IP with validation (`lib/netconfig`) — `test/native/test_netconfig`, HIL
- [x] Confirm, save, reboot; cancel leaves settings unchanged — HIL
- [x] Echo and backspace for terminals without local echo — HIL
- [ ] Set SNMP community string
- [ ] Set sysName / sysLocation / sysContact
- [ ] Show SNMP statistics

## 6. Factory reset
- [ ] 10 s hold on GPIO22, LED feedback, defaults saved, reboot — implemented, needs a test with the real button (timing within spec)
- [ ] Non-blocking hold detection (currently blocks `loop()` while held)

## 7. Testing infrastructure
- [x] `native` environment, MinGW-w64 on Windows, `-Wall -Wextra -Werror`
- [x] On-device Unity tests in `test/embedded`, auto-wait for the USB port after flashing
- [x] HIL tests in `test/hil` (Python, pyserial from PlatformIO)
- [x] Test rules in `.clinerules`, guide in `docs/TESTING.md`
- [ ] Run native tests in CI (GitHub Actions)

## 8. Security
- [ ] Request rate limiting
- [ ] Access logging
- [ ] Malformed SNMP packet fuzzing in native tests

## 9. Documentation
- [x] README: hardware, menu, storage, build, test
- [x] docs/TESTING.md
- [ ] MIB document
- [ ] CHANGELOG and version tags

## Notes
- SNMP must be v1 only and our own code; networking may use libraries
- Memory limits: 70 % flash, 50 % RAM (now 14 % / 26 %)
