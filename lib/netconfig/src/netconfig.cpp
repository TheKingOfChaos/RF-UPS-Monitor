#include "netconfig.h"

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
