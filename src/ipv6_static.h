#pragma once

/**
 * @file ipv6_static.h
 * @brief The static IPv6 address and gateway from the settings (serial menu option 9, web page).
 *
 * The address is added next to the link-local and SLAAC addresses as a /64
 * (lwIP treats the /64 of a static address as on-link) and goes through
 * duplicate address detection like any other. The gateway, when set, is the
 * next hop for every off-link IPv6 destination (lwIP hook
 * LWIP_HOOK_ND6_GET_GW, include/lwip_hooks.h); without it lwIP uses the
 * routers that announce themselves. Changes take effect after a reboot.
 */

#include <stdint.h>

struct netif;

/// Add the stored address and gateway to the interface. Call once after Ethernet started.
void ipv6StaticBegin(netif* nif);

/// true if the static address is configured and lwIP's address slot i holds it
bool ipv6StaticIsSlot(int i);

/// Why the configured address is not on the interface ("" if it is, or none is configured)
const char* ipv6StaticProblem();
