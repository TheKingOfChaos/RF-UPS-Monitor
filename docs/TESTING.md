# Testing

The project has three test layers. Each one tests what the layer below cannot.

| Layer | Folder | Runs on | Framework | Command | Time |
|---|---|---|---|---|---|
| Unit | `test/native/` | Your PC | Unity | `pio test -e native` | ~5 s |
| On-device | `test/embedded/` | The Pico | Unity | `pio test -e pico` | ~35 s |
| Hardware-in-the-loop (HIL) | `test/hil/` | PC talking to flashed Pico | Python `unittest` | see below | ~20 s |

The rules for writing tests are in [.clinerules](../.clinerules), section *Testing Requirements → Test Rules*.

## One-time setup

### Host compiler (for `native`)

The native tests need `g++` on `PATH`. On Windows, install MinGW-w64 (WinLibs):

```powershell
winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e
```

Restart VS Code (or open a new terminal) afterwards so `PATH` is refreshed, then check with `g++ --version`.

### USB driver (for flashing)

`pio run -t upload` and `pio test -e pico` use picotool, which needs the WinUSB driver on the Pico's boot interface. Do this once per PC:

1. Hold BOOTSEL while plugging in the Pico (the `RPI-RP2` drive appears).
2. Run [Zadig](https://zadig.akeo.ie), **Options → List All Devices**.
3. Select **RP2 Boot (Interface 1)** (USB ID `2E8A 0003 01`). Do not pick Interface 0, that is the drive.
4. Target driver **WinUSB**, click **Install Driver**, then replug.

### Serial port

The Pico is expected on `COM16` (set in `platformio.ini`; the HIL tests read `RFUPS_PORT`). Close PuTTY or any serial monitor first: an open port gives "Access denied" and the upload fails.

## Running the tests

`pio` is not on `PATH` by default. Use the full path, or the PlatformIO terminal / Testing panel in VS Code.

```powershell
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

# Unit tests on the PC, no hardware needed
& $pio test -e native

# On-device tests: flashes each test program to the Pico in turn
& $pio test -e pico

# One suite only
& $pio test -e native -f native/test_netconfig
& $pio test -e pico   -f embedded/test_settings_store
```

### HIL tests

These run against the real firmware, so flash it first:

```powershell
& $pio run -e pico -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m unittest discover -s test/hil -v
```

They use the Python and `pyserial` that come with PlatformIO, so there is nothing to install.

The device must be in DHCP mode when the run starts, and is left in DHCP mode. The menu is available before the network is up, so tests that need the network call `Device.wait_connected()` first. The static IP test is skipped unless you give it a free address:

```powershell
$env:RFUPS_STATIC_IP = "10.10.6.250"   # must not be in use on the LAN
$env:RFUPS_STATIC_MASK = "255.255.0.0" # optional, default 255.255.0.0
$env:RFUPS_STATIC_GW = "10.10.0.1"     # optional, default: current DHCP gateway
```

Flashing an embedded test replaces the firmware, so always reflash with `pio run -e pico -t upload` before HIL tests or normal use.

## What is covered

### `test/native/test_netconfig`: `lib/netconfig`
- IPv4 parsing: valid addresses, octet range, octet count, stray characters, signs, whitespace, empty and null input, output left untouched on failure
- Subnet masks: every prefix /1 to /32 accepted, zero and non-contiguous masks rejected
- Static configuration: first failing field reported, network/broadcast host rejected, /31 and /32 handled, gateway outside the subnet or equal to the host rejected

### `test/native/test_settings_codec`: `lib/settings_codec`
- Defaults and JSON round trip, maximum-length fields fit the buffer
- v1 files without `sequence` still load (backward compatibility)
- Rejection of empty, malformed, wrong-version, mistyped, over-long and unprintable data, with no truncation
- Static configuration is validated only when DHCP is off
- Sequence comparison across 32-bit wrap-around, A/B slot selection, and fallback when the newer copy is invalid

### `test/embedded/test_settings_store`: `lib/settings` on LittleFS
Uses `test/settings_a.json` and `test/settings_b.json`, never the real settings.
- Save and reload, slot alternation, newest copy wins
- A half-written newest copy falls back to the previous one, and the next save overwrites the corrupt copy
- Both copies corrupt, or a file that is too large: defaults are used
- A v1 file is loaded and replaced by a newer copy
- Saving factory defaults (sequence 0) still beats the stored copy

### `test/embedded/test_ethernet`: W5500 and lwIP
- W5500 detected over SPI, link up, DHCP lease with a gateway, gateway answers ping

### `test/hil/test_serial_menu.py`: the flashed firmware
- Menu text, status output with state, lease and MAC, ping from the PC
- Boot does not wait for DHCP: the prompt appears within 5 s, before `Network up`, and the lease still arrives
- Unknown input, backspace editing
- Each invalid entry rejected with one clear message; cancel leaves settings unchanged
- DHCP save → reboot → reconnect → ping
- Static IP applied after reboot, then DHCP restored (opt-in)

## Adding a test

1. Decide the layer. If the code can run without the Pico, move it into a pure library under `lib/` and test it in `test/native/`. Use `test/embedded/` only for flash, SPI, GPIO or timing, and `test/hil/` for end-to-end behaviour.
2. Add the test to the matching suite, or create a new suite folder named `test_<area>` with a `test_main.cpp`:
   - native: `int main()` with `UNITY_BEGIN()`, `RUN_TEST(...)`, `return UNITY_END();`
   - embedded: the same inside `setup()`, after waiting up to 10 s for `Serial`, and an empty `loop()`
3. Name it `test_<what>_<expected behaviour>` and assert on the code under test.
4. Check it can fail: break the code briefly (or write the test before the fix) and watch it go red.
5. Run all native and embedded tests before committing.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `g++` not found / native build fails immediately | MinGW not installed or `PATH` not refreshed. Restart VS Code. |
| `could not open port 'COM16'` | Port in use (PuTTY, serial monitor) or Pico unplugged. |
| `No accessible RP2040 devices in BOOTSEL mode` | WinUSB driver missing on the boot interface, see Zadig above. |
| Embedded test hangs with no output | Board not running the test program. Press reset or replug. `scripts/wait_for_port.py` already waits for the port to come back after flashing. |
| `test_dhcp_lease_obtained` fails | No cable, no link LED, or no DHCP server on that network. |
| HIL tests skipped with "device is in Static mode" | Switch to DHCP with menu option 2 first. |
