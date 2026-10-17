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

## 2. SNMP v1/v2c agent
### 2.1 BER codec (pure library, native tests) — `lib/snmp_ber`, `test/native/test_snmp_ber`
- [x] Length encoding/decoding (short and long form, minimal on encode)
- [x] INTEGER, OCTET STRING, NULL, OBJECT IDENTIFIER, SEQUENCE, IpAddress
- [x] TimeTicks, Counter32, Gauge32
- [x] OID text parsing/formatting, lexicographic compare for GetNext
- [x] Reject malformed and truncated input, bounds checks on every read
- [x] Byte-exact against requests captured from net-snmp
- [x] Clean under AddressSanitizer/UBSan incl. every single-byte corruption — `scripts/sanitize.sh`
### 2.2 Message handling — `lib/snmp_agent/snmp_message`, `test/native/test_snmp_message`
- [x] Parse Message (version 0, community, PDU), max 16 varbinds, trailing bytes rejected
- [x] v2c/v3, response/trap PDUs and malformed packets reported separately
- [x] Build GetResponse; error responses echo the request varbinds (RFC 1157)
- [x] Byte-exact against net-snmp requests, every truncation and single-byte corruption
### 2.3 Operations — `lib/snmp_agent/snmp_agent`, `test/native/test_snmp_agent`
- [x] Community check (constant time), wrong community silently dropped and counted
- [x] GetRequest, noSuchName with error-index for unknown or non-leaf OIDs
- [x] GetNextRequest, lexicographic order, noSuchName past the end (net-snmp: "End of MIB")
- [x] SetRequest answered with noSuchName (everything read-only)
- [x] tooBig when the response does not fit; statistics counters
### 2.4 MIB — `lib/snmp_agent/snmp_mib`, `mibs/RF-UPS-MONITOR-MIB.txt`, `docs/MIB.md`
- [x] system group: sysDescr, sysObjectID, sysUpTime, sysContact, sysName, sysLocation, sysServices
- [x] 63050.1 mainsPowerStatus, .2 mainsPowerChanges, .3 mainsPowerLastChange
- [x] OID table sorted (tested), full walk test
- [x] Host build checked against real snmpget/snmpwalk/snmpset — `scripts/host_agent_check.sh`
- [x] MIB file validated with snmptranslate `-PW` (no warnings), names resolve in snmpget — `host_agent_check.sh`
- [x] sysContact / sysLocation / sysName configurable (settings + menu option 4), active without reboot — native `test_settings_codec`, HIL `test_snmp_settings_*`
### 2.5 Integration — `src/snmp_service.cpp`, `test/hil/test_snmp.py`
- [x] UDP port 161 on lwIP, max 4 packets per loop, oversized packets dropped
- [x] HIL tests with net-snmp from WSL: get, getnext, walk, set, wrong community, v2c
- [x] Response time < 100 ms (measured median ~6 ms, max ~20 ms over 30 requests)
- [x] Survives malformed packet floods; 5 simultaneous requests all answered
- [x] SNMP counters on the status screen
- [ ] Mains loss/restore over SNMP with real mains — HIL `test_mains_loss_and_restore_visible_over_snmp` (manual, needs you at the switch)
- [x] Known limit, accepted and documented in docs/MIB.md: the core queues 5 UDP packets (`UdpContext::rxBufMaxDepth`, hard-coded), larger bursts are dropped; one reply is held during an ARP lookup. Managers poll one request at a time and retry
### 2.7 SNMPv2c (decided 2026-10-17: v1 and v2c, no v3; traps in v1 or v2c)
- [x] Parse and answer v2c (version 1), each request in its own version; v3 dropped and counted — native + HIL
- [x] v2c exceptions: noSuchObject / noSuchInstance (Get), endOfMibView (GetNext) — native, net-snmp, HIL
- [x] GetBulkRequest: non-repeaters, max-repetitions, early stop at endOfMibView, truncation to fit (max 16 varbinds, see 8), tooBig only if nothing fits — native, net-snmp `snmpbulkget`/`snmpbulkwalk`, HIL
- [x] v2c Set: notWritable / noCreation; v2c tooBig with empty varbind list — native, net-snmp
- [x] v1 behaviour unchanged — native `test_v1_behaviour_is_unchanged_for_the_same_requests`, all v1 tests still pass
- [x] SNMPv2-Trap (sysUpTime.0, snmpTrapOID.0, objects), byte-exact against `snmptrap -v2c` — native
- [x] Trap version setting (default v2c), menu option 4, status screen — native + HIL; trap tests run in both versions
- [x] v2c and GetBulk counters on the status screen
### 2.8 IF-MIB and board temperature (for LibreNMS)
- [x] Counter64 in the BER codec — native
- [x] Frame counting (octets, unicast, multicast, broadcast, 64-bit) and W5500 PHYCFGR decoding, `lib/if_stats` — native `test_if_stats`
- [x] Counting hooked into the lwIP netif input/linkoutput, PHY read every 500 ms, link changes logged, `src/net_stats.cpp` — HIL `test_traffic_counters_count_real_packets`, `test_if_speed_and_mac_match_the_device`
- [x] ifNumber, ifTable (22 columns), ifXTable (19 columns) for interface 1 — native, net-snmp `host_agent_check.sh`, HIL
- [x] Counter64 hidden from v1 requests (RFC 3584) — native, net-snmp, HIL
- [x] boardTemperature (63050.4, 0.1 °C, RP2040 sensor averaged) in the agent and the MIB file — native, net-snmp, HIL
- [x] Status screen: board temperature, Ethernet link and traffic — HIL
- [x] LibreNMS OS detection/discovery definitions (`librenms/`), validated against the LibreNMS schemas — `docs/LIBRENMS.md`
- [ ] Try the LibreNMS definitions in a running LibreNMS
- [ ] ifInErrors / ifInDiscards from the W5500 (not visible in MAC-raw mode; reported as 0)
### 2.6 Traps — `lib/snmp_agent/snmp_trap`, `test/native/test_snmp_trap`, `test/hil/test_snmp_traps.py`
- [x] SNMPv1 Trap-PDU encoding, byte-exact against net-snmp `snmptrap`
- [x] coldStart once per boot when the network comes up — HIL (after reboot, empty ARP table)
- [x] authenticationFailure on wrong community, throttled to one per 10 s — native + HIL
- [x] Trap destination in settings and menu option 4, `-` turns traps off — native + HIL
- [x] Notifications in the MIB file (63050.0.1 / .0.2), validated with snmptranslate
- [x] Trap counters on the status screen

## 3. Power monitoring
- [x] Debouncer (5 ms), detection within 10 ms — `test/native/test_power_debouncer`
- [x] GPIO27 sampled at 1 kHz on core 1, seqlock hand-over to core 0 — `test/embedded/test_mains_input`
- [x] Status screen line and serial log on every change — HIL `test_status_reports_mains_power`
- [x] Verify loss/restore with real mains — HIL `test_mains_loss_and_restore_are_reported` (manual, `RFUPS_MANUAL_MAINS=1`, passed 2026-10-16)
- [x] Expose in MIB: mainsPowerStatus, mainsPowerChanges, mainsPowerLastChange
- [x] SNMP trap on change: mainsPowerLost / mainsPowerRestored with the three mains objects — native byte-exact test
- [ ] Mains traps with real mains — HIL `test_mains_traps_with_real_mains` (manual, `RFUPS_MANUAL_MAINS=1`)

## 4. Configuration storage
- [x] LittleFS mount
- [x] JSON settings with validation (`lib/settings_codec`) — `test/native/test_settings_codec`
- [x] A/B copies with sequence number, power-cut safe — `test/embedded/test_settings_store`
- [x] Read-back verification on save
- [x] Load v1 files without `sequence`, remove old `active_file`
- [x] Factory defaults when no copy is valid
- [x] Store sysName / sysLocation / sysContact (optional fields, older files load with defaults)
- [x] Save/load stack use measured on the device (≤ 1.5 KB of core 0's 4 KB) — `test_save_and_load_use_little_stack`
- [x] Remove leftover `config.bak` / `config.schema.json` from older firmware at boot — `test_legacy_files_are_removed_and_settings_kept`
- [x] Trap destination setting (optional field, IPv4 or empty) — native

## 5. Serial menu
- [x] Show status (mode, stored static config, live interface)
- [x] Switch to DHCP
- [x] Set static IP with validation (`lib/netconfig`) — `test/native/test_netconfig`, HIL
- [x] Confirm, save, reboot; cancel leaves settings unchanged — HIL
- [x] Echo and backspace for terminals without local echo — HIL
- [x] Set SNMP community string (menu option 4, old community refused immediately) — HIL
- [x] Set sysName / sysLocation / sysContact (Enter keeps, `-` clears, length and character checks) — HIL
- [x] Over-long input lines rejected instead of silently truncated — HIL
- [x] Show SNMP statistics (on the status screen)
- [x] Trap destination in option 4 — HIL
- [x] Access log (option 5) — HIL

## 6. Factory reset
- [x] Non-blocking: button sampled on core 1 (`lib/power_monitor/reset_button`), reset done by core 0 — native `test_reset_button`
- [x] 10 s hold counted from the debounced press, triggers once, release just before 10 s cancels, button held at boot ignored — native
- [x] LED feedback: slow blink while held, fast after trigger — native
- [x] Defaults saved (incl. trap destination), reboot within 2 s of the trigger
- [ ] Test with the real button: timing, LED, SNMP keeps answering, settings restored — HIL `test_factory_reset.py` (manual, `RFUPS_MANUAL_BUTTON=1`)

## 7. Testing infrastructure
- [x] `native` environment, MinGW-w64 on Windows, `-Wall -Wextra -Werror`
- [x] On-device Unity tests in `test/embedded`, auto-wait for the USB port after flashing
- [x] HIL tests in `test/hil` (Python, pyserial from PlatformIO)
- [x] Test rules in `.clinerules`, guide in `docs/TESTING.md`
- [x] CI (GitHub Actions): native tests, sanitizers, net-snmp check, firmware + test program build — dry run in a clean WSL copy
- [ ] First CI run on GitHub (after push)

## 8. Security
- [x] Request rate limiting: 50/s per source, burst 100, 8 sources tracked (`lib/access_control`) — native + HIL
- [x] Global limit 100/s, burst 200, so rotating/spoofed sources cannot reset the per-source buckets (found by automated security review) — native `test_rotating_sources_cannot_exceed_global_limit`
- [x] Sources with the right community are trusted: own shared bucket and evicted last, so a spoofed flood cannot starve managers — native `test_trusted_manager_is_not_starved_by_spoofed_flood`, `test_spoofed_sources_cannot_evict_trusted_ones`
- [x] Trust bounded: trusted sources share 200/s (burst 400), at most 4 trusted, trust expires after 10 min idle, cleared when the community changes — native `test_trusted_sources_share_their_own_bounded_bucket`, `test_number_of_trusted_sources_is_capped`, `test_trust_expires_after_inactivity`, `test_clear_trust_revokes_every_trusted_source`; HIL checks the clear on community change
- [x] Expired trust is swept from the whole table on every request, so idle managers do not keep trust slots or eviction protection — native `test_idle_trusted_sources_release_their_slots`, `test_expired_trust_does_not_protect_against_eviction`
- [x] Rate limiter no longer freezes after an idle gap of more than 2^31 ms — native `test_idle_gap_longer_than_24_days_refills_instead_of_freezing`
- [x] Access logging: rejected requests with source, folded repeats, menu option 5 and serial log — native + HIL
- [x] authenticationFailure trap for wrong communities, throttled (see 2.6)
- [x] GetBulk replies capped at 16 varbinds against reflection/amplification with faked sources, and GetBulk does not grant trust — native `test_getbulk_response_is_capped_against_amplification`, `test_getbulk_outcome_does_not_count_as_trust`, net-snmp, HIL
- [x] Malformed packet tests for the BER layer (truncation, every single-byte corruption, sanitizers)
- [x] Same for the message and agent layers, plus malformed floods against the device (HIL)

## 9. Documentation
- [x] README: hardware, menu, storage, build, test
- [x] docs/TESTING.md
- [x] MIB document (docs/MIB.md) and MIB file
- [x] CHANGELOG.md (Keep a Changelog) and version tags (`v1.0.0`)
- [x] docs/LIBRENMS.md

## Notes
- SNMP must be v1 and v2c (no v3) and our own code; networking may use libraries
- Memory limits: 70 % flash, 50 % RAM (1.0.0: 8.5 % / 30 %, built with -Os)
- Version: `include/version.h`; every user-visible change goes into CHANGELOG.md under [Unreleased]
- Core 0 has a 4 KB stack shared with USB/lwIP/W5500 interrupts: keep big buffers static
