#pragma once

/**
 * @file dhcp_option.h
 * @brief Request and read one extra DHCP option by editing and reading Ethernet frames.
 *
 * The core's lwIP is precompiled without DHCP hooks, so it neither asks for
 * nor keeps site-specific options. The firmware sees every frame at the
 * driver (src/net_stats.cpp), so it
 *  - adds the option code to the Parameter Request List (option 55) of every
 *    outgoing DISCOVER/REQUEST/INFORM, in place, using a padding byte after
 *    the END option (lwIP always pads), and fixes the UDP checksum;
 *  - reads the option from incoming DHCPACKs addressed to this device.
 *
 * Pure C++, host tested with frames captured from the device and from Kea.
 */

#include <stddef.h>
#include <stdint.h>

/// Site-specific option (RFC 2132 range 224-254) carrying the SNMP trap destination (IPv4)
static const uint8_t DHCP_OPTION_TRAP_TARGET = 224;

/// Result of dhcpRequestOption()
enum class DhcpEdit : uint8_t {
    NotDhcpRequest,    ///< Not a well-formed client DISCOVER/REQUEST/INFORM with option 55: unchanged
    AlreadyRequested,  ///< The code is already in option 55: unchanged
    Added,             ///< Code appended to option 55, UDP checksum updated
    NoRoom             ///< No padding byte after END (or option 55 full): unchanged
};

/**
 * @brief Add code to the Parameter Request List of a DHCP client message, in place
 * @param frame Ethernet frame (destination MAC first); the length does not change
 */
DhcpEdit dhcpRequestOption(uint8_t* frame, size_t len, uint8_t code);

/// Result of dhcpReadAck()
enum class DhcpAck : uint8_t {
    NotDhcpReply,  ///< Not a well-formed DHCP server message (bad checksums count as malformed)
    NotForUs,      ///< chaddr is another device's MAC
    NotAck,        ///< OFFER, NAK, ...
    NoOption,      ///< DHCPACK without the option (or with a wrong length)
    Option         ///< DHCPACK with a 4-byte option: value in out
};

/**
 * @brief Read a 4-byte option (an IPv4 address) from a DHCPACK for this device
 * @param mac    This device's MAC; the ACK's chaddr must match
 * @param out    The option value when the result is Option
 * @param yiaddr The address assigned by the ACK (for every NoOption/Option result)
 */
DhcpAck dhcpReadAck(const uint8_t* frame, size_t len, const uint8_t mac[6], uint8_t code, uint8_t out[4],
                    uint8_t yiaddr[4]);
