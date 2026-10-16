#include "snmp_service.h"

#include <Arduino.h>
#include <WiFiUdp.h>
#include <mains_input.h>
#include <netconfig.h>
#include <settings.h>
#include <snmp_trap.h>
#include "version.h"

// Requests handled per snmpPoll() call, so a flood cannot starve the menu
const uint8_t MAX_PACKETS_PER_POLL = 4;

static const char* const SYS_DESCR = "RF-UPS-Monitor v" FIRMWARE_VERSION " mains power monitor (RP2040, W5500)";

static WiFiUDP udp;
static bool started = false;
static bool coldStartSent = false;
static uint8_t agentAddr[4] = {0, 0, 0, 0};

static SnmpAgent agent(currentSettings.community);
static RateLimiter limiter(SNMP_RATE_PER_SECOND, SNMP_RATE_BURST, SNMP_GLOBAL_RATE_PER_SECOND,
                           SNMP_GLOBAL_RATE_BURST);
static AccessLog accessLog;
static TrapThrottle authTrapThrottle(AUTH_TRAP_INTERVAL_MS);
static SnmpServiceStats serviceStats = {};

static uint8_t inBuf[SNMP_MAX_MESSAGE];
static uint8_t outBuf[SNMP_MAX_MESSAGE];

/**
 * @brief sysUpTime in hundredths of a second, from the 64-bit microsecond timer
 *
 * Wraps after 497 days like any TimeTicks counter, not after millis()' 49 days.
 */
static uint32_t uptimeTicks() {
    return (uint32_t)(time_us_64() / 10000);
}

/**
 * @brief Collect the values the MIB reports
 */
static SnmpDeviceState captureState() {
    SnmpDeviceState s = {};
    s.uptimeTicks = uptimeTicks();
    // Read from the settings on every request, so menu changes apply immediately
    s.sysDescr = SYS_DESCR;
    s.sysContact = currentSettings.sysContact;
    s.sysName = currentSettings.sysName;
    s.sysLocation = currentSettings.sysLocation;

    MainsSnapshot mains;
    if (mainsRead(mains)) {
        s.mainsPresent = mains.present;
        s.mainsChanges = mains.changes;
        if (mains.changes > 0) {
            // sysUpTime at the change, from the elapsed time (wrap-safe for 49 days)
            uint32_t agoTicks = (millis() - mains.lastChangeMs) / 10;
            s.mainsLastChangeTicks = s.uptimeTicks - agoTicks;
        }
    }
    return s;
}

/**
 * @brief Host-order 32-bit value of an address, for the rate limiter and log
 */
static uint32_t toU32(const IPAddress& ip) {
    return ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
}

/**
 * @brief Record a rejected request and print it when it starts a new log entry
 */
static void logAccess(uint32_t source, AccessEvent event) {
    if (accessLog.record(source, event, millis())) {
        char ip[16];
        formatIPv4(source, ip);
        Serial.printf("\nSNMP access: %s from %s\n", accessEventName(event), ip);
    }
}

/**
 * @brief Send an encoded trap to the configured destination, if any
 */
static void sendTrap(size_t len, const char* what) {
    uint8_t target[4];
    if (currentSettings.trapTarget[0] == '\0' || !parseIPv4(currentSettings.trapTarget, target)) return;
    if (len == 0) {
        serviceStats.trapsFailed++;
        return;
    }
    IPAddress dest(target[0], target[1], target[2], target[3]);
    bool ok = udp.beginPacket(dest, SNMP_TRAP_PORT) == 1 && udp.write(outBuf, len) == len && udp.endPacket() == 1;
    if (ok) {
        serviceStats.trapsSent++;
        Serial.printf("SNMP trap %s sent to %s\n", what, currentSettings.trapTarget);
    } else {
        serviceStats.trapsFailed++;
        Serial.printf("SNMP trap %s to %s failed\n", what, currentSettings.trapTarget);
    }
}

bool snmpBegin() {
    limiter.configureTrusted(SNMP_TRUSTED_RATE_PER_SECOND, SNMP_TRUSTED_RATE_BURST, SNMP_MAX_TRUSTED_SOURCES,
                             SNMP_TRUST_TIMEOUT_MS);
    started = udp.begin(SNMP_PORT) == 1;
    Serial.printf(started ? "SNMP agent listening on UDP %u\n" : "SNMP: cannot bind UDP %u\n", SNMP_PORT);
    return started;
}

void snmpNetworkUp(const IPAddress& localIP) {
    for (int i = 0; i < 4; i++) agentAddr[i] = localIP[i];
    if (!started || coldStartSent) return;
    coldStartSent = true;
    sendTrap(snmpBuildColdStartTrap(currentSettings.community, agentAddr, uptimeTicks(), outBuf, sizeof(outBuf)),
             "coldStart");
}

void snmpCommunityChanged() {
    // Trust was earned with the old community and must not carry over
    limiter.clearTrust();
    Serial.println("SNMP: community changed, rate-limit trust cleared");
}

void snmpNotifyMainsChange() {
    if (!started) return;
    SnmpDeviceState state = captureState();
    sendTrap(snmpBuildMainsTrap(currentSettings.community, agentAddr, state, outBuf, sizeof(outBuf)),
             state.mainsPresent ? "mainsPowerRestored" : "mainsPowerLost");
}

void snmpPoll() {
    if (!started) return;

    for (uint8_t i = 0; i < MAX_PACKETS_PER_POLL; i++) {
        int size = udp.parsePacket();
        if (size <= 0) return;

        uint32_t source = toU32(udp.remoteIP());
        if ((size_t)size > sizeof(inBuf)) {
            udp.flush();  // Larger than any valid SNMP v1 request we accept
            logAccess(source, AccessEvent::Malformed);
            continue;
        }
        if (!limiter.allow(source, millis())) {
            udp.flush();
            serviceStats.rateLimited++;
            logAccess(source, AccessEvent::RateLimited);
            continue;
        }

        int n = udp.read(inBuf, sizeof(inBuf));
        if (n <= 0) continue;

        IPAddress remote = udp.remoteIP();
        uint16_t remotePort = udp.remotePort();
        size_t len = agent.handle(inBuf, (size_t)n, outBuf, sizeof(outBuf), captureState());

        switch (agent.lastOutcome()) {
        case SnmpOutcome::Responded:
        case SnmpOutcome::NoRoom:
            // Right community: exempt from the shared bucket, so spoofed floods cannot starve it
            limiter.markTrusted(source);
            break;
        case SnmpOutcome::BadCommunity:
            logAccess(source, AccessEvent::BadCommunity);
            if (authTrapThrottle.allow(millis())) {
                sendTrap(snmpBuildAuthFailureTrap(currentSettings.community, agentAddr, uptimeTicks(), outBuf,
                                                  sizeof(outBuf)),
                         "authenticationFailure");
            }
            break;
        case SnmpOutcome::BadVersion:  logAccess(source, AccessEvent::BadVersion); break;
        case SnmpOutcome::ParseError:  logAccess(source, AccessEvent::Malformed); break;
        case SnmpOutcome::SetRejected:
            limiter.markTrusted(source);
            logAccess(source, AccessEvent::SetRejected);
            break;
        default: break;
        }
        if (len == 0) continue;

        udp.beginPacket(remote, remotePort);
        udp.write(outBuf, len);
        udp.endPacket();
    }
}

const SnmpStats& snmpStats() {
    return agent.stats();
}

const SnmpServiceStats& snmpServiceStats() {
    return serviceStats;
}

const AccessLog& snmpAccessLog() {
    return accessLog;
}
