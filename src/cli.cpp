#include "cli.h"

#include <mains_input.h>
#include <netconfig.h>
#include <settings.h>
#include "board_temp.h"
#include "net_stats.h"
#include "snmp_service.h"

// Menu state machine
enum CliState {
    CLI_MENU,           // Waiting for a menu choice
    CLI_STATIC_IP,      // Entering static IP
    CLI_STATIC_SUBNET,  // Entering subnet mask
    CLI_STATIC_GATEWAY, // Entering gateway
    CLI_CONFIRM,        // Confirming network change, save and reboot
    CLI_SNMP_COMMUNITY, // Entering SNMP community
    CLI_SNMP_NAME,      // Entering sysName
    CLI_SNMP_CONTACT,   // Entering sysContact
    CLI_SNMP_LOCATION,  // Entering sysLocation
    CLI_SNMP_TRAP,      // Entering trap destination
    CLI_SNMP_TRAP_VER,  // Entering trap version
    CLI_SNMP_CONFIRM    // Confirming SNMP change, save without reboot
};

static CliState state = CLI_MENU;
static char lineBuf[96];
static uint8_t lineLen = 0;
static bool lineOverflow = false;  // More characters were typed than lineBuf holds

// Network values being entered, applied only on confirm
static char newIP[16];
static char newSubnet[16];
static char newGateway[16];
static bool newDhcp;

// SNMP values being entered, applied only on confirm
static char newCommunity[COMMUNITY_MAX_LENGTH + 1];
static char newSysName[SYS_TEXT_MAX_LENGTH + 1];
static char newSysContact[SYS_TEXT_MAX_LENGTH + 1];
static char newSysLocation[SYS_TEXT_MAX_LENGTH + 1];
static char newTrapTarget[16];
static uint8_t newTrapVersion;

static const char* trapVersionName(uint8_t v) {
    return v == TRAP_VERSION_V1 ? "1" : "2c";
}

/**
 * @brief Print the main menu
 */
static void printMenu() {
    Serial.println();
    Serial.println("=== RF-UPS-Monitor Menu ===");
    Serial.println("1. Show status");
    Serial.println("2. Use DHCP");
    Serial.println("3. Set static IP");
    Serial.println("4. SNMP settings");
    Serial.println("5. Access log");
    Serial.println("?. Show this menu");
    Serial.print("> ");
}

/**
 * @brief Show the stored settings and the live interface addresses
 */
static void printStatus() {
    Serial.println();

    MainsSnapshot mains;
    if (!mainsRead(mains)) {
        Serial.println("Mains power: unknown (monitor not running)");
    } else if (mains.changes == 0) {
        Serial.printf("Mains power: %s (no changes since boot)\n", mains.present ? "present" : "LOST");
    } else {
        Serial.printf("Mains power: %s (%lu changes, last change %lu s ago)\n",
                      mains.present ? "present" : "LOST", (unsigned long)mains.changes,
                      (unsigned long)((millis() - mains.lastChangeMs) / 1000));
    }

    const SnmpStats& snmp = snmpStats();
    Serial.printf("SNMP: %lu packets, %lu responses (get %lu, getnext %lu, set %lu), "
                  "bad community %lu, bad version %lu, malformed %lu\n",
                  (unsigned long)snmp.inPackets, (unsigned long)snmp.outResponses,
                  (unsigned long)snmp.inGetRequests, (unsigned long)snmp.inGetNextRequests,
                  (unsigned long)snmp.inSetRequests, (unsigned long)snmp.inBadCommunity,
                  (unsigned long)snmp.inBadVersions, (unsigned long)snmp.inParseErrors);
    Serial.printf("SNMP v2c: %lu requests, %lu getbulk\n", (unsigned long)snmp.inV2cRequests,
                  (unsigned long)snmp.inGetBulkRequests);

    int32_t t = boardTempDeciC();
    Serial.printf("Board temperature: %ld.%ld C\n", (long)(t / 10), (long)abs(t % 10));
    SnmpInterfaceState ifs;
    netStatsCapture(ifs);
    Serial.printf("Ethernet: link %s, %lu Mbit/s, in %llu bytes / %llu packets, out %llu bytes / %llu packets\n",
                  ifs.linkUp ? "up" : "down", (unsigned long)(ifs.speedBps / 1000000),
                  (unsigned long long)ifs.counters.inOctets,
                  (unsigned long long)(ifs.counters.inUcastPkts + ifs.counters.inMulticastPkts +
                                       ifs.counters.inBroadcastPkts),
                  (unsigned long long)ifs.counters.outOctets,
                  (unsigned long long)(ifs.counters.outUcastPkts + ifs.counters.outMulticastPkts +
                                       ifs.counters.outBroadcastPkts));
    Serial.printf("Ethernet drops: %lu discarded (no buffer), %lu errors, %lu unknown protocol, %lu send errors\n",
                  (unsigned long)ifs.counters.inDiscards, (unsigned long)ifs.counters.inErrors,
                  (unsigned long)ifs.counters.inUnknownProtos, (unsigned long)ifs.counters.outErrors);
    const SnmpServiceStats& svc = snmpServiceStats();
    Serial.printf("SNMP security: %lu rate limited, %lu access log events\n",
                  (unsigned long)svc.rateLimited, (unsigned long)snmpAccessLog().total());
    Serial.printf("SNMP community: \"%s\"\n", currentSettings.community);
    Serial.printf("sysName: \"%s\"\n", currentSettings.sysName);
    Serial.printf("sysContact: \"%s\"\n", currentSettings.sysContact);
    Serial.printf("sysLocation: \"%s\"\n", currentSettings.sysLocation);
    Serial.printf("Trap destination: \"%s\" (%lu sent, %lu failed)\n", currentSettings.trapTarget,
                  (unsigned long)svc.trapsSent, (unsigned long)svc.trapsFailed);
    Serial.printf("Trap version: %s\n", currentSettings.trapVersion == TRAP_VERSION_V1 ? "v1" : "v2c");

    Serial.printf("Mode: %s\n", currentSettings.dhcpEnabled ? "DHCP" : "Static");
    Serial.printf("Stored static IP: %s / %s, gateway %s\n",
                  currentSettings.staticIP, currentSettings.subnetMask, currentSettings.gateway);
    Serial.println("Live interface:");
    printNetworkInfo();
}

/**
 * @brief Prompt for a value, showing the current one as default
 */
static void prompt(const char* name, const char* current) {
    Serial.printf("%s [%s]: ", name, current);
}

/**
 * @brief Accept an IPv4 address line, empty keeps the current value
 * @return true if dest now holds a valid address
 */
static bool takeAddress(const char* input, const char* current, char* dest) {
    const char* value = input[0] ? input : current;
    uint8_t octets[4];
    if (!parseIPv4(value, octets)) {
        Serial.printf("'%s' is not a valid IPv4 address\n", value);
        return false;
    }
    strlcpy(dest, value, 16);
    return true;
}

/**
 * @brief Start entering a static configuration from the IP prompt
 */
static void startStaticEntry() {
    prompt("IP address", currentSettings.staticIP);
    state = CLI_STATIC_IP;
}

/**
 * @brief Accept a text line for an SNMP setting
 *
 * Empty input keeps the current value; "-" clears it when empty is allowed.
 * The menu only accepts printable characters, so length is the main check.
 *
 * @return true if dest now holds a valid value
 */
static bool takeText(const char* input, const char* current, char* dest, size_t maxLength, bool allowEmpty) {
    const char* value = input;
    if (input[0] == '\0') {
        value = current;
    } else if (strcmp(input, "-") == 0) {
        value = "";
    }
    if (value[0] == '\0' && !allowEmpty) {
        Serial.println("Must not be empty");
        return false;
    }
    if (strlen(value) > maxLength) {
        Serial.printf("Too long, at most %u characters\n", (unsigned)maxLength);
        return false;
    }
    if (!settingsTextIsValid(value, maxLength, allowEmpty)) {
        Serial.println("Only printable ASCII characters are allowed");
        return false;
    }
    strlcpy(dest, value, maxLength + 1);
    return true;
}

/**
 * @brief Prompt for the SNMP setting that belongs to the current state
 */
static void promptSnmp() {
    switch (state) {
    case CLI_SNMP_COMMUNITY: prompt("Community", currentSettings.community); break;
    case CLI_SNMP_NAME:      prompt("sysName", currentSettings.sysName); break;
    case CLI_SNMP_CONTACT:   prompt("sysContact", currentSettings.sysContact); break;
    case CLI_SNMP_LOCATION:  prompt("sysLocation", currentSettings.sysLocation); break;
    case CLI_SNMP_TRAP:      prompt("Trap destination IP", currentSettings.trapTarget); break;
    case CLI_SNMP_TRAP_VER:  prompt("Trap version (1/2c)", trapVersionName(currentSettings.trapVersion)); break;
    default: break;
    }
}

/**
 * @brief Accept the trap destination: empty keeps, "-" turns traps off, else an IPv4 address
 */
static bool takeTrapTarget(const char* input) {
    if (input[0] == '\0') {
        strlcpy(newTrapTarget, currentSettings.trapTarget, sizeof(newTrapTarget));
        return true;
    }
    if (strcmp(input, "-") == 0) {
        newTrapTarget[0] = '\0';
        return true;
    }
    uint8_t octets[4];
    if (strlen(input) >= sizeof(newTrapTarget) || !parseIPv4(input, octets)) {
        Serial.printf("'%s' is not a valid IPv4 address\n", input);
        return false;
    }
    strlcpy(newTrapTarget, input, sizeof(newTrapTarget));
    return true;
}

/**
 * @brief Show the access log, newest first
 */
static void printAccessLog() {
    const AccessLog& log = snmpAccessLog();
    uint32_t now = millis();
    Serial.println();
    Serial.printf("Access log: %u entries, %lu events since boot\n", log.size(), (unsigned long)log.total());
    for (uint8_t i = 0; i < log.size(); i++) {
        const AccessLog::Entry& e = log.newest(i);
        char ip[16];
        formatIPv4(e.source, ip);
        Serial.printf("  %-15s %-14s x%-6lu last %lu s ago\n", ip, accessEventName(e.event),
                      (unsigned long)e.count, (unsigned long)((now - e.lastMs) / 1000));
    }
    Serial.print("> ");
}

/**
 * @brief Show the pending SNMP change and ask for confirmation
 */
static void askSnmpConfirm() {
    Serial.println();
    Serial.printf("New SNMP settings: community \"%s\", sysName \"%s\", sysContact \"%s\", sysLocation \"%s\", "
                  "trap destination \"%s\", trap version %s\n",
                  newCommunity, newSysName, newSysContact, newSysLocation, newTrapTarget,
                  newTrapVersion == TRAP_VERSION_V1 ? "v1" : "v2c");
    Serial.print("Save? (y/n): ");
    state = CLI_SNMP_CONFIRM;
}

/**
 * @brief Show the pending change and ask for confirmation
 */
static void askConfirm() {
    Serial.println();
    if (newDhcp) {
        Serial.println("New mode: DHCP");
    } else {
        Serial.printf("New mode: Static %s / %s, gateway %s\n", newIP, newSubnet, newGateway);
    }
    Serial.print("Save and reboot? (y/n): ");
    state = CLI_CONFIRM;
}

/**
 * @brief Handle one complete input line according to the current state
 */
static void handleLine(const char* line) {
    switch (state) {
    case CLI_MENU:
        if (strcmp(line, "1") == 0) {
            printStatus();
            Serial.print("> ");
        } else if (strcmp(line, "2") == 0) {
            newDhcp = true;
            strlcpy(newIP, currentSettings.staticIP, sizeof(newIP));
            strlcpy(newSubnet, currentSettings.subnetMask, sizeof(newSubnet));
            strlcpy(newGateway, currentSettings.gateway, sizeof(newGateway));
            askConfirm();
        } else if (strcmp(line, "3") == 0) {
            newDhcp = false;
            Serial.println("Press Enter to keep the value in brackets.");
            startStaticEntry();
        } else if (strcmp(line, "4") == 0) {
            Serial.println("Press Enter to keep the value in brackets, '-' to clear it.");
            state = CLI_SNMP_COMMUNITY;
            promptSnmp();
        } else if (strcmp(line, "5") == 0) {
            printAccessLog();
        } else if (line[0] == '\0') {
            Serial.print("> ");
        } else {
            printMenu();
        }
        break;

    case CLI_STATIC_IP:
        if (takeAddress(line, currentSettings.staticIP, newIP)) {
            prompt("Subnet mask", currentSettings.subnetMask);
            state = CLI_STATIC_SUBNET;
        } else {
            prompt("IP address", currentSettings.staticIP);
        }
        break;

    case CLI_STATIC_SUBNET: {
        uint8_t mask[4];
        if (!takeAddress(line, currentSettings.subnetMask, newSubnet)) {
            prompt("Subnet mask", currentSettings.subnetMask);
        } else if (parseIPv4(newSubnet, mask) && !isValidSubnetMask(mask)) {
            Serial.println(staticConfigErrorMessage(StaticConfigError::NonContiguousMask));
            prompt("Subnet mask", currentSettings.subnetMask);
        } else {
            prompt("Gateway", currentSettings.gateway);
            state = CLI_STATIC_GATEWAY;
        }
        break;
    }

    case CLI_STATIC_GATEWAY: {
        if (!takeAddress(line, currentSettings.gateway, newGateway)) {
            prompt("Gateway", currentSettings.gateway);
            break;
        }
        StaticConfigError err = validateStaticConfig(newIP, newSubnet, newGateway);
        if (err == StaticConfigError::None) {
            askConfirm();
        } else if (err == StaticConfigError::GatewayNotInSubnet || err == StaticConfigError::GatewayIsHost) {
            Serial.println(staticConfigErrorMessage(err));
            prompt("Gateway", currentSettings.gateway);
        } else {
            // IP and mask combination is unusable, start over
            Serial.println(staticConfigErrorMessage(err));
            startStaticEntry();
        }
        break;
    }

    case CLI_CONFIRM:
        if (line[0] == 'y' || line[0] == 'Y') {
            Settings updated = currentSettings;
            updated.dhcpEnabled = newDhcp;
            strlcpy(updated.staticIP, newIP, sizeof(updated.staticIP));
            strlcpy(updated.subnetMask, newSubnet, sizeof(updated.subnetMask));
            strlcpy(updated.gateway, newGateway, sizeof(updated.gateway));

            Settings saved = currentSettings;
            currentSettings = updated;
            if (saveSettings()) {
                Serial.println("Settings saved. Rebooting...");
                Serial.flush();
                delay(200);
                rp2040.reboot();
            }
            currentSettings = saved;
            Serial.println("Failed to save settings, nothing changed");
        } else {
            Serial.println("Cancelled");
        }
        state = CLI_MENU;
        printMenu();
        break;

    case CLI_SNMP_COMMUNITY:
        if (takeText(line, currentSettings.community, newCommunity, COMMUNITY_MAX_LENGTH, false)) {
            state = CLI_SNMP_NAME;
        }
        promptSnmp();
        break;

    case CLI_SNMP_NAME:
        if (takeText(line, currentSettings.sysName, newSysName, SYS_TEXT_MAX_LENGTH, true)) {
            state = CLI_SNMP_CONTACT;
        }
        promptSnmp();
        break;

    case CLI_SNMP_CONTACT:
        if (takeText(line, currentSettings.sysContact, newSysContact, SYS_TEXT_MAX_LENGTH, true)) {
            state = CLI_SNMP_LOCATION;
        }
        promptSnmp();
        break;

    case CLI_SNMP_LOCATION:
        if (takeText(line, currentSettings.sysLocation, newSysLocation, SYS_TEXT_MAX_LENGTH, true)) {
            state = CLI_SNMP_TRAP;
        }
        promptSnmp();
        break;

    case CLI_SNMP_TRAP:
        if (takeTrapTarget(line)) {
            state = CLI_SNMP_TRAP_VER;
        }
        promptSnmp();
        break;

    case CLI_SNMP_TRAP_VER:
        if (line[0] == '\0') {
            newTrapVersion = currentSettings.trapVersion;
        } else if (strcmp(line, "1") == 0) {
            newTrapVersion = TRAP_VERSION_V1;
        } else if (strcmp(line, "2c") == 0 || strcmp(line, "2C") == 0) {
            newTrapVersion = TRAP_VERSION_V2C;
        } else {
            Serial.println("Enter 1 or 2c");
            promptSnmp();
            break;
        }
        askSnmpConfirm();
        break;

    case CLI_SNMP_CONFIRM:
        if (line[0] == 'y' || line[0] == 'Y') {
            Settings saved = currentSettings;
            bool communityChanged = strcmp(currentSettings.community, newCommunity) != 0;
            strlcpy(currentSettings.community, newCommunity, sizeof(currentSettings.community));
            strlcpy(currentSettings.sysName, newSysName, sizeof(currentSettings.sysName));
            strlcpy(currentSettings.sysContact, newSysContact, sizeof(currentSettings.sysContact));
            strlcpy(currentSettings.sysLocation, newSysLocation, sizeof(currentSettings.sysLocation));
            strlcpy(currentSettings.trapTarget, newTrapTarget, sizeof(currentSettings.trapTarget));
            currentSettings.trapVersion = newTrapVersion;
            // The SNMP agent reads these on every request, so no reboot is needed
            if (saveSettings()) {
                if (communityChanged) snmpCommunityChanged();
                Serial.println("SNMP settings saved and active");
            } else {
                currentSettings = saved;
                Serial.println("Failed to save settings, nothing changed");
            }
        } else {
            Serial.println("Cancelled");
        }
        state = CLI_MENU;
        printMenu();
        break;
    }
}

void cliBegin() {
    printMenu();
}

void cliProcess() {
    static bool lastWasCR = false;

    while (Serial.available()) {
        char c = Serial.read();

        // Ignore the \n of a \r\n pair
        bool skip = (c == '\n' && lastWasCR);
        lastWasCR = (c == '\r');
        if (skip) continue;

        if (c == '\r' || c == '\n') {
            Serial.println();
            lineBuf[lineLen] = '\0';
            lineLen = 0;
            if (lineOverflow) {
                // Never act on a silently truncated value
                lineOverflow = false;
                Serial.printf("Input too long (max %u characters), ignored\n", (unsigned)(sizeof(lineBuf) - 1));
                if (state == CLI_MENU) {
                    Serial.print("> ");
                } else if (state >= CLI_SNMP_COMMUNITY && state <= CLI_SNMP_TRAP_VER) {
                    promptSnmp();
                } else {
                    state = CLI_MENU;
                    printMenu();
                }
                continue;
            }
            handleLine(lineBuf);
        } else if (c == '\b' || c == 0x7F) {
            if (lineLen > 0) {
                lineLen--;
                Serial.print("\b \b");
            }
        } else if (isprint((unsigned char)c)) {
            if (lineLen < sizeof(lineBuf) - 1) {
                lineBuf[lineLen++] = c;
                Serial.print(c);    // Echo, terminals like PuTTY don't echo locally
            } else {
                lineOverflow = true;
            }
        }
    }
}
