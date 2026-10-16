#pragma once

/**
 * @file access_control.h
 * @brief Per-source request rate limiting and a log of rejected access attempts.
 *
 * Pure C++ with no Arduino dependency, unit tested on the host. The SNMP
 * service checks every request with RateLimiter::allow() and records
 * rejections in AccessLog; the serial menu shows the log.
 */

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Token bucket per source address
 *
 * Each source may send `burst` requests at once and then `ratePerSecond` on
 * average. Sources are tracked in a small table; when it is full, the source
 * seen least recently is replaced. Wrap-around safe for millis().
 */
class RateLimiter {
public:
    /// Sources tracked at the same time
    static constexpr uint8_t MAX_SOURCES = 8;

    RateLimiter(uint32_t ratePerSecond, uint32_t burst) : _rate(ratePerSecond), _burst(burst) {}

    /**
     * @brief Take one token for this source
     * @param source IPv4 address as a 32-bit value
     * @return true if the request may be handled
     */
    bool allow(uint32_t source, uint32_t nowMs);

    /// Requests refused since start
    uint32_t refused() const { return _refused; }

private:
    struct Bucket {
        uint32_t source;
        uint32_t milliTokens;  // tokens * 1000, avoids floating point
        uint32_t lastMs;
        bool used;
    };

    Bucket* find(uint32_t source, uint32_t nowMs);

    uint32_t _rate;
    uint32_t _burst;
    uint32_t _refused = 0;
    Bucket _buckets[MAX_SOURCES] = {};
};

/// Kinds of rejected access
enum class AccessEvent : uint8_t {
    BadCommunity,  ///< SNMP request with a wrong community
    BadVersion,    ///< SNMPv2c/v3 request
    Malformed,     ///< Not a valid SNMP message
    RateLimited,   ///< Dropped by the rate limiter
    SetRejected    ///< SetRequest with the right community (all objects are read-only)
};

/**
 * @brief Short name of an event for the log display
 */
const char* accessEventName(AccessEvent event);

/**
 * @brief Ring buffer of the most recent rejected access attempts
 *
 * Repeated events from the same source and of the same kind are folded into
 * one entry with a count, so a flood does not push everything else out.
 */
class AccessLog {
public:
    static constexpr uint8_t CAPACITY = 16;

    struct Entry {
        uint32_t firstMs;   ///< millis() of the first event
        uint32_t lastMs;    ///< millis() of the latest event
        uint32_t source;    ///< IPv4 address
        uint32_t count;     ///< Events folded into this entry
        AccessEvent event;
    };

    /**
     * @brief Record an event
     * @return true if it started a new entry (worth printing), false if it was folded
     */
    bool record(uint32_t source, AccessEvent event, uint32_t nowMs);

    /// Number of entries held (at most CAPACITY)
    uint8_t size() const { return _size; }

    /// Entry by age, 0 = newest
    const Entry& newest(uint8_t index) const;

    /// Events recorded since start, including folded and overwritten ones
    uint32_t total() const { return _total; }

private:
    Entry _entries[CAPACITY] = {};
    uint8_t _head = 0;   // Next slot to write
    uint8_t _size = 0;
    uint32_t _total = 0;
};

/**
 * @brief Format an IPv4 address (host-order 32-bit) as dotted quad
 * @param buf At least 16 bytes
 */
void formatIPv4(uint32_t address, char* buf);
