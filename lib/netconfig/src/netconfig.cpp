#include "netconfig.h"

#include <string.h>

bool parseIPv4(const char* text, uint8_t out[4]) {
    if (text == nullptr) return false;

    uint8_t octets[4];
    const char* p = text;

    for (int i = 0; i < 4; i++) {
        // Each octet is 1-3 decimal digits with a value of 0-255
        int value = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return false;
            value = value * 10 + (*p - '0');
            p++;
        }
        if (digits == 0 || value > 255) return false;
        octets[i] = (uint8_t)value;

        // Octets are separated by '.', and the string must end after the fourth
        if (i < 3) {
            if (*p != '.') return false;
            p++;
        }
    }
    if (*p != '\0') return false;

    for (int i = 0; i < 4; i++) out[i] = octets[i];
    return true;
}

uint32_t ipv4ToU32(const uint8_t addr[4]) {
    return ((uint32_t)addr[0] << 24) | ((uint32_t)addr[1] << 16) |
           ((uint32_t)addr[2] << 8) | (uint32_t)addr[3];
}

bool isValidSubnetMask(const uint8_t mask[4]) {
    uint32_t m = ipv4ToU32(mask);
    if (m == 0) return false;
    // Inverted host bits must be of the form 0...01...1
    uint32_t hostBits = ~m;
    return (hostBits & (hostBits + 1)) == 0;
}

bool isSameSubnet(const uint8_t a[4], const uint8_t b[4], const uint8_t mask[4]) {
    uint32_t m = ipv4ToU32(mask);
    return (ipv4ToU32(a) & m) == (ipv4ToU32(b) & m);
}

StaticConfigError validateStaticConfig(const char* ip, const char* mask, const char* gateway) {
    uint8_t ipOct[4], maskOct[4], gwOct[4];

    if (!parseIPv4(ip, ipOct)) return StaticConfigError::InvalidIP;
    if (!parseIPv4(mask, maskOct)) return StaticConfigError::InvalidMask;
    if (!isValidSubnetMask(maskOct)) return StaticConfigError::NonContiguousMask;
    if (!parseIPv4(gateway, gwOct)) return StaticConfigError::InvalidGateway;

    uint32_t m = ipv4ToU32(maskOct);
    uint32_t host = ipv4ToU32(ipOct);
    // /31 and /32 have no separate network and broadcast addresses
    if (m < 0xFFFFFFFEu) {
        uint32_t hostPart = host & ~m;
        if (hostPart == 0 || hostPart == ~m) return StaticConfigError::HostIsNetworkOrBroadcast;
    }

    if (!isSameSubnet(ipOct, gwOct, maskOct)) return StaticConfigError::GatewayNotInSubnet;
    if (ipv4ToU32(gwOct) == host) return StaticConfigError::GatewayIsHost;

    return StaticConfigError::None;
}

const char* staticConfigErrorMessage(StaticConfigError error) {
    switch (error) {
    case StaticConfigError::None:                     return "OK";
    case StaticConfigError::InvalidIP:                return "IP address is not a valid IPv4 address";
    case StaticConfigError::InvalidMask:              return "Subnet mask is not a valid IPv4 address";
    case StaticConfigError::NonContiguousMask:        return "Subnet mask must be contiguous, e.g. 255.255.255.0";
    case StaticConfigError::InvalidGateway:           return "Gateway is not a valid IPv4 address";
    case StaticConfigError::HostIsNetworkOrBroadcast: return "IP address is the network or broadcast address of the subnet";
    case StaticConfigError::GatewayNotInSubnet:       return "Gateway is not in the same subnet as the IP address";
    case StaticConfigError::GatewayIsHost:            return "Gateway must differ from the IP address";
    }
    return "Unknown error";
}

static bool isHostnameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

bool hostnameIsValid(const char* name) {
    if (name == nullptr || name[0] == '\0' || name[0] == '-') return false;
    size_t len = 0;
    for (const char* c = name; *c; c++, len++) {
        if (len >= HOSTNAME_MAX_LENGTH || !isHostnameChar(*c)) return false;
    }
    return name[len - 1] != '-';
}

void hostnameFromText(const char* text, char* out) {
    if (hostnameIsValid(text)) {
        strcpy(out, text);
        return;
    }
    size_t len = 0;
    bool pendingDash = false;
    for (const char* c = text ? text : ""; *c && len < HOSTNAME_MAX_LENGTH; c++) {
        if (isHostnameChar(*c) && *c != '-') {
            if (pendingDash && len > 0) {
                if (len + 2 > HOSTNAME_MAX_LENGTH) break;  // no room for '-' and a letter: stop at the word end
                out[len++] = '-';
            }
            pendingDash = false;
            out[len++] = *c;
        } else {
            pendingDash = true;  // '-' and anything invalid: at most one '-' between letters
        }
    }
    out[len] = '\0';
    if (!hostnameIsValid(out)) {
        strncpy(out, HOSTNAME_FALLBACK, HOSTNAME_MAX_LENGTH);
        out[HOSTNAME_MAX_LENGTH] = '\0';
    }
}
