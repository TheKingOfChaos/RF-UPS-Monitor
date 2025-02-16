# LittleFS Data Directory

This directory contains the files that will be uploaded to the Pico's LittleFS filesystem.

## Files

- `settings_a.json`: Primary settings file containing device configuration
- `settings_b.json`: Backup settings file for wear leveling
- `active_file`: Flag file indicating which settings file is currently active

## Structure

The settings files use a JSON format with the following structure:
```json
{
    "community": "public",      // SNMP community string
    "dhcpEnabled": true,       // DHCP enable flag
    "staticIP": "192.168.1.100", // Static IP if DHCP disabled
    "subnetMask": "255.255.255.0", // Subnet mask
    "gateway": "192.168.1.1",   // Gateway address
    "version": 1               // Settings version for validation
}
```

## Wear Leveling

The system uses an A/B file system for wear leveling:
1. Settings are always read from the file indicated by `active_file`
2. When saving settings, the system writes to the inactive file
3. After successful write, `active_file` is updated to point to the new file
4. This ensures there is always a valid backup of settings
