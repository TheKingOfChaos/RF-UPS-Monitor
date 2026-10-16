#pragma once

/**
 * @file netconfig.h
 * @brief IPv4 address parsing and static network configuration validation.
 *
 * Pure C++ with no Arduino dependency, so it is unit tested on the host
 * (`pio test -e native`). Used by the serial menu and by settings loading.
 */

#include <stdint.h>

/**
 * @brief Result of validating a static IP configuration
 */
enum class StaticConfigError : uint8_t {
    None = 0,            ///< Configuration is valid
    InvalidIP,           ///< IP address is not a dotted-quad IPv4 address
    InvalidMask,         ///< Subnet mask is not a dotted-quad IPv4 address
    NonContiguousMask,   ///< Subnet mask one-bits are not contiguous, or mask is 0.0.0.0
    InvalidGateway,      ///< Gateway is not a dotted-quad IPv4 address
    HostIsNetworkOrBroadcast, ///< IP is the network or broadcast address of its subnet
    GatewayNotInSubnet,  ///< Gateway is outside the subnet of the IP address
    GatewayIsHost        ///< Gateway equals the device IP address
};

/**
 * @brief Parse a strict dotted-quad IPv4 address such as "10.10.6.111"
 *
 * Accepts exactly four decimal octets 0-255 separated by '.', each with 1-3
 * digits. Rejects signs, spaces, empty octets, and trailing characters.
 *
 * @param text NUL-terminated input, may be nullptr
 * @param out  Receives the four octets on success, untouched on failure
 * @return true if text is a valid address
 */
bool parseIPv4(const char* text, uint8_t out[4]);

/**
 * @brief Convert four octets to a host-order 32-bit value
 */
uint32_t ipv4ToU32(const uint8_t addr[4]);

/**
 * @brief Check that a subnet mask is non-zero and its one-bits are contiguous
 * @param mask Four octets, e.g. {255, 255, 252, 0}
 * @return true for masks /1 through /32
 */
bool isValidSubnetMask(const uint8_t mask[4]);

/**
 * @brief Check whether two addresses are in the same subnet
 */
bool isSameSubnet(const uint8_t a[4], const uint8_t b[4], const uint8_t mask[4]);

/**
 * @brief Validate a complete static configuration given as strings
 *
 * Checks are applied in the order listed in StaticConfigError, and the first
 * failure is returned. /31 and /32 masks have no network/broadcast address,
 * so that check is skipped for them.
 *
 * @return StaticConfigError::None if the configuration can be applied
 */
StaticConfigError validateStaticConfig(const char* ip, const char* mask, const char* gateway);

/**
 * @brief Human-readable message for a validation error, for the serial menu
 */
const char* staticConfigErrorMessage(StaticConfigError error);
