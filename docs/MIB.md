# SNMP MIB

The device runs an SNMPv1 agent on UDP port 161 and sends SNMPv1 traps. Every object is read-only.

- **Version:** SNMPv1 only. v2c and v3 requests get no response.
- **Community:** from the settings, default `public`, changed with menu option 4 (active immediately). A request with the wrong community gets no response; it is counted on the status screen.
- **sysName, sysContact, sysLocation:** set with menu option 4; read-only over SNMP.
- **Operations:** GetRequest, GetNextRequest (and so `snmpwalk`). SetRequest is answered with `noSuchName`.
- **Enterprise number:** 63050 (`1.3.6.1.4.1.63050`), assigned to this project.
- **MIB file:** [mibs/RF-UPS-MONITOR-MIB.txt](../mibs/RF-UPS-MONITOR-MIB.txt) (SMIv2).

## Objects

| OID | Name | Type | Value |
|---|---|---|---|
| `1.3.6.1.2.1.1.1.0` | sysDescr | OCTET STRING | `RF-UPS-Monitor v3.1.0 mains power monitor (RP2040, W5500)` |
| `1.3.6.1.2.1.1.2.0` | sysObjectID | OBJECT IDENTIFIER | `1.3.6.1.4.1.63050` |
| `1.3.6.1.2.1.1.3.0` | sysUpTime | TimeTicks | Hundredths of a second since boot |
| `1.3.6.1.2.1.1.4.0` | sysContact | OCTET STRING | From the settings (menu option 4), default empty |
| `1.3.6.1.2.1.1.5.0` | sysName | OCTET STRING | From the settings (menu option 4), default `rf-ups-monitor` |
| `1.3.6.1.2.1.1.6.0` | sysLocation | OCTET STRING | From the settings (menu option 4), default empty |
| `1.3.6.1.2.1.1.7.0` | sysServices | INTEGER | 72 (end-to-end + applications) |
| `1.3.6.1.4.1.63050.1.0` | mainsPowerStatus | INTEGER | `1` = present, `0` = lost |
| `1.3.6.1.4.1.63050.2.0` | mainsPowerChanges | Counter32 | Mains changes since boot (2 per outage) |
| `1.3.6.1.4.1.63050.3.0` | mainsPowerLastChange | TimeTicks | sysUpTime at the last change, 0 if none |

`mainsPowerStatus` is debounced: a change must last 5 ms to count and is reported within 10 ms. To detect an outage between two polls, compare `mainsPowerChanges` with the previous poll; `mainsPowerLastChange` tells you when it happened.

## Traps

SNMPv1 traps are sent to the trap destination set in menu option 4 (UDP 162, community = the configured community). With no destination set, no traps are sent.

| Trap | generic-trap | specific-trap | Variable bindings | When |
|---|---|---|---|---|
| coldStart | 0 | 0 | none | Once per boot, when the network first comes up |
| authenticationFailure | 4 | 0 | none | Request with a wrong community; at most one per 10 s |
| mainsPowerLost | 6 (enterpriseSpecific) | 1 | mainsPowerStatus, mainsPowerChanges, mainsPowerLastChange | Mains lost |
| mainsPowerRestored | 6 (enterpriseSpecific) | 2 | same | Mains restored |

All traps carry enterprise `1.3.6.1.4.1.63050` and the device's IP as agent-addr. The time-stamp of a mains trap is the moment of the change (equal to mainsPowerLastChange), not when the trap was sent. In the MIB file the mains traps are the notifications `mainsPowerLost` (`63050.0.1`) and `mainsPowerRestored` (`63050.0.2`), which RFC 3584 maps to the v1 traps above.

Traps are not acknowledged in SNMPv1, so a manager should also poll `mainsPowerChanges`: if it changed between two polls, an outage happened even if its traps were lost.

To receive traps on Linux, run `snmptrapd -f -Lo` (package `snmptrapd`). On Windows, allow inbound UDP 162 in the firewall for the receiving program.

## Rate limiting and access log

Each source address may send 50 requests per second on average, with bursts of up to 100. Requests beyond that are dropped without a response and counted on the status screen ("rate limited"). Up to 8 sources are tracked at once; when more appear, the one seen least recently is forgotten. A flood from one source does not affect others.

On top of that, all untrusted sources together may send at most 100 requests per second (bursts of 200). Source addresses are easy to fake with UDP, and a new address starts with a full bucket, so without this global limit an attacker rotating through many addresses could bypass the per-source limit. A request refused for its own source does not count against the global limit.

A source becomes trusted once it sends a request with the right community. Trusted sources (your SNMP managers) use a separate shared limit of 200 requests per second (bursts of 400) instead of the untrusted one, so a flood from addresses that do not know the community cannot starve them. They are also kept in the 8-entry table in preference to untrusted sources, so such a flood cannot push them out and reset their state.

Trust is deliberately bounded, because the community is only a weak secret: the default `public` is well known, and SNMPv1 sends it in clear text.
- At most 4 sources are trusted at once.
- Trust expires after 10 minutes without requests.
- Changing the community (menu option 4) revokes all trust immediately.
- All trusted sources together stay within 200/s, so knowing the community does not give unlimited throughput.

Anyone who knows the community is treated like a manager; that is a limit of SNMPv1. **Change the community from `public`** to keep this protection meaningful. The limiter keeps working after any idle period, including gaps of more than 24.8 days (the point where `millis()` differences turn negative when read as signed values).

Rejected requests are recorded in the access log (menu option 5): wrong community, SNMPv2c/v3, malformed or oversized packets, rate-limited requests and SetRequests, with the source address. The newest 16 entries are kept; repeats from the same source are folded into one entry with a count.

## Examples

From Linux or WSL with net-snmp (`apt install snmp`):

```bash
# Everything
snmpwalk -v1 -c public 10.10.6.111

# Mains state
snmpget -v1 -c public 10.10.6.111 1.3.6.1.4.1.63050.1.0
# .1.3.6.1.4.1.63050.1.0 = INTEGER: 1

# Mains state, change count and last change in one request
snmpget -v1 -c public 10.10.6.111 1.3.6.1.4.1.63050.1.0 1.3.6.1.4.1.63050.2.0 1.3.6.1.4.1.63050.3.0
```

With the MIB file, the tools show names instead of numbers:

```bash
snmpwalk -v1 -c public -M +./mibs -m +RF-UPS-MONITOR-MIB 10.10.6.111 1.3.6.1.4.1.63050
# RF-UPS-MONITOR-MIB::mainsPowerStatus.0 = INTEGER: present(1)
# RF-UPS-MONITOR-MIB::mainsPowerChanges.0 = Counter32: 0
# RF-UPS-MONITOR-MIB::mainsPowerLastChange.0 = Timeticks: (0) 0:00:00.00
```

This needs the standard IETF MIBs, which Debian/Ubuntu ship separately: `sudo apt install snmp-mibs-downloader`.

## Errors

| Situation | Response |
|---|---|
| GetRequest for an OID that is not an object (including non-leaf OIDs such as `1.3.6.1.2.1.1.1`) | `noSuchName`, error-index = position of the first unknown OID |
| GetNextRequest past the last object | `noSuchName` (net-snmp prints "End of MIB" during a walk) |
| SetRequest | `noSuchName` for the first varbind |
| Response would exceed 1472 bytes | `tooBig` |
| Wrong community, not SNMPv1, malformed, trap or response PDU, more than 16 varbinds | No response (logged in the access log) |
| Source over the rate limit | No response (logged) |

## Limits

- Up to 16 variable bindings per request.
- Requests up to 1472 bytes (one Ethernet frame); larger packets are dropped.
- The network stack queues up to 5 UDP packets. Requests sent in a burst beyond that are dropped; SNMP managers retry, and normal polling or `snmpwalk` sends one request at a time.
- If the device has not talked to a manager for a few minutes, it first has to look up the manager's MAC address (ARP). lwIP holds one reply during that lookup, so only one request of a burst arriving at that moment is answered.
- Typical response time is about 4 ms on the local network (requirement: 100 ms).

## Implementation

| Layer | Code | Tests |
|---|---|---|
| BER encoding/decoding, OIDs | `lib/snmp_ber` | `test/native/test_snmp_ber` |
| Message parsing and responses | `lib/snmp_agent/snmp_message` | `test/native/test_snmp_message` |
| MIB table, Get/GetNext/Set handling | `lib/snmp_agent/snmp_mib`, `snmp_agent` | `test/native/test_snmp_agent` |
| Trap encoding, throttle | `lib/snmp_agent/snmp_trap` | `test/native/test_snmp_trap` (byte-exact vs net-snmp `snmptrap`) |
| Rate limiter, access log | `lib/access_control` | `test/native/test_access_control` |
| Traps on the device | `src/snmp_service.cpp` | `test/hil/test_snmp_traps.py` |
| Against real net-snmp, without hardware | `test/host/host_agent.cpp` | `scripts/host_agent_check.sh` |
| UDP port 161 on the device | `src/snmp_service.cpp` | `test/hil/test_snmp.py` |

To add an object: add a getter and an entry in `lib/snmp_agent/src/snmp_mib.cpp` (keeping the table sorted), extend `test_mib_contains_expected_objects` and the walk checks, add it to the MIB file and to this table.
