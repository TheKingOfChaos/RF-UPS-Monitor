# Raspberry Pi Pico SNMP Agent with Power Monitoring

A lightweight SNMP agent implementation for Raspberry Pi Pico with W5500 Ethernet module, featuring mains power monitoring capabilities. This project implements SNMP v1 from scratch without external libraries, providing basic monitoring and management functionality through SNMP and a user-friendly CLI interface.

## Features

- SNMP v1 support with configurable community string (default: 'public')
- SNMP Walk support for device discovery
- Mains power monitoring via GPIO
- DHCP and static IP configuration
- Serial CLI interface for configuration and monitoring
- Factory reset capability
- Configuration persistence

## Hardware Requirements

- Raspberry Pi Pico
- W5500 Ethernet module
- Power monitoring circuit connected to GPIO 27
- Factory reset button connected to GPIO 22
- USB Serial connection for CLI

## Pin Configuration

| Pin   | Function                    | Description                                    |
|-------|----------------------------|------------------------------------------------|
| GPIO27| Mains Power Detection      | HIGH = Power Present, LOW = Power Lost         |
| GPIO22| Factory Reset Button       | Pull to GND for 10 seconds to trigger reset    |
| GPIO16| W5500 MISO                | SPI Communication                              |
| GPIO17| W5500 CS                  | SPI Chip Select                                |
| GPIO18| W5500 SCK                 | SPI Clock                                      |
| GPIO19| W5500 MOSI                | SPI Communication                              |
| GPIO20| W5500 RST                 | Reset pin for W5500                            |
| GPIO21| W5500 INT                 | Interrupt pin                                  |

## SNMP Implementation

### MIB Structure

```
.1.3.6.1.2.1.1 (system)
  └── .1 = sysDescr
  └── .3 = sysUpTime
  └── .4 = sysContact
  └── .5 = sysName
  └── .6 = sysLocation

.1.3.6.1.4.1.63050 (private)
  └── .1 = mainsPowerStatus (INTEGER: 0=off, 1=on)
```

### Supported Operations
- GetRequest
- GetNextRequest (for SNMP walks)
- Community: Configurable (default: "public")

## Network Configuration

The device supports both DHCP and static IP configuration:
- Default: DHCP enabled
- Configurable through CLI interface
- Network settings persist across reboots
- Factory reset returns to DHCP mode

## CLI Menu System

Connect to the device using a serial terminal (115200 baud) to access the configuration menu:

```
=== SNMP Agent Configuration ===
1. Show Current Status
   - IP Address
   - Network Mode (DHCP/Static)
   - Uptime
   - Mains Power Status
   - MAC Address
   - SNMP Community String
2. Network Configuration
   - Toggle DHCP/Static
   - Set Static IP (if in Static mode)
   - Set Subnet Mask
   - Set Gateway
3. SNMP Configuration
   - Set Community String
   - View Access Statistics
4. View SNMP Statistics
   - Packets Received
   - Get Requests
   - Get Next Requests
5. Save Configuration
X. Exit
```

## Factory Reset Process

To perform a factory reset:
1. Press and hold the factory reset button (GPIO22) for 10 seconds
2. The onboard LED will start blinking rapidly to indicate reset in progress
3. The following settings will be reset:
   - Network configuration returns to DHCP
   - Stored IP settings cleared
   - SNMP statistics cleared
   - System name reset to default
   - SNMP community string reset to "public"
4. The device will automatically restart with default settings

## Building and Flashing

1. Clone this repository
2. Install PlatformIO
3. Build the project:
   ```bash
   pio run
   ```
4. Flash to your Pico:
   ```bash
   pio run --target upload
   ```

## Testing SNMP Functionality

You can test the SNMP agent using standard SNMP tools:

```bash
# Get system description
snmpget -v 1 -c public [device-ip] .1.3.6.1.2.1.1.1.0

# Get mains power status
snmpget -v 1 -c public [device-ip] .1.3.6.1.4.1.63050.1.0

# Perform SNMP walk
snmpwalk -v 1 -c public [device-ip]
```

Note: Replace 'public' with your configured community string if changed from default.

## Configuration Storage

The device uses LittleFS for persistent configuration storage:
- JSON format for easy parsing and modification
- Wear leveling provided by LittleFS
- Configuration stored in '/config.json'
- Automatic corruption detection and recovery
- Backup configuration maintained

### Configuration Structure
```json
{
  "network": {
    "dhcp": true,
    "static_ip": "",
    "subnet": "",
    "gateway": ""
  },
  "snmp": {
    "community": "public"
  },
  "system": {
    "name": "",
    "location": "",
    "contact": ""
  }
}
```

## Technical Details

- Network Stack: Custom implementation without external libraries
- SNMP Version: 1
- Default Community String: "public" (configurable via CLI)
- Serial Configuration: 115200 baud, 8N1
- Power Monitoring: Active HIGH on GPIO27
- Configuration Storage: LittleFS with JSON format
- Network: Auto-negotiation, 10/100 Mbps

## Troubleshooting

1. If the device doesn't obtain an IP address:
   - Check Ethernet cable connection
   - Verify network switch/router DHCP settings
   - Try static IP configuration through CLI

2. If power monitoring shows incorrect status:
   - Verify voltage levels on GPIO27
   - Check power monitoring circuit connections

3. If factory reset doesn't work:
   - Ensure the button is properly connected to GPIO22
   - Hold the button for full 10 seconds
   - Check LED feedback during reset process

## Safety Considerations

- The power monitoring circuit should include proper isolation
- Use appropriate voltage dividers/optocouplers for GPIO protection
- Ensure proper grounding of all components

## License

This project is released under the MIT License. See the LICENSE file for details.
# SNMPAgent
