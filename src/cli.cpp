#include "cli.h"

#include <mains_input.h>
#include <netconfig.h>
#include <settings.h>

// Menu state machine
enum CliState {
    CLI_MENU,           // Waiting for a menu choice
    CLI_STATIC_IP,      // Entering static IP
    CLI_STATIC_SUBNET,  // Entering subnet mask
    CLI_STATIC_GATEWAY, // Entering gateway
    CLI_CONFIRM         // Confirming save and reboot
};

static CliState state = CLI_MENU;
static char lineBuf[32];
static uint8_t lineLen = 0;

// Values being entered, applied only on confirm
static char newIP[16];
static char newSubnet[16];
static char newGateway[16];
static bool newDhcp;

/**
 * @brief Print the main menu
 */
static void printMenu() {
    Serial.println();
    Serial.println("=== RF-UPS-Monitor Network Menu ===");
    Serial.println("1. Show network status");
    Serial.println("2. Use DHCP");
    Serial.println("3. Set static IP");
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
            handleLine(lineBuf);
        } else if (c == '\b' || c == 0x7F) {
            if (lineLen > 0) {
                lineLen--;
                Serial.print("\b \b");
            }
        } else if (isprint((unsigned char)c) && lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = c;
            Serial.print(c);    // Echo, terminals like PuTTY don't echo locally
        }
    }
}
