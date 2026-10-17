#pragma once

/**
 * @file source_key.h
 * @brief The rate-limit / access-log key (lib/access_control) of a request's source address.
 *
 * The core's IPAddress returns 0 from its IPv4 accessors for an IPv6 address,
 * so it must not be read with operator[] or as uint32_t.
 */

#include <IPAddress.h>
#include <access_control.h>
#include <lwip/ip_addr.h>

/**
 * @brief Key of a source address (the full IPv4 or IPv6 address)
 * @return false for IPv6 sources that cannot come from the LAN (::/32): drop the request
 */
inline bool sourceKeyOf(const IPAddress& ip, SourceKey& key) {
    const ip_addr_t* a = ip;
#if LWIP_IPV6
    if (IP_IS_V6(a)) return sourceKeyIPv6((const uint8_t*)ip_2_ip6(a)->addr, key);
#endif
    key = sourceKeyIPv4((const uint8_t*)&ip_2_ip4(a)->addr);
    return true;
}
