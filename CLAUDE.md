# RF-UPS-Monitor v3

Raspberry Pi Pico (RP2040) with a W5500 Ethernet module: a mains power monitor that will be read over SNMP v1, configured over a USB serial menu.

The project rules are in `.clinerules` and apply to all work here, including its testing rules:

@.clinerules

## Commands

`pio` is not on PATH: use `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`.

| Task | Command |
|---|---|
| Build firmware | `pio run -e pico` |
| Flash firmware | `pio run -e pico -t upload` |
| Unit tests (PC) | `pio test -e native` |
| On-device tests | `pio test -e pico` (replaces the firmware, reflash afterwards) |
| HIL tests | `%USERPROFILE%\.platformio\penv\Scripts\python.exe -m unittest discover -s test/hil -v` |
| Sanitizers | `wsl -d Ubuntu-24.04 -- bash scripts/sanitize.sh` (after `pio test -e native`) |
| SNMP vs net-snmp, no hardware | `wsl -d Ubuntu-24.04 -- bash scripts/host_agent_check.sh` |
| SNMP HIL only | `... python.exe -m unittest discover -s test/hil -p "test_snmp.py" -v` |

Details and setup are in `docs/TESTING.md`.

## Working with the hardware

- Pico on `COM16`, 115200 baud. The firmware waits up to 3 s for a terminal, and needs DTR set to print.
- If upload or tests fail with "Access denied" on COM16, PuTTY is probably open. Ask the user to close it; do not kill it.
- snmpwalk/snmpget are in WSL: `wsl -d Ubuntu-24.04 -- snmpwalk -v1 -c public <ip>`.

## Layout

- `src/`: firmware entry (`main.cpp`), serial menu (`cli.cpp`), factory reset
- `include/pins.h`: all GPIO assignments
- `lib/netconfig`, `lib/settings_codec`: pure C++, host tested
- `lib/settings`: LittleFS A/B storage on top of `settings_codec`
- `lib/snmp_ber`: BER codec and OIDs (pure, host tested, sanitizer checked)
- `lib/snmp_agent`: SNMP v1 messages, MIB table, request handling (pure); `src/snmp_service.cpp` runs it on UDP 161. MIB docs in `docs/MIB.md`
- `lib/power_monitor` (pure: mains debouncer, reset button) and `lib/mains_input` (GPIO27 on core 1, `setup1()`/`loop1()` in `main.cpp`); `src/factory_reset.cpp` samples GPIO22 on core 1
- `lib/access_control` (pure): SNMP rate limiter and access log
- CI: `.github/workflows/tests.yml` runs native tests, sanitizers, net-snmp check and the firmware build
- `test/native`, `test/embedded`, `test/hil`: the three test layers
