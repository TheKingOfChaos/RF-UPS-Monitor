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
- `test/native`, `test/embedded`, `test/hil`: the three test layers
- `lib/w5500`, `lib/network`: obsolete custom stack, excluded with `lib_ignore`, to be deleted
